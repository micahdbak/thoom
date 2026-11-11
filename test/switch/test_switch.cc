#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "switch/frame.h"
#include "switch/peer_identity.h"
#include "switch/switch.h"
#include "thoom/thoom.h"

namespace {

typedef thoom::Switch TestedSwitch;

constexpr uint8_t kVersion = thoom::kProtocolVersion;

constexpr uint8_t kHelloKind = thoom::kFrameHello;
constexpr uint8_t kWelcomeKind = thoom::kFrameWelcome;
constexpr uint8_t kRejectKind = thoom::kFrameReject;
constexpr uint8_t kByeKind = thoom::kFrameBye;
constexpr uint8_t kDataKind = thoom::kFrameData;

constexpr uint64_t kHostHandle = 10;
constexpr uint64_t kGuestHandle = 20;
constexpr uint64_t kSecondGuestHandle = 21;
constexpr uint64_t kUnknownHandle = 99;

std::vector<uint8_t> frame_bytes(uint8_t kind,
                                 const std::vector<uint8_t>& payload,
                                 uint8_t version = kVersion,
                                 uint8_t flags = 0) {
  std::vector<uint8_t> bytes = {version, kind, flags};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

std::vector<uint8_t> u32_bytes(uint32_t value) {
  std::vector<uint8_t> bytes(4);
  for (int i = 0; i < 4; i++) {
    bytes[i] = static_cast<uint8_t>(value >> (8 * i));
  }
  return bytes;
}

std::vector<uint8_t> hello_payload(thoom_pid pid,
                                   uint8_t role = thoom::kRoleGuest) {
  std::vector<uint8_t> payload = {role};
  std::vector<uint8_t> id = u32_bytes(pid);
  payload.insert(payload.end(), id.begin(), id.end());
  return payload;
}

std::vector<uint8_t> hello_bytes(thoom_pid pid,
                                 uint8_t role = thoom::kRoleGuest) {
  std::vector<uint8_t> bytes = {kVersion, kHelloKind, 0};
  std::vector<uint8_t> payload = hello_payload(pid, role);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

TestedSwitch::Options make_options(uint32_t soft_cap = 2,
                                   uint32_t handshake_timeout_ms = 10000,
                                   uint32_t reject_flush_ms = 0) {
  TestedSwitch::Options options;
  options.soft_cap = soft_cap;
  options.handshake_timeout_ms = handshake_timeout_ms;
  options.reject_flush_ms = reject_flush_ms;
  return options;
}

void expect_consistent(const TestedSwitch& sw) {
  switch (sw.get_state()) {
    case TestedSwitch::kIdle:
      EXPECT_EQ(sw.get_reason(), TestedSwitch::kReasonNone);
      EXPECT_EQ(sw.peer_count(), 0u);
      EXPECT_EQ(sw.get_local_pid(), THOOM_PID_INVALID);
      break;
    case TestedSwitch::kListening:
      EXPECT_EQ(sw.get_local_pid(), THOOM_PID_HOST);
      break;
    case TestedSwitch::kJoining:
      EXPECT_EQ(sw.get_local_pid(), THOOM_PID_INVALID);
      EXPECT_EQ(sw.peer_count(), 0u);
      break;
    case TestedSwitch::kJoined:
      EXPECT_NE(sw.get_local_pid(), THOOM_PID_HOST);
      EXPECT_NE(sw.get_local_pid(), THOOM_PID_INVALID);
      EXPECT_EQ(sw.peer_count(), 1u);
      EXPECT_EQ(sw.peer_pid(0), THOOM_PID_HOST);
      break;
    case TestedSwitch::kClosed:
      EXPECT_NE(sw.get_reason(), TestedSwitch::kReasonNone);
      break;
  }

  std::set<thoom_pid> pids;
  for (uint32_t i = 0; i < sw.peer_count(); i++) {
    const thoom_pid pid = sw.peer_pid(i);
    EXPECT_NE(pid, THOOM_PID_INVALID);
    EXPECT_TRUE(pids.insert(pid).second);
    EXPECT_TRUE(sw.peer_ready(pid));
  }

  EXPECT_EQ(sw.peer_pid(sw.peer_count()), THOOM_PID_INVALID);
}

void expect_event(TestedSwitch* sw, TestedSwitch::Event::Kind kind,
                  thoom_pid peer, TestedSwitch::Reason reason) {
  TestedSwitch::Event event = {};
  ASSERT_TRUE(sw->next_event(&event));
  EXPECT_EQ(event.kind, kind);
  EXPECT_EQ(event.peer, peer);
  EXPECT_EQ(event.reason, reason);
}

void expect_no_events(TestedSwitch* sw) {
  TestedSwitch::Event event = {};
  EXPECT_FALSE(sw->next_event(&event));
}

void expect_no_data(TestedSwitch* sw) {
  thoom::Datagram datagram;
  EXPECT_FALSE(sw->receive(&datagram));
}

class FakeSwitch : public TestedSwitch {
 public:
  struct Sent {
    uint64_t peer;
    std::vector<uint8_t> bytes;
    bool reliable;
  };

  bool open_room(std::string* token) override {
    if (!this->open_room_ok) {
      return false;
    }

    *token = this->room_token;
    return true;
  }

  bool enter_room(const std::string& token) override {
    if (!this->enter_room_ok) {
      return false;
    }

    this->entered_token = token;
    return true;
  }

  void poll_backend() override { this->poll_calls++; }

  bool send_raw(PeerHandle peer, const uint8_t* data, uint32_t size,
                bool reliable) override {
    Sent frame;
    frame.peer = peer;
    frame.bytes.assign(data, data + size);
    frame.reliable = reliable;
    this->sent.push_back(std::move(frame));
    return !this->fail_send;
  }

  void close_peer(PeerHandle peer) override {
    EXPECT_EQ(this->opened.erase(peer), 1u)
        << "close_peer called for a handle that is not open";
    this->closed.push_back(peer);

    if (this->close_peer_reenters) {
      this->on_drop(peer, kReasonTransport);
    }
  }

  void close_room() override {
    EXPECT_EQ(this->close_room_calls, 0) << "close_room called twice";
    this->close_room_calls++;
    this->opened.clear();
  }

  void open(PeerHandle handle) {
    this->opened.insert(handle);
    this->on_open(handle);
  }

  void feed(PeerHandle handle, const std::vector<uint8_t>& bytes,
            bool reliable = true) {
    this->on_datagram(handle, bytes.data(), static_cast<uint32_t>(bytes.size()),
                      reliable);
  }

  void drop(PeerHandle handle, Reason reason) {
    this->opened.erase(handle);
    this->on_drop(handle, reason);
  }

  bool open_room_ok = true;
  bool enter_room_ok = true;
  bool fail_send = false;
  bool close_peer_reenters = false;
  std::string room_token = "ROOM01";
  std::string entered_token;
  int poll_calls = 0;
  int close_room_calls = 0;
  std::vector<Sent> sent;
  std::vector<uint64_t> closed;
  std::set<uint64_t> opened;
};

thoom_pid sent_hello_pid(const FakeSwitch& fake, size_t index = 0) {
  if (index >= fake.sent.size() || fake.sent[index].bytes.size() < 8) {
    return THOOM_PID_INVALID;
  }

  const std::vector<uint8_t>& bytes = fake.sent[index].bytes;
  thoom_pid pid = 0;
  for (int i = 0; i < 4; i++) {
    pid |= static_cast<thoom_pid>(bytes[4 + i]) << (8 * i);
  }

  return pid;
}

void feed_welcome(FakeSwitch* guest, thoom_pid pid) {
  guest->feed(kHostHandle, frame_bytes(kWelcomeKind, u32_bytes(pid)));
}

thoom_pid other_pid(thoom_pid pid) { return pid == 1 ? 2 : 1; }

void expect_frame(const FakeSwitch& fake, size_t index, uint64_t peer,
                  uint8_t kind, const std::vector<uint8_t>& payload,
                  bool reliable) {
  ASSERT_LT(index, fake.sent.size());
  EXPECT_EQ(fake.sent[index].peer, peer);
  EXPECT_EQ(fake.sent[index].reliable, reliable);
  EXPECT_EQ(fake.sent[index].bytes, frame_bytes(kind, payload));
}

void expect_reject(const FakeSwitch& fake, size_t index, uint64_t peer,
                   TestedSwitch::Reason reason) {
  expect_frame(fake, index, peer, kRejectKind, {static_cast<uint8_t>(reason)},
               true);
}

void expect_welcome(const FakeSwitch& fake, size_t index, uint64_t peer,
                    thoom_pid pid) {
  expect_frame(fake, index, peer, kWelcomeKind, u32_bytes(pid), true);
}

class SwitchTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(thoom::PeerIdentity::init()); }

  void TearDown() override { thoom::PeerIdentity::quit(); }

  FakeSwitch host;
  FakeSwitch guest;
};

TEST_F(SwitchTest, HostStartsListening) {
  ASSERT_TRUE(this->host.host(make_options()));

  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonNone);
  EXPECT_EQ(this->host.get_local_pid(), THOOM_PID_HOST);
  EXPECT_STREQ(this->host.get_token(), "ROOM01");
  EXPECT_EQ(this->host.peer_count(), 0u);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, HostOnlyFromIdle) {
  ASSERT_TRUE(this->host.host(make_options()));
  EXPECT_FALSE(this->host.host(make_options()));
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, HostOpenRoomFailure) {
  this->host.open_room_ok = false;

  EXPECT_FALSE(this->host.host(make_options()));
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->host.close_room_calls, 0);

  this->host.poll();
  EXPECT_EQ(this->host.close_room_calls, 0);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, GuestJoins) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonNone);
  EXPECT_EQ(this->guest.get_local_pid(), THOOM_PID_INVALID);
  EXPECT_STREQ(this->guest.get_token(), "ROOM01");
  EXPECT_EQ(this->guest.entered_token, "ROOM01");
  EXPECT_EQ(this->guest.peer_count(), 0u);
  expect_consistent(this->guest);
}

