#include "peer/peer.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <utility>

#include "thoom/macros.h"

#define CANDIDATE_MSG "candidate"
#define DESCRIPTION_MSG "description"
#define FAIL_MSG "fail"
#define PING_MSG "ping"

#define MSG_IS_NTS -1

namespace thoom {

static std::string make_code() {
  static char charset[] = "023456789ABCEFHLPRUXY";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

  std::string ret;
  for (int i = 0; i < 6; i++) {
    char c = charset[dis(gen)];
    ret.push_back(c);
  }

  return ret;
}

bool Peer::init(bool offerer) {
  if (Peer::instance != nullptr) {
    fprintf(stderr, THOOM_LOG "Peer::init error: a peer already exists.\n");
    return false;
  }

  Peer::instance = new Peer(offerer);
  return true;
}

void Peer::quit() {
  if (Peer::instance == nullptr) {
    return;
  }

  delete Peer::instance;
  Peer::instance = nullptr;
}

Peer::Peer(bool offerer) {
  this->offerer.store(offerer);
  rtcPreload();

  this->message_thread = new std::thread(&Peer::message_loop, this);
}

void Peer::set_offerer(bool offerer) {
  this->offerer.store(offerer);
  this->role_set.store(true);
}

Peer::~Peer() {
  {
    std::lock_guard<std::mutex> guard(this->stop_message_loop_mutex);
    this->stop_message_loop = true;
  }

  this->message_thread->join();
  delete this->message_thread;
  this->message_thread = nullptr;
  rtcCleanup();
}

std::string Peer::get_connection_code() {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  return this->connection_code;
}

void Peer::set_connection_code(std::string code) {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  this->connection_code = code;
}

std::string Peer::next_message() {
  std::lock_guard<std::mutex> guard(this->received_messages_mutex);
  if (this->received_messages.empty()) {
    return "";
  }

  std::string message = this->received_messages.front();
  this->received_messages.pop();
  return message;
}

std::string Peer::last_message() {
  std::lock_guard<std::mutex> guard(this->received_messages_mutex);
  if (this->received_messages.empty()) {
    return "";
  }

  std::string message = this->received_messages.back();
  while (!this->received_messages.empty()) this->received_messages.pop();
  return message;
}

std::queue<std::string> Peer::all_messages() {
  std::lock_guard<std::mutex> guard(this->received_messages_mutex);
  if (this->received_messages.empty()) {
    return std::queue<std::string>();
  }

  std::queue<std::string> cpy(this->received_messages);
  while (!this->received_messages.empty()) this->received_messages.pop();
  return cpy;
}

void Peer::send_message(std::string message) {
  std::lock_guard<std::mutex> guard(this->send_messages_mutex);

  // max 10 messages in queue
  if (this->send_messages.size() >= 10) {
    // drop the oldest message and prefer more recent messages
    this->send_messages.pop();
  }

  this->send_messages.push(message);
}

bool Peer::should_stop() {
  std::lock_guard<std::mutex> guard(this->stop_message_loop_mutex);
  return this->stop_message_loop;
}

void Peer::start_over() {
  this->connection.kill();
  this->signaller.disconnect();
  this->state.store(kStateNoConnection);

  // sleep an additional 500ms for good luck
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void Peer::no_connection() {
  switch (this->signaller.state) {
    case PeerSignaller::State::NOT_CONNECTED: {
      std::string connection_code;

      if (this->offerer) {
        connection_code = this->get_connection_code();
        if (connection_code.empty()) break;
      } else {
        connection_code = make_code();
        this->set_connection_code(connection_code);
      }

      this->signaller.connect(connection_code, this->offerer);

    } break;

    case PeerSignaller::State::CONNECTED:
      this->state.store(kStateWaitingForPeer);
      this->got_ping = false;

      break;

    default:
      break;  // PeerSignaller::State::CONNECTING
  }

  // wait half a second before checking on the connection
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void Peer::waiting_for_peer() {
  // when the connection is alive, we are no longer waiting for peer
  if (this->connection.state == PeerConnection::State::ALIVE) {
    this->signaller.disconnect();  // no longer needed
    this->state.store(kStateConnected);
    return;
  }

  // if signaller disconnected or connection error'd
  if (this->signaller.state != PeerSignaller::State::CONNECTED ||
      this->connection.state == PeerConnection::State::ERROR) {
    this->start_over();
    return;
  }

  std::string msg = this->signaller.receive();

  // signaller is connected, but no peer yet
  if (!this->got_ping) {
    this->signaller.send(PING_MSG);

    if (msg == PING_MSG || msg == DESCRIPTION_MSG || msg == CANDIDATE_MSG) {
      this->got_ping = true;
      this->connection.start_collecting(this->offerer);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      return;
    }
  }

  // got_ping is true past here

  if (!msg.empty()) {
    if (msg == DESCRIPTION_MSG) {
      std::string sdp = this->signaller.block_receive();
      std::string type = this->signaller.block_receive();
      if (sdp.empty() || type.empty()) {
        this->start_over();
        return;
      }

      this->connection.set_remote_description(sdp, type);
    } else if (msg == CANDIDATE_MSG) {
      std::string cand = this->signaller.block_receive();
      std::string mid = this->signaller.block_receive();
      if (cand.empty() || mid.empty()) {
        this->start_over();
        return;
      }

      this->connection.add_candidate(cand, mid);
    } else if (msg == FAIL_MSG) {
      this->start_over();  // peer gave up
      return;
    }
  }

  // offer will be generated by the offerer when ready, answer when ready
  std::pair<std::string, std::string> pair =
      this->connection.get_local_description();
  if (!pair.first.empty()) {
    this->signaller.send(DESCRIPTION_MSG);
    this->signaller.send(pair.first);   // sdp
    this->signaller.send(pair.second);  // type
  }

  // candidate
  pair = this->connection.get_candidate();
  if (!pair.first.empty()) {
    this->signaller.send(CANDIDATE_MSG);
    this->signaller.send(pair.first);
    this->signaller.send(pair.second);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void Peer::connected() {
  // if connection died, start over
  if (this->connection.state != PeerConnection::State::ALIVE) {
    this->start_over();
    return;
  }

  // connection is alive, send messages
  {
    std::lock_guard<std::mutex> guard(this->send_messages_mutex);

    while (!this->send_messages.empty()) {
      std::string msg = this->send_messages.front();
      this->send_messages.pop();
      this->connection.send(msg);
    }
  }

  // receive messages
  std::string msg = this->connection.receive();
  if (!msg.empty()) {
    this->push_message(msg);
  }
}

void Peer::message_loop() {
  while (!this->should_stop()) {
    if (!this->role_set.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    switch (this->state.load()) {
      case kStateNoConnection:
        this->no_connection();
        break;
      case kStateWaitingForPeer:
        this->waiting_for_peer();
        break;
      case kStateConnected:
        this->connected();
        break;
      case kStateTryReset:
        // disconnect everything and clear connection code
        this->connection.kill();
        this->signaller.disconnect();
        this->set_connection_code("");

        this->state.store(kStateNoConnection);
        break;
    }
  }

  this->connection.kill();
  this->signaller.disconnect();
}

void Peer::push_message(std::string message) {
  std::lock_guard<std::mutex> guard(this->received_messages_mutex);
  this->received_messages.push(message);
}

};  // namespace thoom
