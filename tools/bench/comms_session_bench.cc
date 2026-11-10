#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bench.h"
#include "comms/bytes.h"
#include "comms/messages.h"
#include "comms/protocol.h"
#include "comms/session.h"
#include "comms_bench.h"
#include "connection.h"
#include "loopback.h"
#include "thoom/comms.h"
#include "thoom/thoom.h"

// Scripts the message stack in process: the byte codec and every session state
// machine path, through a loopback connection, with no transport or server.

#define TEST_FLUSH_MS 25

struct Fixture {
  thoom::Session* session = nullptr;
  LoopbackConnection* own = nullptr;
  LoopbackConnection* remote = nullptr;
};

static thoom::Session::Options test_options() {
  thoom::Session::Options options;
  options.handshake_timeout_ms = 10000;
  options.reject_flush_ms = TEST_FLUSH_MS;
  return options;
}

static Fixture make_fixture(thoom::Session::Role role,
                            const thoom::Session::Options& options) {
  Fixture fixture;
  LoopbackConnection::make_pair(&fixture.own, &fixture.remote);
  fixture.session = new thoom::Session(role, fixture.own, options);
  return fixture;
}

static void destroy_fixture(Fixture* fixture) {
  delete fixture->session;
  delete fixture->own;
  delete fixture->remote;
}

static std::vector<uint8_t> hello_payload(uint8_t role) {
  thoom::Hello hello = {};
  hello.role = role;

  thoom::ByteWriter writer;
  thoom::write_hello(&writer, hello);
  return writer.bytes();
}

static std::vector<uint8_t> welcome_payload(uint64_t pid) {
  thoom::Welcome welcome = {};
  welcome.pid = pid;

  thoom::ByteWriter writer;
  thoom::write_welcome(&writer, welcome);
  return writer.bytes();
}

static std::vector<uint8_t> reject_payload(uint8_t reason) {
  thoom::Reject reject = {};
  reject.reason = reason;

  thoom::ByteWriter writer;
  thoom::write_reject(&writer, reject);
  return writer.bytes();
}

static void feed(Fixture* fixture, uint8_t kind,
                 const std::vector<uint8_t>& payload, uint8_t version,
                 uint8_t flags) {
  std::vector<uint8_t> frame;
  thoom::encode_frame(version, kind, flags, payload.data(),
                      static_cast<uint32_t>(payload.size()), &frame);
  fixture->own->inject(frame.data(), static_cast<uint32_t>(frame.size()), true);
}

static void feed_bytes(Fixture* fixture, const std::vector<uint8_t>& bytes) {
  fixture->own->inject(bytes.data(), static_cast<uint32_t>(bytes.size()), true);
}

static bool take_sent(Fixture* fixture, thoom::Frame* frame,
                      std::vector<uint8_t>* bytes) {
  thoom::Message message;
  if (!fixture->remote->receive(&message)) {
    return false;
  }

  *bytes = std::move(message.bytes);
  return thoom::decode_frame(bytes->data(),
                             static_cast<uint32_t>(bytes->size()),
                             frame) == thoom::kDecodeOk;
}

static bool take_sent_reject(Fixture* fixture, uint8_t* reason) {
  thoom::Frame frame = {};
  std::vector<uint8_t> bytes;

  if (!take_sent(fixture, &frame, &bytes) ||
      frame.kind != thoom::kFrameReject) {
    return false;
  }

  thoom::ByteReader reader(frame.payload, frame.size);
  thoom::Reject reject = {};
  if (!thoom::read_reject(&reader, &reject)) {
    return false;
  }

  *reason = reject.reason;
  return true;
}

static bool expect_event(thoom::Session* session,
                         thoom::Session::EventKind kind, uint64_t peer,
                         thoom::Session::Reason reason) {
  thoom::Session::Event event = {};
  if (!session->next_event(&event)) {
    return false;
  }

  return event.kind == kind && event.peer == peer && event.reason == reason;
}

static bool expect_no_event(thoom::Session* session) {
  thoom::Session::Event event = {};
  return !session->next_event(&event);
}