TEST_F(SwitchTest, GuestTokenValidation) {
  EXPECT_FALSE(this->guest.join(make_options(), ""));
  EXPECT_FALSE(this->guest.join(make_options(), nullptr));
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kIdle);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonNone);
  expect_consistent(this->guest);
}

TEST_F(SwitchTest, GuestOnlyFromIdle) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  EXPECT_FALSE(this->guest.join(make_options(), "ROOM02"));
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
  EXPECT_STREQ(this->guest.get_token(), "ROOM01");
}

TEST_F(SwitchTest, GuestEnterRoomFailure) {
  this->guest.enter_room_ok = false;

  EXPECT_FALSE(this->guest.join(make_options(), "ROOM01"));
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->guest.close_room_calls, 0);
  expect_consistent(this->guest);
}

TEST_F(SwitchTest, CloseFromIdle) {
  this->host.close();

  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonClosed);
  EXPECT_EQ(this->host.close_room_calls, 0);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, CloseSendsByeAndClosesRoom) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.sent.clear();

  this->host.close();

  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonClosed);
  EXPECT_EQ(this->host.close_room_calls, 1);
  EXPECT_EQ(this->host.sent.size(), 1u);
  expect_frame(this->host, 0, kGuestHandle, kByeKind, {}, true);
  expect_no_events(&this->host);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, CloseIsIdempotent) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.sent.clear();

  this->host.close();
  this->host.close();

  EXPECT_EQ(this->host.sent.size(), 1u);
  expect_frame(this->host, 0, kGuestHandle, kByeKind, {}, true);
  EXPECT_EQ(this->host.close_room_calls, 1);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
}

TEST_F(SwitchTest, PollAfterCloseDoesNothing) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.close();
  const int polls = this->host.poll_calls;

  this->host.poll();
  this->host.poll();

  EXPECT_EQ(this->host.poll_calls, polls);
  EXPECT_EQ(this->host.close_room_calls, 1);
}

TEST_F(SwitchTest, PollCallsBackendOnce) {
  ASSERT_TRUE(this->host.host(make_options()));

  this->host.poll();
  EXPECT_EQ(this->host.poll_calls, 1);

  this->host.poll();
  EXPECT_EQ(this->host.poll_calls, 2);
}

TEST_F(SwitchTest, HostOpenBeforeListeningClosesHandle) {
  this->host.open(kGuestHandle);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kIdle);
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);

  this->host.close();
  this->host.open(kUnknownHandle);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.closed.size(), 2u);
  EXPECT_EQ(this->host.closed[1], kUnknownHandle);
}

TEST_F(SwitchTest, HostOpenAddsHandshakingPeer) {
  ASSERT_TRUE(this->host.host(make_options()));

  this->host.open(kGuestHandle);

  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_TRUE(this->host.sent.empty());
  expect_no_events(&this->host);

  this->host.poll();
  EXPECT_TRUE(this->host.closed.empty());
  expect_consistent(this->host);
}

TEST_F(SwitchTest, HostWelcomesGuest) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(kGuestHandle, hello_bytes(1));

  expect_welcome(this->host, 0, kGuestHandle, 1);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->host.peer_count(), 1u);
  EXPECT_EQ(this->host.peer_pid(0), 1);
  EXPECT_TRUE(this->host.peer_ready(1));
  expect_consistent(this->host);
}

