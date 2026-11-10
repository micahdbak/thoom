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
#include "connection.h"
#include "webrtc/webrtc_connection.h"
#include "webrtc/webrtc_signaller.h"

// Drives the transport without the message stack: one rendezvous, both data
// channels, reliable and unreliable payloads, the largest payload, a peer
// leaving, and the failure state. A signalling server is required;
// THOOM_SIGNALLING_URL selects it.

#define CONNECT_TIMEOUT_MS 15000
#define TRANSFER_TIMEOUT_MS 30000
#define LEAVE_TIMEOUT_MS 15000
#define SETTLE_MS 500
#define WINDOW_MESSAGES 32
#define LARGE_BODY_BYTES 60000

struct TransferResult {
  uint32_t sent = 0;
  uint32_t received = 0;
  uint64_t bytes = 0;
  std::string failure;
};

static bool poll_transports(thoom::WebRtcConnection* first,
                            thoom::WebRtcConnection* second,
                            const std::function<bool()>& done, int timeout_ms) {
  const int64_t deadline = bench_now_ms() + timeout_ms;

  for (;;) {
    if (first != nullptr) {
      first->poll();
    }
    if (second != nullptr) {
      second->poll();
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

static TransferResult transfer(thoom::WebRtcConnection* from,
                               thoom::WebRtcConnection* to, bool reliable,
                               const BenchOptions& options, int timeout_ms) {
  TransferResult result;
  const int64_t deadline = bench_now_ms() + timeout_ms;
  int64_t last_received_ms = bench_now_ms();
  uint32_t next = 0;

  for (;;) {
    while (result.sent < options.messages &&
           result.sent - result.received < WINDOW_MESSAGES) {
      thoom::Message message;
      message.bytes = make_payload(options.seed, result.sent, options);
      message.reliable = reliable;

      if (!from->send(message)) {
        result.failure = "send refused";
        return result;
      }

      result.sent++;
    }

    from->poll();
    to->poll();

    thoom::Message message;
    while (to->receive(&message)) {
      uint32_t index = 0;
      if (!match_payload(message.bytes.data(),
                         static_cast<uint32_t>(message.bytes.size()),
                         options.seed, options, &index)) {
        result.failure = "corrupt payload";
        return result;
      }

      if (message.reliable != reliable) {
        result.failure = "payload arrived on the wrong channel";
        return result;
      }

      if (reliable && index != next) {
        result.failure = "out of order payload";
        return result;
      }

      next = index + 1;
      result.received++;
      result.bytes += message.bytes.size();
      last_received_ms = bench_now_ms();
    }

    if (result.sent == options.messages) {
      if (reliable && result.received == options.messages) {
        return result;
      }

      if (!reliable && bench_now_ms() - last_received_ms >= SETTLE_MS) {
        return result;
      }
    }

    if (bench_now_ms() >= deadline) {
      result.failure = "timed out";
      return result;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

static void check_transfer(BenchReport* report, const std::string& name,
                           const TransferResult& result, bool reliable,
                           const BenchOptions& options, int64_t elapsed_ms) {
  if (!result.failure.empty()) {
    report->check(false, name + ": " + result.failure);
    return;
  }

  const double seconds = elapsed_ms > 0 ? elapsed_ms / 1000.0 : 0.001;

  char stats[200];
  std::snprintf(stats, sizeof(stats),
                ": %u/%u messages, %" PRIu64 " bytes, %" PRId64
                " ms, %.0f msg/s, %.2f MB/s",
                result.received, options.messages, result.bytes, elapsed_ms,
                result.received / seconds, result.bytes / seconds / 1.0e6);

  const bool complete =
      reliable ? result.received == options.messages : result.received > 0;
  report->check(complete, name + stats);
}

static void run_transport(const BenchOptions& options, BenchReport* report) {
  thoom::WebRtcConnection* host = new thoom::WebRtcConnection();
  report->check(host->start_host(), "transport: the host starts a rendezvous");
  report->check(host->state() == thoom::Connection::kConnecting,
                "transport: the host is connecting");
  report->check(host->code.size() == 6,
                "transport: the host code has six characters");

  thoom::Message probe;
  probe.bytes = {1, 2, 3};
  report->check(!host->send(probe),
                "transport: a send before ready is refused");

  thoom::WebRtcConnection* guest = new thoom::WebRtcConnection();
  report->check(guest->start_guest(host->code),
                "transport: the guest joins a host code");
  report->check(!guest->start_guest(host->code),
                "transport: a started transport cannot be started twice");
  report->check(!guest->start_guest("bad"),
                "transport: the guest refuses a short code");

  const bool connected = poll_transports(
      host, guest,
      [host, guest] {
        return host->state() == thoom::Connection::kReady &&
               guest->state() == thoom::Connection::kReady;
      },
      CONNECT_TIMEOUT_MS);

  if (!report->check(connected, "transport: both endpoints are ready")) {
    std::fprintf(stderr,
                 "transport: no connection. Start the signalling server and "
                 "set THOOM_SIGNALLING_URL, for example "
                 "ws://localhost:8080/sc/.\n");
    delete guest;
    delete host;
    return;
  }

  int64_t start = bench_now_ms();
  TransferResult transfer_result =
      transfer(host, guest, true, options, TRANSFER_TIMEOUT_MS);
  check_transfer(report, "transport: reliable host to guest", transfer_result,
                 true, options, bench_now_ms() - start);

  start = bench_now_ms();
  transfer_result = transfer(guest, host, true, options, TRANSFER_TIMEOUT_MS);
  check_transfer(report, "transport: reliable guest to host", transfer_result,
                 true, options, bench_now_ms() - start);

  start = bench_now_ms();
  transfer_result = transfer(host, guest, false, options, TRANSFER_TIMEOUT_MS);
  check_transfer(report, "transport: unreliable host to guest", transfer_result,
                 false, options, bench_now_ms() - start);

  start = bench_now_ms();
  transfer_result = transfer(guest, host, false, options, TRANSFER_TIMEOUT_MS);
  check_transfer(report, "transport: unreliable guest to host", transfer_result,
                 false, options, bench_now_ms() - start);

  {
    BenchOptions large = options;
    large.min_bytes = LARGE_BODY_BYTES;
    large.max_bytes = LARGE_BODY_BYTES;

    thoom::Message message;
    message.bytes = make_payload(options.seed, 0, large);
    message.reliable = true;

    const bool sent = host->send(message);
    const int64_t deadline = bench_now_ms() + CONNECT_TIMEOUT_MS;
    bool received = false;

    while (!received && bench_now_ms() < deadline) {
      poll_transports(host, guest, [] { return false; }, 1);

      thoom::Message incoming;
      while (guest->receive(&incoming)) {
        uint32_t index = 0;
        if (match_payload(incoming.bytes.data(),
                          static_cast<uint32_t>(incoming.bytes.size()),
                          large.seed, large, &index)) {
          received = true;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    report->check(sent && received,
                  "transport: the largest payload round-trips");
  }

  delete guest;

  const bool left = poll_transports(
      host, nullptr,
      [host] { return host->state() != thoom::Connection::kReady; },
      LEAVE_TIMEOUT_MS);
  report->check(left && host->state() == thoom::Connection::kFailed,
                "transport: the host fails when the guest leaves");

  poll_transports(host, nullptr, [] { return false; }, 500);
  report->check(host->state() == thoom::Connection::kFailed,
                "transport: the failure is final, with no hidden retry");
  delete host;

  thoom::WebRtcConnection* lonely = new thoom::WebRtcConnection();
  report->check(lonely->start_guest(thoom::WebRtcSignaller::make_code()),
                "transport: a guest starts with an unused code");

  poll_transports(lonely, nullptr, [] { return false; }, 1500);
  report->check(lonely->state() == thoom::Connection::kConnecting,
                "transport: an unused code keeps negotiating");
  delete lonely;
}

int main(int argc, char** argv) {
  absl::InitializeLog();

  BenchOptions options;
  const int parsed = parse_bench_options(argc, argv, &options);
  if (parsed != 0) {
    return parsed == 1 ? 0 : 2;
  }

  print_bench_options("webrtc bench", options);

  const char* url = std::getenv("THOOM_SIGNALLING_URL");
  std::printf("      signalling %s\n", url != nullptr ? url : "(compiled URL)");

  BenchReport report;
  run_transport(options, &report);
  return report.finish("webrtc bench");
}