void run_comms_codec(const BenchOptions& options, BenchReport* report) {
  thoom::ByteWriter writer;
  writer.write_u8(0x12);
  writer.write_u16(0x3456);
  writer.write_u32(0x89abcdefu);
  writer.write_u64(0x0123456789abcdefULL);
  writer.write_float(1.5f);
  writer.write_bool(true);
  writer.write_string("cheddar");
  writer.write_blob("feta", 4);

  thoom::ByteReader reader(writer.bytes().data(),
                           static_cast<uint32_t>(writer.bytes().size()));

  bool fixed = reader.read_u8() == 0x12;
  fixed = fixed && reader.read_u16() == 0x3456;
  fixed = fixed && reader.read_u32() == 0x89abcdefu;
  fixed = fixed && reader.read_u64() == 0x0123456789abcdefULL;
  fixed = fixed && reader.read_float() == 1.5f;
  fixed = fixed && reader.read_bool();

  uint32_t blob_size = 0;
  const uint8_t* blob = nullptr;
  fixed = fixed && reader.read_string() == "cheddar";
  blob = reader.read_blob(&blob_size);
  fixed = fixed && blob != nullptr && blob_size == 4 &&
          std::memcmp(blob, "feta", 4) == 0;
  fixed = fixed && reader.ok() && reader.remaining() == 0;
  report->check(fixed, "codec: fixed-width values round-trip little-endian");

  bool truncation = true;
  const uint32_t total = static_cast<uint32_t>(writer.bytes().size());
  for (uint32_t size = 0; size < total; size++) {
    thoom::ByteReader partial(writer.bytes().data(), size);
    partial.read_u8();
    partial.read_u16();
    partial.read_u32();
    partial.read_u64();
    partial.read_float();
    partial.read_bool();
    partial.read_string();

    uint32_t partial_size = 0;
    partial.read_blob(&partial_size);
    truncation = truncation && !partial.ok();
  }
  report->check(truncation, "codec: every truncated buffer is rejected");

  uint64_t state = options.seed + 1;
  thoom::ByteWriter random_writer;
  std::vector<uint64_t> numbers;
  std::vector<float> floats;
  std::vector<std::string> strings;

  for (uint32_t i = 0; i < options.messages; i++) {
    const uint64_t number = bench_random(&state);
    numbers.push_back(number);
    random_writer.write_u64(number);

    const float value =
        static_cast<float>(static_cast<int32_t>(number & 0xffff));
    floats.push_back(value);
    random_writer.write_float(value);

    const uint32_t length = static_cast<uint32_t>(bench_random(&state) % 40);
    std::string text(length, '\0');
    for (uint32_t j = 0; j < length; j++) {
      text[j] = static_cast<char>(bench_random(&state) & 0xff);
    }

    strings.push_back(text);
    random_writer.write_string(text);
  }

  thoom::ByteReader random_reader(
      random_writer.bytes().data(),
      static_cast<uint32_t>(random_writer.bytes().size()));

  bool random = true;
  for (uint32_t i = 0; i < options.messages; i++) {
    random = random && random_reader.read_u64() == numbers[i];
    random = random && random_reader.read_float() == floats[i];
    random = random && random_reader.read_string() == strings[i];
  }

  random = random && random_reader.ok() && random_reader.remaining() == 0;
  report->check(random, "codec: randomized values round-trip");
}