TEST_F(SwitchTest, HostKeepsReservationAfterLeave) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.drop(kGuestHandle, TestedSwitch::kReasonTransport);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonTransport);

  // The pid stays reserved for the life of the host identity state, so the
  // next peer must propose a different one.
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(1));
  expect_reject(this->host, 1, kSecondGuestHandle, TestedSwitch::kReasonPid);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_TRUE(this->host.sent[1].bytes.size() > 0);

  // The slot is still handshaking, so it can take a new proposal on the same
  // link without another open.
  this->host.feed(kSecondGuestHandle, hello_bytes(2));
  expect_welcome(this->host, 2, kSecondGuestHandle, 2);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);
  EXPECT_TRUE(this->host.peer_ready(2));
}

TEST_F(SwitchTest, HostRejectsVersionMismatch) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(kGuestHandle,
                  frame_bytes(kHelloKind, {thoom::kRoleGuest}, kVersion + 1));

  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonVersion);
  expect_no_events(&this->host);
}

TEST_F(SwitchTest, HostRejectsMalformedFrames) {
  std::vector<uint8_t> oversized(
      thoom::kFrameHeaderSize + thoom::kMaxPayload + 1, 0);
  oversized[0] = kVersion;
  oversized[1] = kHelloKind;

  struct FrameCase {
    const char* name;
    std::vector<uint8_t> bytes;
  };

  const std::vector<FrameCase> cases = {
      {"short header", {kVersion}},
      {"bad flags", frame_bytes(kHelloKind, {thoom::kRoleGuest}, kVersion, 1)},
      {"unknown kind", frame_bytes(99, {})},
      {"empty hello", frame_bytes(kHelloKind, {})},
      {"hello trailing byte",
       frame_bytes(kHelloKind, {thoom::kRoleGuest, 1, 0, 0, 0, 0})},
      {"unexpected welcome", frame_bytes(kWelcomeKind, u32_bytes(1))},
      {"unexpected reject", frame_bytes(kRejectKind, {1})},
      {"data before ready", frame_bytes(kDataKind, {1})},
      {"oversized payload", oversized},
  };

  for (const FrameCase& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    FakeSwitch host;
    ASSERT_TRUE(host.host(make_options()));
    host.open(kGuestHandle);

    host.feed(kGuestHandle, test_case.bytes);

    expect_reject(host, 0, kGuestHandle, TestedSwitch::kReasonProtocol);
    expect_no_events(&host);
    EXPECT_EQ(host.get_state(), TestedSwitch::kListening);
    EXPECT_EQ(host.peer_count(), 0u);

    host.poll();
    EXPECT_EQ(host.closed.size(), 1u);
    EXPECT_EQ(host.closed[0], kGuestHandle);
  }
}

TEST_F(SwitchTest, HostRejectsFullAtSoftCapOne) {
  ASSERT_TRUE(this->host.host(make_options(1)));
  this->host.open(kGuestHandle);

  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonFull);
  expect_no_events(&this->host);
  EXPECT_EQ(this->host.peer_count(), 0u);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, HostRejectsSecondGuestWhenFull) {
  ASSERT_TRUE(this->host.host(make_options(2)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.open(kSecondGuestHandle);

  expect_reject(this->host, 1, kSecondGuestHandle, TestedSwitch::kReasonFull);
  EXPECT_EQ(this->host.peer_count(), 1u);
  EXPECT_TRUE(this->host.peer_ready(1));

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kSecondGuestHandle);
  EXPECT_TRUE(this->host.peer_ready(1));
}

TEST_F(SwitchTest, HostSecondHelloAfterReadyRefused) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, hello_bytes(1));

  expect_reject(this->host, 1, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonProtocol);
  EXPECT_EQ(this->host.peer_count(), 0u);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, HostAbandonsPeerWhenWelcomeCannotSend) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.fail_send = true;

  this->host.feed(kGuestHandle, hello_bytes(1));

  expect_no_events(&this->host);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
}

TEST_F(SwitchTest, HostKeepsPidAfterFailedWelcome) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.fail_send = true;
  this->host.feed(kGuestHandle, hello_bytes(1));
  this->host.poll();
  ASSERT_EQ(this->host.closed.size(), 1u);

  // The welcome never arrived, so the guest never learned the pid, but the id
  // was consumed and must stay reserved.
  this->host.fail_send = false;
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(1));

  // Index zero is the welcome that failed to send to the first handle.
  expect_reject(this->host, 1, kSecondGuestHandle, TestedSwitch::kReasonPid);
}

TEST_F(SwitchTest, HostHandshakeTimeout) {
  ASSERT_TRUE(this->host.host(make_options(2, 0)));
  this->host.open(kGuestHandle);

  this->host.poll();

  expect_no_events(&this->host);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
  EXPECT_EQ(this->host.peer_count(), 0u);
}

TEST_F(SwitchTest, HostRejectFlushDelaysClose) {
  ASSERT_TRUE(this->host.host(make_options(2, 10000, 200)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1, thoom::kRoleHost));

  this->host.poll();
  EXPECT_TRUE(this->host.closed.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  this->host.poll();

  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
}

TEST_F(SwitchTest, HostByeDuringHandshake) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(kGuestHandle, frame_bytes(kByeKind, {}));

  expect_no_events(&this->host);
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, HostIgnoresUnknownHandles) {
  ASSERT_TRUE(this->host.host(make_options()));

  this->host.feed(kUnknownHandle, hello_bytes(1));
  this->host.drop(kUnknownHandle, TestedSwitch::kReasonTransport);

  EXPECT_TRUE(this->host.sent.empty());
  expect_no_events(&this->host);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, GuestOpenSendsHello) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  ASSERT_EQ(this->guest.sent.size(), 1u);
  EXPECT_EQ(this->guest.sent[0].peer, kHostHandle);
  EXPECT_EQ(this->guest.sent[0].bytes[0], kVersion);
  EXPECT_EQ(this->guest.sent[0].bytes[1], kHelloKind);
  EXPECT_EQ(this->guest.sent[0].bytes[3], thoom::kRoleGuest);
  EXPECT_NE(sent_hello_pid(this->guest), THOOM_PID_INVALID);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
  EXPECT_EQ(this->guest.peer_count(), 0u);
}

TEST_F(SwitchTest, GuestSecondOpenClosesExtra) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.open(kUnknownHandle);

  EXPECT_EQ(this->guest.closed.size(), 1u);
  EXPECT_EQ(this->guest.closed[0], kUnknownHandle);
  EXPECT_EQ(this->guest.sent.size(), 1u);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
}

