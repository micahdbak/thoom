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
    guest.set_connection_code(code);
    guest.set_offerer(true);

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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