static void loopback_handshake(const BenchOptions& options,
                               BenchReport* report) {
  LoopbackConnection* host_side = nullptr;
  LoopbackConnection* guest_side = nullptr;
  LoopbackConnection::make_pair(&host_side, &guest_side);

  {
    const thoom::Session::Options session_options = test_options();
    thoom::Session host(thoom::Session::kHost, host_side, session_options);
    thoom::Session guest(thoom::Session::kGuest, guest_side, session_options);

    for (int i = 0; i < 8 && (host.state != thoom::Session::kReady ||
                              guest.state != thoom::Session::kReady);
         i++) {
      host.poll();
      guest.poll();
    }

    report->check(host.state == thoom::Session::kReady,
                  "session: the host is ready");
    report->check(guest.state == thoom::Session::kReady,
                  "session: the guest is ready");
    report->check(host.local_pid == THOOM_PID_HOST,
                  "session: the host is peer 0");
    report->check(guest.local_pid == 1, "session: the guest is peer 1");
    report->check(expect_event(&host, thoom::Session::kJoin, 1,
                               thoom::Session::kReasonNone),
                  "session: the host reports the join");
    report->check(expect_event(&guest, thoom::Session::kJoin, THOOM_PID_HOST,
                               thoom::Session::kReasonNone),
                  "session: the guest reports the join");

    const std::vector<uint8_t> payload = make_payload(options.seed, 0, options);

    report->check(
        host.send(true, payload.data(), static_cast<uint32_t>(payload.size())),
        "session: the host sends reliable data");
    report->check(guest.send(false, payload.data(),
                             static_cast<uint32_t>(payload.size())),
                  "session: the guest sends unreliable data");
    host.poll();
    guest.poll();

    thoom::Session::Data guest_data;
    const bool guest_got = guest.next_data(&guest_data);
    report->check(
        guest_got && guest_data.reliable && guest_data.bytes == payload,
        "session: the guest receives reliable data");

    thoom::Session::Data host_data;
    const bool host_got = host.next_data(&host_data);
    report->check(host_got && !host_data.reliable && host_data.bytes == payload,
                  "session: the host receives unreliable data");

    guest_side->close();
    host.poll();

    report->check(host.state == thoom::Session::kClosed,
                  "session: the host closes on transport loss");
    report->check(expect_event(&host, thoom::Session::kLeave, 1,
                               thoom::Session::kReasonTransport),
                  "session: the host reports the leave");
    report->check(
        !host.send(true, payload.data(), static_cast<uint32_t>(payload.size())),
        "session: a send after close is refused");

    thoom::Session::Data ignored;
    report->check(!host.next_data(&ignored), "session: no data after close");
  }

  delete host_side;
  delete guest_side;
}

static void loopback_version(BenchReport* report) {
  {
    Fixture fixture = make_fixture(thoom::Session::kHost, test_options());
    fixture.session->poll();

    report->check(fixture.session->state == thoom::Session::kHandshaking,
                  "version: the host is handshaking");

    feed(&fixture, thoom::kFrameHello, hello_payload(thoom::Session::kGuest),
         thoom::kProtocolVersion + 1, thoom::kFrameFlagsNone);
    fixture.session->poll();

    report->check(fixture.session->state == thoom::Session::kRejected,
                  "version: the host refuses a different version");
    report->check(
        expect_event(fixture.session, thoom::Session::kReject,
                     THOOM_COMMS_PEER_NONE, thoom::Session::kReasonVersion),
        "version: the host reports the version reject");

    uint8_t reason = 0;
    report->check(take_sent_reject(&fixture, &reason),
                  "version: the host sends a reject frame");
    report->check(reason == thoom::Session::kReasonVersion,
                  "version: the reject carries the version reason");

    std::this_thread::sleep_for(std::chrono::milliseconds(TEST_FLUSH_MS + 10));
    fixture.session->poll();
    report->check(fixture.session->state == thoom::Session::kClosed,
                  "version: the host closes after the flush");

    destroy_fixture(&fixture);
  }

  {
    Fixture fixture = make_fixture(thoom::Session::kGuest, test_options());
    fixture.session->poll();

    thoom::Frame frame = {};
    std::vector<uint8_t> bytes;
    report->check(
        take_sent(&fixture, &frame, &bytes) && frame.kind == thoom::kFrameHello,
        "version: the guest sends a hello");

    feed(&fixture, thoom::kFrameReject,
         reject_payload(thoom::Session::kReasonVersion),
         thoom::kProtocolVersion + 1, thoom::kFrameFlagsNone);
    fixture.session->poll();

    report->check(fixture.session->state == thoom::Session::kClosed,
                  "version: the guest closes on a foreign reject");
    report->check(
        expect_event(fixture.session, thoom::Session::kReject,
                     THOOM_COMMS_PEER_NONE, thoom::Session::kReasonVersion),
        "version: the guest reports the version reject");

    destroy_fixture(&fixture);
  }
}