TEST_F(SwitchTest, GuestWelcomeJoins) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  feed_welcome(&this->guest, sent_hello_pid(this->guest));

  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoined);
  EXPECT_EQ(this->guest.get_local_pid(), sent_hello_pid(this->guest));
  EXPECT_EQ(this->guest.peer_count(), 1u);
  EXPECT_EQ(this->guest.peer_pid(0), THOOM_PID_HOST);
  EXPECT_TRUE(this->guest.peer_ready(THOOM_PID_HOST));
  expect_consistent(this->guest);
}

TEST_F(SwitchTest, GuestRejectsInvalidWelcomePid) {
  const thoom_pid pids[] = {THOOM_PID_HOST, THOOM_PID_INVALID};

  for (const thoom_pid pid : pids) {
    SCOPED_TRACE(pid);
    FakeSwitch guest;
    ASSERT_TRUE(guest.join(make_options(), "ROOM01"));
    guest.open(kHostHandle);

    guest.feed(kHostHandle, frame_bytes(kWelcomeKind, u32_bytes(pid)));

    EXPECT_EQ(guest.get_state(), TestedSwitch::kClosed);
    EXPECT_EQ(guest.get_reason(), TestedSwitch::kReasonProtocol);
    expect_no_events(&guest);

    guest.poll();
    EXPECT_EQ(guest.close_room_calls, 1);
    EXPECT_TRUE(guest.closed.empty());
  }
}

TEST_F(SwitchTest, GuestKeepsRejectReasons) {
  for (uint8_t code = static_cast<uint8_t>(TestedSwitch::kReasonVersion);
       code <= static_cast<uint8_t>(TestedSwitch::kReasonTransport); code++) {
    if (code == static_cast<uint8_t>(TestedSwitch::kReasonPid)) {
      continue;  // a pid collision is retried, not terminal
    }

    SCOPED_TRACE(static_cast<int>(code));
    FakeSwitch guest;
    ASSERT_TRUE(guest.join(make_options(), "ROOM01"));
    guest.open(kHostHandle);

    guest.feed(kHostHandle, frame_bytes(kRejectKind, {code}));

    EXPECT_EQ(guest.get_state(), TestedSwitch::kClosed);
    EXPECT_EQ(guest.get_reason(), static_cast<TestedSwitch::Reason>(code));
    expect_no_events(&guest);
    EXPECT_TRUE(guest.closed.empty());

    guest.poll();
    EXPECT_EQ(guest.close_room_calls, 1);
  }
}

TEST_F(SwitchTest, GuestMapsUnknownRejectReasonsToProtocol) {
  const uint8_t codes[] = {0, 8, 255};

  for (const uint8_t code : codes) {
    SCOPED_TRACE(static_cast<int>(code));
    FakeSwitch guest;
    ASSERT_TRUE(guest.join(make_options(), "ROOM01"));
    guest.open(kHostHandle);

    guest.feed(kHostHandle, frame_bytes(kRejectKind, {code}));

    EXPECT_EQ(guest.get_state(), TestedSwitch::kClosed);
    EXPECT_EQ(guest.get_reason(), TestedSwitch::kReasonProtocol);
  }
}

TEST_F(SwitchTest, GuestParsesForeignVersionReject) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.feed(
      kHostHandle,
      frame_bytes(kRejectKind, {TestedSwitch::kReasonVersion}, kVersion + 1));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonVersion);
}

TEST_F(SwitchTest, GuestRejectsMalformedFrames) {
  std::vector<uint8_t> oversized(
      thoom::kFrameHeaderSize + thoom::kMaxPayload + 1, 0);
  oversized[0] = kVersion;
  oversized[1] = kWelcomeKind;

  struct FrameCase {
    const char* name;
    std::vector<uint8_t> bytes;
  };

  const std::vector<FrameCase> cases = {
      {"short header", {kVersion}},
      {"bad flags", frame_bytes(kWelcomeKind, u32_bytes(1), kVersion, 1)},
      {"unknown kind", frame_bytes(99, {})},
      {"unexpected hello", frame_bytes(kHelloKind, hello_payload(1))},
      {"data before welcome", frame_bytes(kDataKind, {1})},
      {"welcome trailing byte",
       frame_bytes(kWelcomeKind, {1, 0, 0, 0, 0, 0, 0, 0, 0})},
      {"empty reject", frame_bytes(kRejectKind, {})},
      {"oversized payload", oversized},
  };

  for (const FrameCase& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    FakeSwitch guest;
    ASSERT_TRUE(guest.join(make_options(), "ROOM01"));
    guest.open(kHostHandle);

    guest.feed(kHostHandle, test_case.bytes);

    EXPECT_EQ(guest.get_state(), TestedSwitch::kClosed);
    EXPECT_EQ(guest.get_reason(), TestedSwitch::kReasonProtocol);
    expect_no_events(&guest);

    guest.poll();
    EXPECT_EQ(guest.close_room_calls, 1);
    EXPECT_TRUE(guest.closed.empty());
  }
}

TEST_F(SwitchTest, GuestVersionMismatch) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.feed(kHostHandle,
                   frame_bytes(kHelloKind, {thoom::kRoleGuest}, kVersion + 1));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonVersion);
}

TEST_F(SwitchTest, GuestHandshakeTimeout) {
  ASSERT_TRUE(this->guest.join(make_options(2, 0), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.poll();

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonTimeout);
  EXPECT_EQ(this->guest.closed.size(), 1u);
  EXPECT_EQ(this->guest.closed[0], kHostHandle);
  EXPECT_EQ(this->guest.close_room_calls, 1);
  expect_no_events(&this->guest);
}

TEST_F(SwitchTest, GuestHelloSendFailure) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.fail_send = true;

  this->guest.open(kHostHandle);

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonTransport);

  this->guest.poll();
  EXPECT_EQ(this->guest.close_room_calls, 1);
  EXPECT_TRUE(this->guest.closed.empty());
}

TEST_F(SwitchTest, GuestDuplicateWelcome) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.feed(kHostHandle, frame_bytes(kWelcomeKind, u32_bytes(6)));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);
}

TEST_F(SwitchTest, GuestHostDropDuringHandshake) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.drop(kHostHandle, TestedSwitch::kReasonTransport);

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonTransport);
  expect_no_events(&this->guest);
}

TEST_F(SwitchTest, GuestHostDropAfterJoin) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.drop(kHostHandle, TestedSwitch::kReasonTransport);

  expect_event(&this->guest, TestedSwitch::Event::kLeave, THOOM_PID_HOST,
               TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->guest.peer_count(), 0u);
}

TEST_F(SwitchTest, GuestHostByeAfterJoin) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.feed(kHostHandle, frame_bytes(kByeKind, {}));

  expect_event(&this->guest, TestedSwitch::Event::kLeave, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonClosed);
  EXPECT_EQ(this->guest.closed.size(), 1u);
  EXPECT_EQ(this->guest.closed[0], kHostHandle);

  this->guest.poll();
  EXPECT_EQ(this->guest.close_room_calls, 1);
}

