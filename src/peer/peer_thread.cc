#include "peer/peer_thread.h"

#include <rtc/rtc.h>

#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>

#include "peer/peer.h"

#define CANDIDATE_MSG "candidate"
#define DESCRIPTION_MSG "description"
#define FAIL_MSG "fail"
#define PING_MSG "ping"

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

PeerThread::PeerThread(bool offerer) {
  this->offerer.store(offerer);
  rtcPreload();

  this->message_thread = new std::thread(&PeerThread::message_loop, this);
}

PeerThread::~PeerThread() {
  {
    std::lock_guard<std::mutex> guard(this->stop_message_loop_mutex);
    this->stop_message_loop = true;
  }

  this->message_thread->join();
  delete this->message_thread;
  this->message_thread = nullptr;
  rtcCleanup();
}

void PeerThread::set_offerer(bool offerer) {
  this->offerer.store(offerer);
  this->ready.store(true);
}

bool PeerThread::should_stop() {
  std::lock_guard<std::mutex> guard(this->stop_message_loop_mutex);
  return this->stop_message_loop;
}

void PeerThread::message_loop() {
  while (!this->should_stop()) {
    if (!this->ready.load() || Peer::instance == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    switch (Peer::instance->get_state()) {
      case Peer::kStateNoConnection:
        this->no_connection();
        break;
      case Peer::kStateWaitingForPeer:
        this->waiting_for_peer();
        break;
      case Peer::kStateConnected:
        this->connected();
        break;
      case Peer::kStateTryReset:
        // disconnect everything and clear connection code
        this->connection.kill();
        this->signaller.disconnect();
        Peer::instance->set_connection_code("");

        Peer::instance->set_state(Peer::kStateNoConnection);
        break;
    }
  }

  this->connection.kill();
  this->signaller.disconnect();
}

void PeerThread::start_over() {
  this->connection.kill();
  this->signaller.disconnect();
  Peer::instance->set_state(Peer::kStateNoConnection);

  // sleep an additional 500ms for good luck
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void PeerThread::no_connection() {
  switch (this->signaller.state) {
    case PeerSignaller::State::NOT_CONNECTED: {
      std::string connection_code;

      if (this->offerer) {
        connection_code = Peer::instance->get_connection_code();
        if (connection_code.empty()) break;
      } else {
        connection_code = make_code();
        Peer::instance->set_connection_code(connection_code);
      }

      this->signaller.connect(connection_code, this->offerer);

    } break;

    case PeerSignaller::State::CONNECTED:
      Peer::instance->set_state(Peer::kStateWaitingForPeer);
      this->got_ping = false;

      break;

    default:
      break;  // PeerSignaller::State::CONNECTING
  }

  // wait half a second before checking on the connection
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void PeerThread::waiting_for_peer() {
  // when the connection is alive, we are no longer waiting for peer
  if (this->connection.state == PeerConnection::State::ALIVE) {
    this->signaller.disconnect();  // no longer needed
    Peer::instance->set_state(Peer::kStateConnected);
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

void PeerThread::connected() {
  // if connection died, start over
  if (this->connection.state != PeerConnection::State::ALIVE) {
    this->start_over();
    return;
  }

  // connection is alive, send messages
  std::string message;
  while (Peer::instance->send_queue.pop(&message)) {
    this->connection.send(message);
  }

  // receive messages
  message = this->connection.receive();
  if (!message.empty()) {
    Peer::instance->recv_queue.push(message);
  }
}

};  // namespace thoom