static std::vector<std::vector<uint8_t>> malformed_frames() {
  std::vector<std::vector<uint8_t>> frames;
  std::vector<uint8_t> frame;

  frames.push_back({thoom::kProtocolVersion, thoom::kFrameHello});

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameHello,
                      thoom::kFrameFlagsNone,
                      hello_payload(thoom::Session::kGuest).data(), 1, &frame);
  frame.pop_back();
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameHello,
                      thoom::kFrameFlagsNone,
                      hello_payload(thoom::Session::kGuest).data(), 1, &frame);
  frame.push_back(0x00);
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameHello, 1,
                      hello_payload(thoom::Session::kGuest).data(), 1, &frame);
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, 99, thoom::kFrameFlagsNone,
                      nullptr, 0, &frame);
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameHello,
                      thoom::kFrameFlagsNone, nullptr, 0, &frame);
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameWelcome,
                      thoom::kFrameFlagsNone, welcome_payload(1).data(), 8,
                      &frame);
  frames.push_back(frame);

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameData,
                      thoom::kFrameFlagsNone, "x", 1, &frame);
  frames.push_back(frame);

  frames.push_back({thoom::kProtocolVersion, thoom::kFrameHello,
                    thoom::kFrameFlagsNone, 0x10, 0x00, 0x00, 0x00});

  frames.push_back({thoom::kProtocolVersion, thoom::kFrameHello,
                    thoom::kFrameFlagsNone, 0xff, 0xff, 0xff, 0xff});

  frame.clear();
  thoom::encode_frame(thoom::kProtocolVersion, thoom::kFrameHello,
                      thoom::kFrameFlagsNone, "\x01\x02", 2, &frame);
  frames.push_back(frame);

  return frames;
}

static void loopback_malformed(BenchReport* report) {
  const std::vector<std::vector<uint8_t>> frames = malformed_frames();

  for (size_t i = 0; i < frames.size(); i++) {
    Fixture fixture = make_fixture(thoom::Session::kHost, test_options());
    fixture.session->poll();

    feed_bytes(&fixture, frames[i]);
    fixture.session->poll();

    const std::string name =
        "protocol: malformed frame " + std::to_string(i) + " is refused";
    const bool refused =
        fixture.session->state == thoom::Session::kRejected &&
        expect_event(fixture.session, thoom::Session::kReject,
                     THOOM_COMMS_PEER_NONE, thoom::Session::kReasonProtocol);
    report->check(refused, name);

    uint8_t reason = 0;
    report->check(take_sent_reject(&fixture, &reason) &&
                      reason == thoom::Session::kReasonProtocol,
                  "protocol: malformed frame " + std::to_string(i) +
                      " gets a protocol reject");

    destroy_fixture(&fixture);
  }
}

static void loopback_welcome(BenchReport* report) {
  const uint64_t pids[] = {THOOM_PID_HOST, THOOM_COMMS_PEER_NONE};

  for (const uint64_t pid : pids) {
    Fixture fixture = make_fixture(thoom::Session::kGuest, test_options());
    fixture.session->poll();

    thoom::Frame frame = {};
    std::vector<uint8_t> bytes;
    take_sent(&fixture, &frame, &bytes);

    feed(&fixture, thoom::kFrameWelcome, welcome_payload(pid),
         thoom::kProtocolVersion, thoom::kFrameFlagsNone);
    fixture.session->poll();

    report->check(
        fixture.session->state == thoom::Session::kRejected &&
            expect_event(fixture.session, thoom::Session::kReject,
                         THOOM_COMMS_PEER_NONE,
                         thoom::Session::kReasonProtocol),
        "protocol: the guest refuses the peer id " + std::to_string(pid));

    destroy_fixture(&fixture);
  }
}

static void loopback_guest_hello(BenchReport* report) {
  {
    Fixture fixture = make_fixture(thoom::Session::kGuest, test_options());
    fixture.session->poll();

    thoom::Frame frame = {};
    std::vector<uint8_t> bytes;
    take_sent(&fixture, &frame, &bytes);

    feed(&fixture, thoom::kFrameHello, hello_payload(thoom::Session::kGuest),
         thoom::kProtocolVersion, thoom::kFrameFlagsNone);
    fixture.session->poll();

    report->check(fixture.session->state == thoom::Session::kRejected &&
                      expect_event(fixture.session, thoom::Session::kReject,
                                   THOOM_COMMS_PEER_NONE,
                                   thoom::Session::kReasonProtocol),
                  "protocol: a guest refuses a hello");

    destroy_fixture(&fixture);
  }

  {
    Fixture fixture = make_fixture(thoom::Session::kGuest, test_options());
    fixture.session->poll();

    thoom::Frame frame = {};
    std::vector<uint8_t> bytes;
    take_sent(&fixture, &frame, &bytes);

    feed(&fixture, thoom::kFrameReject, {}, thoom::kProtocolVersion,
         thoom::kFrameFlagsNone);
    fixture.session->poll();

    report->check(fixture.session->state == thoom::Session::kRejected &&
                      expect_event(fixture.session, thoom::Session::kReject,
                                   THOOM_COMMS_PEER_NONE,
                                   thoom::Session::kReasonProtocol),
                  "protocol: an empty reject payload is refused");

    destroy_fixture(&fixture);
  }
}