TEST_F(SwitchTest, HostSendsData) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.sent.clear();

  thoom::Datagram reliable;
  reliable.peer = 1;
  reliable.bytes = {1, 2, 3};
  reliable.reliable = true;
  ASSERT_TRUE(this->host.send(reliable));

  thoom::Datagram unreliable;
  unreliable.peer = 1;
  unreliable.bytes = {};
  unreliable.reliable = false;
  ASSERT_TRUE(this->host.send(unreliable));

  expect_frame(this->host, 0, kGuestHandle, kDataKind, {1, 2, 3}, true);
  expect_frame(this->host, 1, kGuestHandle, kDataKind, {}, false);
}

TEST_F(SwitchTest, GuestSendsData) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);
  this->guest.sent.clear();

  thoom::Datagram datagram;
  datagram.peer = THOOM_PID_HOST;
  datagram.bytes = {9};
  datagram.reliable = false;
  ASSERT_TRUE(this->guest.send(datagram));

  expect_frame(this->guest, 0, kHostHandle, kDataKind, {9}, false);
}

TEST_F(SwitchTest, SendGuards) {
  ASSERT_TRUE(this->host.host(make_options()));
  thoom::Datagram datagram;
  datagram.peer = 1;
  datagram.bytes = {1};
  datagram.reliable = true;

  EXPECT_FALSE(this->host.send(datagram));

  this->host.open(kGuestHandle);
  EXPECT_FALSE(this->host.send(datagram));

  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  datagram.peer = kUnknownHandle;
  EXPECT_FALSE(this->host.send(datagram));

  datagram.peer = THOOM_PID_HOST;
  EXPECT_FALSE(this->host.send(datagram));

  datagram.peer = 1;
  this->host.fail_send = true;
  EXPECT_FALSE(this->host.send(datagram));

  this->host.close();
  EXPECT_FALSE(this->host.send(datagram));

  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  datagram.peer = THOOM_PID_HOST;
  EXPECT_FALSE(this->guest.send(datagram));
}

TEST_F(SwitchTest, HostReceivesData) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, frame_bytes(kDataKind, {1, 2, 3}), false);

  thoom::Datagram datagram;
  ASSERT_TRUE(this->host.receive(&datagram));
  EXPECT_EQ(datagram.peer, 1);
  EXPECT_EQ(datagram.bytes, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_FALSE(datagram.reliable);

  this->host.feed(kGuestHandle, frame_bytes(kDataKind, {}), true);
  ASSERT_TRUE(this->host.receive(&datagram));
  EXPECT_EQ(datagram.peer, 1);
  EXPECT_TRUE(datagram.bytes.empty());
  EXPECT_TRUE(datagram.reliable);

  const std::vector<uint8_t> largest(thoom::kMaxPayload, 0x5a);
  this->host.feed(kGuestHandle, frame_bytes(kDataKind, largest), true);
  ASSERT_TRUE(this->host.receive(&datagram));
  EXPECT_EQ(datagram.peer, 1);
  EXPECT_EQ(datagram.bytes.size(), thoom::kMaxPayload);
  EXPECT_EQ(datagram.bytes.front(), 0x5a);
  EXPECT_EQ(datagram.bytes.back(), 0x5a);

  expect_no_data(&this->host);
}

TEST_F(SwitchTest, GuestReceivesData) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.feed(kHostHandle, frame_bytes(kDataKind, {7}));

  thoom::Datagram datagram;
  ASSERT_TRUE(this->guest.receive(&datagram));
  EXPECT_EQ(datagram.peer, THOOM_PID_HOST);
  EXPECT_EQ(datagram.bytes, std::vector<uint8_t>({7}));
  EXPECT_TRUE(datagram.reliable);
}

TEST_F(SwitchTest, HostDataOrderAcrossGuests) {
  ASSERT_TRUE(this->host.host(make_options(3)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(2));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, frame_bytes(kDataKind, {1}));
  this->host.feed(kSecondGuestHandle, frame_bytes(kDataKind, {2}));
  this->host.feed(kGuestHandle, frame_bytes(kDataKind, {3}));

  const thoom_pid peers[] = {1, 2, 1};
  const uint8_t bytes[] = {1, 2, 3};
  for (int i = 0; i < 3; i++) {
    thoom::Datagram datagram;
    ASSERT_TRUE(this->host.receive(&datagram));
    EXPECT_EQ(datagram.peer, peers[i]);
    ASSERT_EQ(datagram.bytes.size(), 1u);
    EXPECT_EQ(datagram.bytes[0], bytes[i]);
  }

  expect_no_data(&this->host);
}

TEST_F(SwitchTest, HostPeerIteration) {
  ASSERT_TRUE(this->host.host(make_options(3)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(2));

  EXPECT_EQ(this->host.peer_count(), 2u);
  EXPECT_EQ(this->host.peer_pid(0), 1);
  EXPECT_EQ(this->host.peer_pid(1), 2);
  EXPECT_EQ(this->host.peer_pid(2), THOOM_PID_INVALID);
  EXPECT_TRUE(this->host.peer_ready(1));
  EXPECT_TRUE(this->host.peer_ready(2));
  EXPECT_FALSE(this->host.peer_ready(3));
  expect_consistent(this->host);
}

TEST_F(SwitchTest, GuestPeerIteration) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));

  EXPECT_EQ(this->guest.peer_count(), 1u);
  EXPECT_EQ(this->guest.peer_pid(0), THOOM_PID_HOST);
  EXPECT_EQ(this->guest.peer_pid(1), THOOM_PID_INVALID);
  EXPECT_TRUE(this->guest.peer_ready(THOOM_PID_HOST));
  EXPECT_FALSE(this->guest.peer_ready(1));
}

TEST_F(SwitchTest, EmptyQueues) {
  thoom::Datagram datagram;
  TestedSwitch::Event event = {};

  EXPECT_FALSE(this->host.receive(&datagram));
  EXPECT_FALSE(this->host.next_event(&event));
  EXPECT_EQ(this->host.peer_pid(0), THOOM_PID_INVALID);
  EXPECT_STREQ(this->host.get_token(), "");
  expect_consistent(this->host);
}

TEST_F(SwitchTest, HostDropReadyGuest) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.drop(kGuestHandle, TestedSwitch::kReasonTransport);

  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  EXPECT_TRUE(this->host.closed.empty());
}

