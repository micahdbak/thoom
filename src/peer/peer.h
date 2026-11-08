#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "peer/peer_connection.h"
#include "peer/peer_signaller.h"

namespace thoom {

// shared message codes between cheddar and feta

// feta -> cheddar
#define MSG_USE_ITEM '0'
#define MSG_TOSS_ITEM '1'
#define MSG_PUSH_HITBOX '2'
#define MSG_FETA_INFO '3'
#define MSG_EAT_CHEESE '4'
#define MSG_IS_DOWN '5'

// cheddar -> feta
#define MSG_MAP 'a'
#define MSG_SPRITE 'b'
#define MSG_AUDIO 'c'
#define MSG_SYNC 'd'
#define MSG_ATTACK 'e'
#define MSG_PUSH_ITEM 'f'
#define MSG_PUSH_CHEESE 'g'
#define MSG_FORCE_DANCE 'h'
#define MSG_DID_HIT 'i'
#define MSG_THROW 'j'
#define MSG_MAX_SPEED 'k'
#define MSG_COLLISION 'l'

class Peer {
 public:
  static constexpr uint64_t kStateNoConnection = 0;
  static constexpr uint64_t kStateWaitingForPeer = 1;
  static constexpr uint64_t kStateConnected = 2;
  static constexpr uint64_t kStateTryReset = 3;

  inline static Peer* instance = nullptr;

  static bool init(bool offerer);
  static void quit();

  Peer(bool offerer);
  ~Peer();

  std::string get_connection_code();
  void set_connection_code(std::string code);

  std::string next_message();
  std::string last_message();
  std::queue<std::string> all_messages();
  void send_message(std::string message);

  void set_offerer(bool offerer);

  std::atomic<uint64_t> state{kStateNoConnection};

 private:
  bool should_stop();

  void start_over();
  void no_connection();
  void waiting_for_peer();
  void connected();
  void message_loop();
  void push_message(std::string message);

  std::atomic<bool> offerer{false};
  std::atomic<bool> role_set{false};
  std::thread* message_thread = nullptr;

  bool stop_message_loop = false;
  std::mutex stop_message_loop_mutex;

  std::string connection_code;
  std::mutex connection_code_mutex;

  std::queue<std::string> received_messages;
  std::mutex received_messages_mutex;

  std::queue<std::string> send_messages;
  std::mutex send_messages_mutex;

  // Only used by message_thread.
  PeerConnection connection;
  PeerSignaller signaller;
  bool got_ping = false;
};

};  // namespace thoom
