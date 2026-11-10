#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <queue>
#include <string>
#include <thread>

#include "SDL3/SDL.h"
#include "peer/peer.h"

#define LOCAL_SIGNALLING_URL "ws://localhost:8080/sc/"

namespace {

constexpr int kMessageCount = 2000;
constexpr int kPayloadBytes = 200;
constexpr int kConnectTimeoutMs = 15000;
constexpr int kTransferTimeoutMs = 30000;
constexpr int kLeaveTimeoutMs = 15000;
constexpr int kSmallTimeoutMs = 5000;

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return predicate();
}

std::string make_message(char side, int index) {
  char header[16];
  snprintf(header, sizeof(header), "%c%06d:", side, index);

  std::string message = header;
  message.append(kPayloadBytes, char('a' + (index % 26)));
  return message;
}

void send_all(thoom::Peer* peer, char side) {
  for (int i = 0; i < kMessageCount; i++) {
    peer->send_queue.push(make_message(side, i));
  }
}

void receive_all(thoom::Peer* peer, char side, int* received) {
  for (;;) {
    std::queue<std::string> messages = peer->recv_queue.take_all();

    if (messages.empty()) {
      return;
    }

    while (!messages.empty()) {
      const std::string message = messages.front();
      messages.pop();

      if (message != make_message(side, *received)) {
        ADD_FAILURE() << "message " << *received << " from " << side
                      << " is corrupt: " << message.substr(0, 16);
        return;
      }

      (*received)++;
    }
  }
}

// The newer cases share these three steps: start the room, connect a guest to
// it, and wait for the host to see a guest come or go.
std::string start_room(thoom::Peer* host) {
  host->set_offerer(false);

  if (!wait_until([host] { return !host->get_connection_code().empty(); },
                  kConnectTimeoutMs)) {
    ADD_FAILURE() << "the host never got a connection code. Is a signalling "
                     "server running on localhost:8080?";
    return std::string();
  }

  return host->get_connection_code();
}

bool connect_guest(thoom::Peer* host, thoom::Peer* guest,
                   const std::string& code) {
  guest->set_offerer(true);
  guest->set_connection_code(code);

  return wait_until(
      [host, guest] {
        return host->get_state() == thoom::Peer::kStateConnected &&
               guest->get_state() == thoom::Peer::kStateConnected;
      },
      kConnectTimeoutMs);
}

bool wait_leave(thoom::Peer* host) {
  return wait_until(
      [host] { return host->get_state() != thoom::Peer::kStateConnected; },
      kLeaveTimeoutMs);
}

bool wait_message(thoom::Peer* peer, const std::string& expected) {
  return wait_until(
      [peer, &expected] {
        std::queue<std::string> messages = peer->recv_queue.take_all();
        bool found = false;

        while (!messages.empty()) {
          if (messages.front() == expected) {
            found = true;
          }
          messages.pop();
        }

        return found;
      },
      kSmallTimeoutMs);
}

void exchange(thoom::Peer* host, thoom::Peer* guest, const std::string& text) {
  host->send_queue.push(text);
  EXPECT_TRUE(wait_message(guest, text))
      << "the guest never read \"" << text << "\"";

  guest->send_queue.push(text + " back");
  EXPECT_TRUE(wait_message(host, text + " back"))
      << "the host never read \"" << text << " back\"";
}

}  // namespace

TEST(Peer, TwoInstancesConnectExchangeAndClose) {
  // The compiled URL points at the hosted server, so the test asks for the
  // local one unless the environment already chose a server.
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  EXPECT_EQ(thoom::Peer::instance, nullptr);

  thoom::Peer host(false);
  host.set_offerer(false);

  ASSERT_TRUE(
      wait_until([&host] { return !host.get_connection_code().empty(); },
                 kConnectTimeoutMs))
      << "the host never got a connection code. Is a signalling server "
         "running on localhost:8080?";

  const std::string code = host.get_connection_code();
  EXPECT_EQ(code.size(), 6u);

  {
    thoom::Peer guest(true);
    guest.set_offerer(true);
    guest.set_connection_code(code);

    ASSERT_TRUE(wait_until(
        [&host, &guest] {
          return host.get_state() == thoom::Peer::kStateConnected &&
                 guest.get_state() == thoom::Peer::kStateConnected;
        },
        kConnectTimeoutMs))
        << "the peers never connected. Is a signalling server running on "
           "localhost:8080?";

    send_all(&host, 'H');
    send_all(&guest, 'G');

    int from_host = 0;
    int from_guest = 0;

    ASSERT_TRUE(wait_until(
        [&] {
          receive_all(&guest, 'H', &from_host);
          receive_all(&host, 'G', &from_guest);

          return from_host == kMessageCount && from_guest == kMessageCount;
        },
        kTransferTimeoutMs))
        << "the guest received " << from_host << " of " << kMessageCount
        << ", the host received " << from_guest << " of " << kMessageCount;

    EXPECT_EQ(from_host, kMessageCount);
    EXPECT_EQ(from_guest, kMessageCount);
  }

  EXPECT_TRUE(wait_until(
      [&host] { return host.get_state() != thoom::Peer::kStateConnected; },
      kLeaveTimeoutMs))
      << "the host did not notice the guest leaving";

  EXPECT_EQ(thoom::Peer::instance, nullptr);
}