TEST_F(SwitchTest, HostByeReadyGuest) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, frame_bytes(kByeKind, {}));

  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, CloseTwoGuestsSendsTwoByes) {
  ASSERT_TRUE(this->host.host(make_options(3)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(2));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);
  this->host.sent.clear();

  this->host.close();

  EXPECT_EQ(this->host.sent.size(), 2u);
  expect_frame(this->host, 0, kGuestHandle, kByeKind, {}, true);
  expect_frame(this->host, 1, kSecondGuestHandle, kByeKind, {}, true);
  EXPECT_EQ(this->host.close_room_calls, 1);
}

TEST_F(SwitchTest, GuestCloseSendsBye) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);
  this->guest.sent.clear();

  this->guest.close();

  EXPECT_EQ(this->guest.sent.size(), 1u);
  expect_frame(this->guest, 0, kHostHandle, kByeKind, {}, true);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonClosed);
  EXPECT_EQ(this->guest.close_room_calls, 1);
}

TEST_F(SwitchTest, HostDuplicateOpenIgnored) {
  ASSERT_TRUE(this->host.host(make_options(2, 0)));
  this->host.open(kGuestHandle);
  this->host.open(kGuestHandle);

  this->host.poll();

  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
}

TEST_F(SwitchTest, GuestDuplicateOpenKeepsHost) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  this->guest.open(kHostHandle);

  EXPECT_TRUE(this->guest.closed.empty());
  EXPECT_EQ(this->guest.sent.size(), 1u);

  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoined);
}

TEST_F(SwitchTest, GuestByeDuringHandshake) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.feed(kHostHandle, frame_bytes(kByeKind, {}));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonClosed);
  EXPECT_EQ(this->guest.closed.size(), 1u);
  EXPECT_EQ(this->guest.closed[0], kHostHandle);
  expect_no_events(&this->guest);
}

TEST_F(SwitchTest, GuestRejectAfterJoin) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.feed(kHostHandle,
                   frame_bytes(kRejectKind, {TestedSwitch::kReasonFull}));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonFull);
}

TEST_F(SwitchTest, HostTableFullClosesExtraHandle) {
  ASSERT_TRUE(this->host.host(make_options(thoom::kSwitchMaxPeers + 1)));

  for (uint64_t handle = 100; handle < 100 + thoom::kSwitchMaxPeers; handle++) {
    this->host.open(handle);
  }
  this->host.open(kUnknownHandle);

  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kUnknownHandle);
  EXPECT_EQ(this->host.peer_count(), 0u);

  this->host.close();
  EXPECT_EQ(this->host.close_room_calls, 1);
}

TEST_F(SwitchTest, HostKeepsPidOrderAfterMiddleLeave) {
  ASSERT_TRUE(this->host.host(make_options(4)));
  const uint64_t handles[] = {200, 201, 202};
  for (uint64_t i = 0; i < 3; i++) {
    this->host.open(handles[i]);
    this->host.feed(handles[i], hello_bytes(static_cast<thoom_pid>(i + 1)));
  }
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 3,
               TestedSwitch::kReasonNone);

  this->host.drop(201, TestedSwitch::kReasonTransport);

  expect_event(&this->host, TestedSwitch::Event::kLeave, 2,
               TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->host.peer_count(), 2u);
  EXPECT_EQ(this->host.peer_pid(0), 1);
  EXPECT_EQ(this->host.peer_pid(1), 3);
  EXPECT_EQ(this->host.peer_pid(2), THOOM_PID_INVALID);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, HostDropRefusedPeerBeforeFlush) {
  ASSERT_TRUE(this->host.host(make_options(2, 10000, 200)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1, thoom::kRoleHost));
  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonRole);

  this->host.drop(kGuestHandle, TestedSwitch::kReasonTransport);

  expect_no_events(&this->host);
  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_EQ(this->host.peer_count(), 0u);

  this->host.poll();
  EXPECT_TRUE(this->host.closed.empty());
}

TEST_F(SwitchTest, SendRefusesOversizedPayload) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.sent.clear();

  thoom::Datagram datagram;
  datagram.peer = 1;
  datagram.bytes.assign(thoom::kMaxPayload + 1, 0);
  datagram.reliable = true;

  EXPECT_FALSE(this->host.send(datagram));
  EXPECT_TRUE(this->host.sent.empty());
}

TEST_F(SwitchTest, HostExpiresMultipleRefusedPeers) {
  ASSERT_TRUE(this->host.host(make_options(3)));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1, thoom::kRoleHost));
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(1, thoom::kRoleHost));

  this->host.poll();

  EXPECT_EQ(this->host.closed.size(), 2u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
  EXPECT_EQ(this->host.closed[1], kSecondGuestHandle);
  expect_no_events(&this->host);
}

TEST_F(SwitchTest, HostRejectsControlWhileReady) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, frame_bytes(kWelcomeKind, u32_bytes(2)));

  expect_reject(this->host, 1, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonProtocol);
}

TEST_F(SwitchTest, HostSurvivesUnknownKindFloodAfterJoin) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  for (int i = 0; i < 10000; i++) {
    this->host.feed(kGuestHandle, frame_bytes(77, {1, 2, 3}));
  }

  expect_reject(this->host, 1, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonProtocol);
  expect_no_events(&this->host);
  EXPECT_EQ(this->host.peer_count(), 0u);

  for (int i = 0; i < 100; i++) {
    this->host.feed(kGuestHandle, frame_bytes(kDataKind, {1}));
  }
  expect_no_data(&this->host);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
}

TEST_F(SwitchTest, HostRejectsNonGuestRoles) {
  const uint8_t roles[] = {thoom::kRoleHost, 2, 255};

  for (const uint8_t role : roles) {
    SCOPED_TRACE(static_cast<int>(role));
    FakeSwitch host;
    ASSERT_TRUE(host.host(make_options()));
    host.open(kGuestHandle);

    host.feed(kGuestHandle, hello_bytes(1, role));

    expect_reject(host, 0, kGuestHandle, TestedSwitch::kReasonRole);
    expect_no_events(&host);
    host.poll();
    EXPECT_EQ(host.closed.size(), 1u);
  }
}

TEST_F(SwitchTest, HostBoundsSoftCapStampede) {
  ASSERT_TRUE(this->host.host(make_options(2)));

  for (uint64_t handle = 300; handle < 400; handle++) {
    this->host.open(handle);
  }

  EXPECT_EQ(this->host.sent.size(), 9u);
  EXPECT_EQ(this->host.closed.size(), 90u);
  EXPECT_EQ(this->host.peer_count(), 0u);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 99u);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);

  this->host.close();
  EXPECT_EQ(this->host.close_room_calls, 1);
  EXPECT_EQ(this->host.closed.size(), 99u);
}

