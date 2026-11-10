#include "comms_bench.h"

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/initialize.h"
#include "bench.h"
#include "comms/protocol.h"
#include "comms/session.h"
#include "thoom/comms.h"
#include "thoom/thoom.h"
#include "webrtc/webrtc_connection.h"

// Runs the message stack over real data channels: the reliable and unreliable
// transfer matrix through the C API, the join and leave events, and a version
// mismatch over a real connection.

#define CONNECT_TIMEOUT_MS 15000
#define TRANSFER_TIMEOUT_MS 30000
#define LEAVE_TIMEOUT_MS 15000
#define SETTLE_MS 500
#define WINDOW_MESSAGES 32
#define LARGE_BODY_BYTES 60000

static bool poll_comms(thoom_comms* first, thoom_comms* second,
                       const std::function<bool()>& done, int timeout_ms) {
  const int64_t deadline = bench_now_ms() + timeout_ms;

  for (;;) {
    if (first != nullptr) {
      thoom_comms_poll(first);
    }
    if (second != nullptr) {
      thoom_comms_poll(second);
    }

    if (done()) {
      return true;
    }

    if (bench_now_ms() >= deadline) {
      return done();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

static bool network_transfer(thoom_comms* from, thoom_comms* to, bool reliable,
                             const BenchOptions& options,
                             const std::string& name, BenchReport* report) {
  const int64_t deadline = bench_now_ms() + TRANSFER_TIMEOUT_MS;
  int64_t last_received_ms = bench_now_ms();
  uint32_t sent = 0;
  uint32_t received = 0;
  uint32_t next = 0;
  uint64_t bytes = 0;
  const int64_t start = bench_now_ms();

  for (;;) {
    while (sent < options.messages && sent - received < WINDOW_MESSAGES) {
      const std::vector<uint8_t> payload =
          make_payload(options.seed, sent, options);

      if (!thoom_comms_send(from, reliable, payload.data(),
                            static_cast<uint32_t>(payload.size()))) {
        report->fail(name + ": send refused");
        return false;
      }

      sent++;
    }

    thoom_comms_poll(from);
    thoom_comms_poll(to);

    thoom_comms_message message = {};
    while (thoom_comms_receive(to, &message)) {
      uint32_t index = 0;
      if (!match_payload(message.bytes, message.size, options.seed, options,
                         &index)) {
        report->fail(name + ": corrupt payload");
        return false;
      }

      if (message.reliable != reliable) {
        report->fail(name + ": payload arrived on the wrong channel");
        return false;
      }

      if (reliable && index != next) {
        report->fail(name + ": out of order payload");
        return false;
      }

      next = index + 1;
      received++;
      bytes += message.size;
      last_received_ms = bench_now_ms();
    }

    if (sent == options.messages) {
      if (reliable && received == options.messages) {
        break;
      }

      if (!reliable && bench_now_ms() - last_received_ms >= SETTLE_MS) {
        break;
      }
    }

    if (bench_now_ms() >= deadline) {
      report->fail(name + ": timed out");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const int64_t elapsed = bench_now_ms() - start;
  const double seconds = elapsed > 0 ? elapsed / 1000.0 : 0.001;

  char stats[200];
  std::snprintf(stats, sizeof(stats),
                ": %u/%u messages, %" PRIu64 " bytes, %" PRId64
                " ms, %.0f msg/s, %.2f MB/s",
                received, options.messages, bytes, elapsed, received / seconds,
                bytes / seconds / 1.0e6);

  report->check(reliable ? received == options.messages : received > 0,
                name + stats);
  return true;
}

static bool open_pair(thoom_comms** host, thoom_comms** guest,
                      BenchReport* report, const std::string& name) {
  *host = thoom_comms_create(THOOM_COMMS_ROLE_HOST);
  *guest = thoom_comms_create(THOOM_COMMS_ROLE_GUEST);

  if (*host == nullptr || *guest == nullptr) {
    report->fail(name + ": session creation");
    return false;
  }

  const char* handle = thoom_comms_get_code(*host);
  const std::string code = handle != nullptr ? handle : "";
  if (!report->check(code.size() == 6, name + ": host code")) {
    return false;
  }

  if (!report->check(thoom_comms_set_code(*guest, code.c_str()),
                     name + ": guest join code")) {
    return false;
  }

  const char* guest_handle = thoom_comms_get_code(*guest);
  report->check(guest_handle != nullptr && code == guest_handle,
                name + ": the guest keeps the join code");

  const bool ready = poll_comms(
      *host, *guest,
      [host, guest] {
        return thoom_comms_state(*host) == THOOM_COMMS_STATE_READY &&
               thoom_comms_state(*guest) == THOOM_COMMS_STATE_READY;
      },
      CONNECT_TIMEOUT_MS);
  char detail[200];
  std::snprintf(detail, sizeof(detail), " (%s, host %d, guest %d)",
                code.c_str(), static_cast<int>(thoom_comms_state(*host)),
                static_cast<int>(thoom_comms_state(*guest)));

  return report->check(ready, name + ": both sessions ready" + detail);
}

static void run_network(const BenchOptions& options, BenchReport* report) {
  thoom_comms* host = nullptr;
  thoom_comms* guest = nullptr;

  if (!open_pair(&host, &guest, report, "network")) {
    std::fprintf(
        stderr,
        "comms: no connection. Start the signalling server and set "
        "THOOM_SIGNALLING_URL, for example ws://localhost:8080/sc/.\n");
    thoom_comms_destroy(guest);
    thoom_comms_destroy(host);
    return;
  }

  const uint64_t host_pid = thoom_comms_pid(host);
  const uint64_t guest_pid = thoom_comms_pid(guest);

  report->check(host_pid == THOOM_PID_HOST, "network: the host is peer 0");
  report->check(guest_pid == 1, "network: the guest is peer 1");

  bool host_join = false;
  bool guest_join = false;
  thoom_comms_event event = {};
  while (thoom_comms_next_event(host, &event)) {
    if (event.kind == THOOM_COMMS_EVENT_JOIN && event.peer == guest_pid) {
      host_join = true;
    }
  }
  while (thoom_comms_next_event(guest, &event)) {
    if (event.kind == THOOM_COMMS_EVENT_JOIN && event.peer == THOOM_PID_HOST) {
      guest_join = true;
    }
  }
  report->check(host_join, "network: the host reports the join");
  report->check(guest_join, "network: the guest reports the join");

  network_transfer(host, guest, true, options,
                   "network: reliable host to guest", report);
  network_transfer(guest, host, true, options,
                   "network: reliable guest to host", report);
  network_transfer(host, guest, false, options,
                   "network: unreliable host to guest", report);
  network_transfer(guest, host, false, options,
                   "network: unreliable guest to host", report);

  {
    BenchOptions large = options;
    large.min_bytes = LARGE_BODY_BYTES;
    large.max_bytes = LARGE_BODY_BYTES;

    const std::vector<uint8_t> payload = make_payload(large.seed, 0, large);
    const bool sent = thoom_comms_send(host, true, payload.data(),
                                       static_cast<uint32_t>(payload.size()));

    bool received = false;
    const int64_t deadline = bench_now_ms() + CONNECT_TIMEOUT_MS;

    while (!received && bench_now_ms() < deadline) {
      thoom_comms_poll(host);
      thoom_comms_poll(guest);

      thoom_comms_message message = {};
      while (thoom_comms_receive(guest, &message)) {
        uint32_t index = 0;
        if (match_payload(message.bytes, message.size, large.seed, large,
                          &index)) {
          received = true;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    report->check(sent && received, "network: the largest payload round-trips");
  }

  thoom_comms_destroy(guest);

  const bool closed = poll_comms(
      host, nullptr,
      [host] { return thoom_comms_state(host) == THOOM_COMMS_STATE_CLOSED; },
      LEAVE_TIMEOUT_MS);
  report->check(closed, "network: the host notices the guest leaving");

  bool host_leave = false;
  while (thoom_comms_next_event(host, &event)) {
    if (event.kind == THOOM_COMMS_EVENT_LEAVE && event.peer == guest_pid) {
      host_leave = true;
    }
  }
  report->check(host_leave, "network: the host reports the leave");
  report->check(!thoom_comms_send(host, true, "x", 1),
                "network: a send after close is refused");
  thoom_comms_destroy(host);

  if (!open_pair(&host, &guest, report, "network reverse")) {
    thoom_comms_destroy(guest);
    thoom_comms_destroy(host);
    return;
  }

  thoom_comms_destroy(host);

  const bool guest_closed = poll_comms(
      guest, nullptr,
      [guest] { return thoom_comms_state(guest) == THOOM_COMMS_STATE_CLOSED; },
      LEAVE_TIMEOUT_MS);
  report->check(guest_closed, "network: the guest notices the host leaving");

  bool guest_leave = false;
  while (thoom_comms_next_event(guest, &event)) {
    if (event.kind == THOOM_COMMS_EVENT_LEAVE && event.peer == THOOM_PID_HOST) {
      guest_leave = true;
    }
  }
  report->check(guest_leave, "network: the guest reports the leave");
  thoom_comms_destroy(guest);

  thoom_comms* orphan = thoom_comms_create(THOOM_COMMS_ROLE_GUEST);
  report->check(thoom_comms_state(orphan) == THOOM_COMMS_STATE_WAITING,
                "network: a guest without a code waits");
  report->check(thoom_comms_pid(orphan) == THOOM_COMMS_PEER_NONE,
                "network: a waiting guest has no peer id");
  report->check(!thoom_comms_send(orphan, true, "x", 1),
                "network: a send before ready is refused");
  thoom_comms_destroy(orphan);
}

static void run_network_version(BenchReport* report) {
  thoom::WebRtcConnection host_connection;
  thoom::WebRtcConnection guest_connection;

  const thoom::Session::Options host_options;
  thoom::Session::Options guest_options;
  guest_options.version = thoom::kProtocolVersion + 1;

  thoom::Session host(thoom::Session::kHost, &host_connection, host_options);
  thoom::Session guest(thoom::Session::kGuest, &guest_connection,
                       guest_options);

  report->check(host_connection.start_host(),
                "version: the host transport starts");
  report->check(guest_connection.start_guest(host_connection.code),
                "version: the guest transport starts");

  bool host_rejected = false;
  bool guest_rejected = false;
  const int64_t deadline = bench_now_ms() + CONNECT_TIMEOUT_MS;

  while (bench_now_ms() < deadline && !(host_rejected && guest_rejected)) {
    host_connection.poll();
    guest_connection.poll();
    host.poll();
    guest.poll();

    thoom::Session::Event event = {};
    while (host.next_event(&event)) {
      if (event.kind == thoom::Session::kReject &&
          event.reason == thoom::Session::kReasonVersion) {
        host_rejected = true;
      }
    }
    while (guest.next_event(&event)) {
      if (event.kind == thoom::Session::kReject &&
          event.reason == thoom::Session::kReasonVersion) {
        guest_rejected = true;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  char detail[240];
  std::snprintf(
      detail, sizeof(detail),
      " (code %s, host transport %d, guest transport %d, host session %d, "
      "guest %d)",
      host_connection.code.c_str(), static_cast<int>(host_connection.state()),
      static_cast<int>(guest_connection.state()), static_cast<int>(host.state),
      static_cast<int>(guest.state));

  report->check(host_rejected, "version: the host rejects a remote mismatch" +
                                   std::string(detail));
  report->check(guest_rejected,
                "version: the guest learns the reason" + std::string(detail));
}

int main(int argc, char** argv) {
  absl::InitializeLog();

  BenchOptions options;
  const int parsed = parse_bench_options(argc, argv, &options);
  if (parsed != 0) {
    return parsed == 1 ? 0 : 2;
  }

  print_bench_options("comms bench", options);

  const char* url = std::getenv("THOOM_SIGNALLING_URL");
  std::printf("      signalling %s\n", url != nullptr ? url : "(compiled URL)");

  BenchReport report;
  run_comms_codec(options, &report);
  run_comms_loopback(options, &report);

  if (!options.loopback) {
    run_network(options, &report);
    run_network_version(&report);
  }

  return report.finish("comms bench");
}
