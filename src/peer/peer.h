#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "peer/queue.h"
#include "switch/switch.h"

#define MSG_USE_ITEM '0'
#define MSG_TOSS_ITEM '1'
#define MSG_PUSH_HITBOX '2'
#define MSG_FETA_INFO '3'
#define MSG_EAT_CHEESE '4'
#define MSG_IS_DOWN '5'

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

namespace thoom {

class Peer {
 public:
  static constexpr uint64_t kStateNoConnection = 0;
  static constexpr uint64_t kStateWaitingForPeer = 1;
  static constexpr uint64_t kStateConnected = 2;

  inline static Peer* instance = nullptr;

  static bool init(bool host);
  static void quit();

  explicit Peer(bool host);
  ~Peer();

  uint64_t get_state() const;

  std::string get_connection_code();
  void set_connection_code(std::string code);

  MutexQueue<std::string> send_queue;
  MutexQueue<std::string> recv_queue;

 private:
  void run();
  void join_room(const std::string& code);
  void poll_switch();
  void publish_state();
  void store_code(const std::string& code);

  std::atomic<uint64_t> state{kStateNoConnection};
  std::atomic<bool> running{true};
  std::atomic<bool> host{false};
  bool ready = false;

  std::string connection_code;
  std::mutex connection_code_mutex;

  Switch* net = nullptr;
  MutexQueue<std::string> join_commands;
  std::thread worker;
};

};  // namespace thoom