TEST_F(SwitchTest, GuestCannotReviveClosedSession) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.feed(kHostHandle, frame_bytes(kDataKind, {1}));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);

  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  this->guest.feed(kHostHandle, frame_bytes(kDataKind, {2}));
  this->guest.feed(kHostHandle, frame_bytes(kByeKind, {}));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);
  EXPECT_EQ(this->guest.get_local_pid(), THOOM_PID_INVALID);
  expect_no_events(&this->guest);
  expect_no_data(&this->guest);

  this->guest.poll();
  EXPECT_EQ(this->guest.close_room_calls, 1);
}

TEST_F(SwitchTest, HostReusesFreedTableSlot) {
  ASSERT_TRUE(this->host.host(make_options(thoom::kSwitchMaxPeers + 1)));

  for (uint64_t handle = 500; handle < 500 + thoom::kSwitchMaxPeers; handle++) {
    this->host.open(handle);
  }

  this->host.drop(505, TestedSwitch::kReasonTransport);
  this->host.open(510);

  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);

  this->host.close();
  EXPECT_EQ(this->host.close_room_calls, 1);
}

TEST_F(SwitchTest, HostChurnsPeersWithoutAccumulation) {
  ASSERT_TRUE(this->host.host(make_options()));

  for (uint64_t i = 0; i < 100; i++) {
    const uint64_t handle = 600 + i;
    this->host.open(handle);
    this->host.feed(handle, hello_bytes(static_cast<thoom_pid>(i + 1)));
    expect_event(&this->host, TestedSwitch::Event::kJoin, i + 1,
                 TestedSwitch::kReasonNone);

    this->host.drop(handle, TestedSwitch::kReasonTransport);
    expect_event(&this->host, TestedSwitch::Event::kLeave, i + 1,
                 TestedSwitch::kReasonTransport);
  }

  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_TRUE(this->host.opened.empty());
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  EXPECT_EQ(this->host.close_room_calls, 0);
  expect_no_events(&this->host);
}

TEST_F(SwitchTest, HostRefusesMaxSizeHello) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(
      kGuestHandle,
      frame_bytes(kHelloKind, std::vector<uint8_t>(thoom::kMaxPayload, 0x41)));

  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_no_events(&this->host);
  expect_no_data(&this->host);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
}

TEST_F(SwitchTest, HostRefusesEmptyDatagram) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(kGuestHandle, {});

  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_no_events(&this->host);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
}

TEST_F(SwitchTest, HostSurvivesTeardownCallbackReentry) {
  ASSERT_TRUE(this->host.host(make_options(3)));
  this->host.close_peer_reenters = true;
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  this->host.open(kSecondGuestHandle);
  this->host.feed(kSecondGuestHandle, hello_bytes(2));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);

  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonProtocol);

  this->host.poll();

  EXPECT_EQ(this->host.closed.size(), 1u);
  EXPECT_EQ(this->host.closed[0], kGuestHandle);
  EXPECT_EQ(this->host.peer_count(), 1u);
  EXPECT_TRUE(this->host.peer_ready(2));
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
  expect_no_events(&this->host);
}

TEST_F(SwitchTest, HostAcceptsReusedHandleAfterDrop) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.drop(kGuestHandle, TestedSwitch::kReasonTransport);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonTransport);

  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(2));

  expect_welcome(this->host, 1, kGuestHandle, 2);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);
  EXPECT_TRUE(this->host.peer_ready(2));
}

TEST_F(SwitchTest, SendToRefusedPeerIsRefused) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kLeave, 1,
               TestedSwitch::kReasonProtocol);

  thoom::Datagram datagram;
  datagram.peer = 1;
  datagram.bytes = {1};
  datagram.reliable = true;
  EXPECT_FALSE(this->host.send(datagram));
  expect_no_data(&this->host);
}

TEST_F(SwitchTest, CloseWithPeersInEveryState) {
  ASSERT_TRUE(this->host.host(make_options(4)));
  this->host.open(800);
  this->host.open(801);
  this->host.feed(801, hello_bytes(1));
  this->host.open(802);
  this->host.feed(802, hello_bytes(1, thoom::kRoleHost));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_reject(this->host, 1, 802, TestedSwitch::kReasonRole);
  this->host.sent.clear();

  this->host.close();

  EXPECT_EQ(this->host.sent.size(), 1u);
  expect_frame(this->host, 0, 801, kByeKind, {}, true);
  EXPECT_EQ(this->host.close_room_calls, 1);
  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
}

TEST_F(SwitchTest, GuestCloseWhileJoiningSendsNoBye) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.close();

  EXPECT_EQ(this->guest.sent.size(), 1u);
  EXPECT_EQ(this->guest.sent[0].bytes[1], kHelloKind);
  EXPECT_EQ(this->guest.close_room_calls, 1);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonClosed);
}

TEST_F(SwitchTest, GuestOpenAfterCloseClosesHandle) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  this->guest.close();

  this->guest.open(kUnknownHandle);

  EXPECT_EQ(this->guest.closed.size(), 1u);
  EXPECT_EQ(this->guest.closed[0], kUnknownHandle);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
}

TEST_F(SwitchTest, GuestRejectsHelloAfterJoin) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  this->guest.feed(kHostHandle, hello_bytes(1));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);
  expect_no_events(&this->guest);
}

TEST_F(SwitchTest, GuestReceivesMaxPayloadData) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);
  feed_welcome(&this->guest, sent_hello_pid(this->guest));
  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);

  const std::vector<uint8_t> payload(thoom::kMaxPayload, 0x5a);
  this->guest.feed(kHostHandle, frame_bytes(kDataKind, payload));

  thoom::Datagram datagram;
  ASSERT_TRUE(this->guest.receive(&datagram));
  EXPECT_EQ(datagram.peer, THOOM_PID_HOST);
  EXPECT_EQ(datagram.bytes.size(), thoom::kMaxPayload);
  EXPECT_EQ(datagram.bytes.front(), 0x5a);
  EXPECT_EQ(datagram.bytes.back(), 0x5a);
  EXPECT_TRUE(datagram.reliable);
}

TEST_F(SwitchTest, GuestRejectsRejectWithTrailingByte) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  this->guest.feed(kHostHandle,
                   frame_bytes(kRejectKind, {TestedSwitch::kReasonVersion, 0}));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);
}

TEST_F(SwitchTest, HostSurvivesDataFloodFromReadyPeer) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);
  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);

  for (uint32_t i = 0; i < 10000; i++) {
    const std::vector<uint8_t> payload = {static_cast<uint8_t>(i & 0xff)};
    this->host.feed(kGuestHandle, frame_bytes(kDataKind, payload), i % 2 == 0);
  }

  for (uint32_t i = 0; i < 10000; i++) {
    thoom::Datagram datagram;
    ASSERT_TRUE(this->host.receive(&datagram));
    EXPECT_EQ(datagram.peer, 1);
    ASSERT_EQ(datagram.bytes.size(), 1u);
    EXPECT_EQ(datagram.bytes[0], static_cast<uint8_t>(i & 0xff));
    EXPECT_EQ(datagram.reliable, i % 2 == 0);
  }

  expect_no_data(&this->host);
}