static void loopback_role(BenchReport* report) {
  Fixture fixture = make_fixture(thoom::Session::kHost, test_options());
  fixture.session->poll();

  feed(&fixture, thoom::kFrameHello, hello_payload(thoom::Session::kHost),
       thoom::kProtocolVersion, thoom::kFrameFlagsNone);
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kRejected &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonRole),
      "protocol: a host hello is refused with the role reason");

  destroy_fixture(&fixture);
}

static void loopback_full(BenchReport* report) {
  thoom::Session::Options options = test_options();
  options.soft_cap = 1;

  Fixture fixture = make_fixture(thoom::Session::kHost, options);
  fixture.session->poll();

  feed(&fixture, thoom::kFrameHello, hello_payload(thoom::Session::kGuest),
       thoom::kProtocolVersion, thoom::kFrameFlagsNone);
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kRejected &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonFull),
      "session: a full session is refused with that reason");

  destroy_fixture(&fixture);
}

static void loopback_timeout(BenchReport* report) {
  thoom::Session::Options options = test_options();
  options.handshake_timeout_ms = 5;
  options.reject_flush_ms = 5;

  Fixture fixture = make_fixture(thoom::Session::kHost, options);
  fixture.session->poll();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kRejected &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonTimeout),
      "session: a silent peer times out");

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  fixture.session->poll();
  report->check(fixture.session->state == thoom::Session::kClosed,
                "session: the timeout closes after the flush");

  destroy_fixture(&fixture);
}

static void loopback_waiting_failure(BenchReport* report) {
  Fixture fixture = make_fixture(thoom::Session::kHost, test_options());
  fixture.remote->close();
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kClosed &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonTransport),
      "session: a transport that fails before ready is reported");

  destroy_fixture(&fixture);
}

static void loopback_transport(BenchReport* report) {
  Fixture fixture = make_fixture(thoom::Session::kHost, test_options());
  fixture.session->poll();

  fixture.own->close();
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kClosed &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonTransport),
      "session: transport loss during the handshake is reported");

  destroy_fixture(&fixture);
}

static void loopback_reject_received(BenchReport* report) {
  Fixture fixture = make_fixture(thoom::Session::kGuest, test_options());
  fixture.session->poll();

  thoom::Frame frame = {};
  std::vector<uint8_t> bytes;
  take_sent(&fixture, &frame, &bytes);

  feed(&fixture, thoom::kFrameReject,
       reject_payload(thoom::Session::kReasonProtocol), thoom::kProtocolVersion,
       thoom::kFrameFlagsNone);
  fixture.session->poll();

  report->check(
      fixture.session->state == thoom::Session::kClosed &&
          expect_event(fixture.session, thoom::Session::kReject,
                       THOOM_COMMS_PEER_NONE, thoom::Session::kReasonProtocol),
      "session: a received reject closes and reports");

  destroy_fixture(&fixture);
}

