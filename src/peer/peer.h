#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "peer/message_queue.h"
#include "thoom/thoom.h"

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

namespace thoom {

// forward declared as peer_*.h includes libdatachannel headers
class PeerThread;

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

  void set_offerer(bool offerer);

  thoom_pid get_pid() const;
  void set_pid(thoom_pid pid);

  uint64_t get_state() const;
  void set_state(uint64_t state);

  std::string get_connection_code();
  void set_connection_code(std::string code);

  MessageQueue send_queue;
  MessageQueue recv_queue;

 private:
  std::atomic<thoom_pid> pid{THOOM_PID_HOST};
  std::atomic<uint64_t> state{kStateNoConnection};

  std::string connection_code;
  std::mutex connection_code_mutex;

  PeerThread* thread = nullptr;
};

};  // namespace thoom