TEST_F(SwitchTest, HostDropHandshakingGuest) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.drop(kGuestHandle, TestedSwitch::kReasonTransport);

  expect_no_events(&this->host);
  EXPECT_EQ(this->host.peer_count(), 0u);
  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kListening);
}

TEST_F(SwitchTest, HostRejectsEmptyRoomToken) {
  this->host.room_token = "";

  EXPECT_FALSE(this->host.host(make_options()));
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->host.close_room_calls, 0);
}

TEST_F(SwitchTest, HostRejectsForeignVersionReject) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(
      kGuestHandle,
      frame_bytes(kRejectKind, {TestedSwitch::kReasonVersion}, kVersion + 1));

  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonProtocol);
  expect_no_events(&this->host);

  this->host.poll();
  EXPECT_EQ(this->host.closed.size(), 1u);
}

TEST_F(SwitchTest, HostCloseWithNoPeers) {
  ASSERT_TRUE(this->host.host(make_options()));

  this->host.close();

  EXPECT_TRUE(this->host.sent.empty());
  EXPECT_TRUE(this->host.closed.empty());
  EXPECT_EQ(this->host.close_room_calls, 1);
  EXPECT_EQ(this->host.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->host.get_reason(), TestedSwitch::kReasonClosed);
}

TEST_F(SwitchTest, HostReusesMiddleSlotWithoutShifting) {
  ASSERT_TRUE(this->host.host(make_options(4)));
  const uint64_t handles[] = {900, 901, 902};
  for (uint64_t i = 0; i < 3; i++) {
    this->host.open(handles[i]);
    this->host.feed(handles[i], hello_bytes(static_cast<thoom_pid>(i + 1)));
  }
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 2,
               TestedSwitch::kReasonNone);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 3,
               TestedSwitch::kReasonNone);

  this->host.drop(901, TestedSwitch::kReasonTransport);
  expect_event(&this->host, TestedSwitch::Event::kLeave, 2,
               TestedSwitch::kReasonTransport);
  EXPECT_EQ(this->host.peer_count(), 2u);
  EXPECT_EQ(this->host.peer_pid(0), 1);
  EXPECT_EQ(this->host.peer_pid(1), 3);

  this->host.open(903);
  this->host.feed(903, hello_bytes(4));

  expect_event(&this->host, TestedSwitch::Event::kJoin, 4,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->host.peer_count(), 3u);
  EXPECT_EQ(this->host.peer_pid(0), 1);
  EXPECT_EQ(this->host.peer_pid(1), 4);
  EXPECT_EQ(this->host.peer_pid(2), 3);
  expect_consistent(this->host);
}

TEST_F(SwitchTest, GuestRetriesPidCollision) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  const thoom_pid first = sent_hello_pid(this->guest);
  ASSERT_NE(first, THOOM_PID_INVALID);

  this->guest.feed(kHostHandle,
                   frame_bytes(kRejectKind, {TestedSwitch::kReasonPid}));
  ASSERT_EQ(this->guest.sent.size(), 2u);

  const thoom_pid second = sent_hello_pid(this->guest, 1);
  ASSERT_NE(second, THOOM_PID_INVALID);
  EXPECT_NE(second, first);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
  EXPECT_TRUE(this->guest.closed.empty());

  feed_welcome(&this->guest, second);

  expect_event(&this->guest, TestedSwitch::Event::kJoin, THOOM_PID_HOST,
               TestedSwitch::kReasonNone);
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoined);
  EXPECT_EQ(this->guest.get_local_pid(), second);
  expect_consistent(this->guest);
}

TEST_F(SwitchTest, GuestRetriesInvalidProposal) {
  ASSERT_TRUE(this->host.host(make_options()));
  this->host.open(kGuestHandle);

  this->host.feed(kGuestHandle, hello_bytes(THOOM_PID_HOST));
  expect_reject(this->host, 0, kGuestHandle, TestedSwitch::kReasonPid);

  this->host.feed(kGuestHandle, hello_bytes(THOOM_PID_INVALID));
  expect_reject(this->host, 1, kGuestHandle, TestedSwitch::kReasonPid);

  this->host.feed(kGuestHandle, hello_bytes(1));
  expect_welcome(this->host, 2, kGuestHandle, 1);
  expect_event(&this->host, TestedSwitch::Event::kJoin, 1,
               TestedSwitch::kReasonNone);
}

TEST_F(SwitchTest, GuestWelcomeMustMatchProposal) {
  ASSERT_TRUE(this->guest.join(make_options(), "ROOM01"));
  this->guest.open(kHostHandle);

  const thoom_pid proposed = sent_hello_pid(this->guest);
  ASSERT_NE(proposed, THOOM_PID_INVALID);

  feed_welcome(&this->guest, other_pid(proposed));

  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kClosed);
  EXPECT_EQ(this->guest.get_reason(), TestedSwitch::kReasonProtocol);
  EXPECT_EQ(this->guest.get_local_pid(), THOOM_PID_INVALID);
  expect_no_events(&this->guest);
}

TEST_F(SwitchTest, GuestRetriesCollisionWithinDeadline) {
  ASSERT_TRUE(this->guest.join(make_options(2, 10000), "ROOM01"));
  this->guest.open(kHostHandle);
  this->guest.feed(kHostHandle,
                   frame_bytes(kRejectKind, {TestedSwitch::kReasonPid}));

  this->guest.poll();
  EXPECT_EQ(this->guest.get_state(), TestedSwitch::kJoining);
  EXPECT_TRUE(this->guest.closed.empty());
  EXPECT_EQ(this->guest.close_room_calls, 0);
}

TEST_F(SwitchTest, ReservationOutlivesTheSwitch) {
  {
    FakeSwitch host;
    ASSERT_TRUE(host.host(make_options()));
    host.open(kGuestHandle);
    host.feed(kGuestHandle, hello_bytes(1));
    expect_event(&host, TestedSwitch::Event::kJoin, 1,
                 TestedSwitch::kReasonNone);
  }

  FakeSwitch host;
  ASSERT_TRUE(host.host(make_options()));
  host.open(kGuestHandle);
  host.feed(kGuestHandle, hello_bytes(1));
  expect_reject(host, 0, kGuestHandle, TestedSwitch::kReasonPid);

  host.feed(kGuestHandle, hello_bytes(2));
  expect_welcome(host, 1, kGuestHandle, 2);
  expect_event(&host, TestedSwitch::Event::kJoin, 2, TestedSwitch::kReasonNone);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