TEST(Peer, SecondGuestJoinsAfterFirstLeaves) {
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  thoom::Peer host(false);
  host.set_offerer(false);

  ASSERT_TRUE(
      wait_until([&host] { return !host.get_connection_code().empty(); },
                 kConnectTimeoutMs))
      << "the host never got a connection code. Is a signalling server "
         "running on localhost:8080?";

  const std::string code = host.get_connection_code();

  {
    thoom::Peer guest(true);
    guest.set_offerer(true);
    guest.set_connection_code(code);

    ASSERT_TRUE(wait_until(
        [&host, &guest] {
          return host.get_state() == thoom::Peer::kStateConnected &&
                 guest.get_state() == thoom::Peer::kStateConnected;
        },
        kConnectTimeoutMs))
        << "the first guest never connected. Is a signalling server running "
           "on localhost:8080?";
  }

  ASSERT_TRUE(wait_until(
      [&host] { return host.get_state() != thoom::Peer::kStateConnected; },
      kLeaveTimeoutMs))
      << "the host did not notice the first guest leaving";

  thoom::Peer second(true);
  second.set_offerer(true);
  second.set_connection_code(code);

  EXPECT_TRUE(wait_until(
      [&host, &second] {
        return host.get_state() == thoom::Peer::kStateConnected &&
               second.get_state() == thoom::Peer::kStateConnected;
      },
      kConnectTimeoutMs))
      << "the second guest never joined the same code after the first left: "
         "the host is "
      << host.get_state() << " and the guest is " << second.get_state();
}

TEST(Peer, GuestRejoinsAfterReset) {
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  thoom::Peer host(false);
  const std::string code = start_room(&host);
  ASSERT_FALSE(code.empty());

  thoom::Peer guest(true);
  ASSERT_TRUE(connect_guest(&host, &guest, code));
  exchange(&host, &guest, "first");

  // This is how the game drops a session and returns to the code grid.
  guest.set_state(thoom::Peer::kStateTryReset);
  EXPECT_TRUE(wait_until(
      [&guest] { return guest.get_state() == thoom::Peer::kStateNoConnection; },
      kLeaveTimeoutMs));
  EXPECT_TRUE(wait_leave(&host)) << "the host did not notice the reset";

  EXPECT_TRUE(connect_guest(&host, &guest, code))
      << "the same guest could not rejoin the same code after a reset";
  exchange(&host, &guest, "second");
}

TEST(Peer, GuestsJoinInTurn) {
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  thoom::Peer host(false);
  const std::string code = start_room(&host);
  ASSERT_FALSE(code.empty());

  for (int i = 0; i < 3; i++) {
    const std::string turn = "turn-" + std::to_string(i);

    {
      thoom::Peer guest(true);
      ASSERT_TRUE(connect_guest(&host, &guest, code))
          << "guest " << i << " never joined";
      exchange(&host, &guest, turn);
    }

    EXPECT_TRUE(wait_leave(&host))
        << "the host never saw guest " << i << " leave";
  }
}

TEST(Peer, GuestLeavesDuringHandshake) {
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  thoom::Peer host(false);
  const std::string code = start_room(&host);
  ASSERT_FALSE(code.empty());

  // A guest that disappears before its channels open leaves a half-finished
  // link behind, which the host has to clear before it can take another one.
  {
    thoom::Peer early(true);
    early.set_offerer(true);
    early.set_connection_code(code);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  thoom::Peer guest(true);
  EXPECT_TRUE(connect_guest(&host, &guest, code))
      << "the host did not accept a guest after a half-finished join";
  exchange(&host, &guest, "after-early-leave");
}

TEST(Peer, HostLeavesEndsTheGuest) {
  ASSERT_EQ(SDL_setenv_unsafe("THOOM_SIGNALLING_URL", LOCAL_SIGNALLING_URL, 0),
            0);

  thoom::Peer* host = new thoom::Peer(false);
  const std::string code = start_room(host);
  ASSERT_FALSE(code.empty());

  thoom::Peer guest(true);
  ASSERT_TRUE(connect_guest(host, &guest, code));
  exchange(host, &guest, "before-the-host-leaves");

  delete host;

  EXPECT_TRUE(wait_until(
      [&guest] { return guest.get_state() == thoom::Peer::kStateNoConnection; },
      kLeaveTimeoutMs))
      << "the guest did not notice the host leaving";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