static void loopback_duplicate(BenchReport* report) {
  LoopbackConnection* host_side = nullptr;
  LoopbackConnection* guest_side = nullptr;
  LoopbackConnection::make_pair(&host_side, &guest_side);

  {
    const thoom::Session::Options session_options = test_options();
    thoom::Session host(thoom::Session::kHost, host_side, session_options);
    thoom::Session guest(thoom::Session::kGuest, guest_side, session_options);

    for (int i = 0; i < 8 && (host.state != thoom::Session::kReady ||
                              guest.state != thoom::Session::kReady);
         i++) {
      host.poll();
      guest.poll();
    }

    report->check(expect_event(&host, thoom::Session::kJoin, 1,
                               thoom::Session::kReasonNone),
                  "protocol: the host is ready before the second hello");
    report->check(expect_event(&guest, thoom::Session::kJoin, THOOM_PID_HOST,
                               thoom::Session::kReasonNone),
                  "protocol: the guest is ready before the second hello");

    std::vector<uint8_t> frame;
    thoom::encode_frame(
        thoom::kProtocolVersion, thoom::kFrameHello, thoom::kFrameFlagsNone,
        hello_payload(thoom::Session::kGuest).data(), 1, &frame);
    host_side->inject(frame.data(), static_cast<uint32_t>(frame.size()), true);
    host.poll();

    report->check(
        host.state == thoom::Session::kRejected &&
            expect_event(&host, thoom::Session::kReject, THOOM_COMMS_PEER_NONE,
                         thoom::Session::kReasonProtocol),
        "protocol: a second hello after the handshake is refused");

    guest.poll();
    report->check(guest.state == thoom::Session::kClosed,
                  "protocol: the peer closes after the violation");

    thoom::Session::Event event = {};
    report->check(guest.next_event(&event) &&
                      event.kind == thoom::Session::kLeave &&
                      event.reason == thoom::Session::kReasonProtocol,
                  "protocol: the peer reports the leave");
  }

  delete host_side;
  delete guest_side;
}

static void loopback_data(const BenchOptions& options, BenchReport* report) {
  LoopbackConnection* host_side = nullptr;
  LoopbackConnection* guest_side = nullptr;
  LoopbackConnection::make_pair(&host_side, &guest_side);

  {
    const thoom::Session::Options session_options = test_options();
    thoom::Session host(thoom::Session::kHost, host_side, session_options);
    thoom::Session guest(thoom::Session::kGuest, guest_side, session_options);

    for (int i = 0; i < 8 && (host.state != thoom::Session::kReady ||
                              guest.state != thoom::Session::kReady);
         i++) {
      host.poll();
      guest.poll();
    }

    bool sent = true;
    for (uint32_t i = 0; i < options.messages; i++) {
      const std::vector<uint8_t> payload =
          make_payload(options.seed, i, options);
      const bool reliable = i % 2 == 0;

      sent = sent && host.send(reliable, payload.data(),
                               static_cast<uint32_t>(payload.size()));
      sent = sent && guest.send(reliable, payload.data(),
                                static_cast<uint32_t>(payload.size()));
    }
    report->check(sent, "session: both sides send a mixed stream");

    const std::vector<uint8_t> oversized(thoom::kMaxPayload + 1, 0x5a);
    report->check(!host.send(true, oversized.data(),
                             static_cast<uint32_t>(oversized.size())),
                  "session: an oversized message is refused");

    host.poll();
    guest.poll();

    bool host_ok = true;
    thoom::Session::Data data;
    uint32_t expected = 0;
    while (host.next_data(&data)) {
      uint32_t index = 0;
      host_ok =
          host_ok && match_payload(data.bytes.data(),
                                   static_cast<uint32_t>(data.bytes.size()),
                                   options.seed, options, &index);
      host_ok = host_ok && index == expected;
      host_ok = host_ok && data.reliable == (expected % 2 == 0);
      expected++;
    }
    report->check(host_ok && expected == options.messages,
                  "session: the host receives the stream in order");

    bool guest_ok = true;
    expected = 0;
    while (guest.next_data(&data)) {
      uint32_t index = 0;
      guest_ok =
          guest_ok && match_payload(data.bytes.data(),
                                    static_cast<uint32_t>(data.bytes.size()),
                                    options.seed, options, &index);
      guest_ok = guest_ok && index == expected;
      guest_ok = guest_ok && data.reliable == (expected % 2 == 0);
      expected++;
    }
    report->check(guest_ok && expected == options.messages,
                  "session: the guest receives the stream in order");
  }

  delete host_side;
  delete guest_side;
}

void run_comms_loopback(const BenchOptions& options, BenchReport* report) {
  loopback_handshake(options, report);
  loopback_data(options, report);
  loopback_version(report);
  loopback_malformed(report);
  loopback_welcome(report);
  loopback_guest_hello(report);
  loopback_role(report);
  loopback_full(report);
  loopback_timeout(report);
  loopback_waiting_failure(report);
  loopback_transport(report);
  loopback_reject_received(report);
  loopback_duplicate(report);
}
