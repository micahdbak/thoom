#include "peer/peer.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

#include "absl/log/log.h"
#include "switch/switch.h"
#include "thoom/thoom.h"
#include "webrtc/webrtc_switch.h"

#define PEER_POLL_MS 1

namespace thoom {

bool Peer::init(bool host) {
  if (Peer::instance != nullptr) {
    LOG(ERROR) << "Peer::init error: a peer already exists";
    return false;
  }

  Peer* peer = new Peer(host);

  if (!peer->ready) {
    delete peer;
    return false;
  }

  Peer::instance = peer;
  return true;
}

void Peer::quit() {
  if (Peer::instance == nullptr) {
    return;
  }

  delete Peer::instance;
  Peer::instance = nullptr;
}

Peer::Peer(bool host) {
  this->host.store(host);
  this->net = new WebRtcSwitch();
  this->ready = !host;

  if (host) {
    this->ready = this->net->host(Switch::Options());

    if (this->ready) {
      this->store_code(this->net->get_token());
    }
  }

  if (this->ready) {
    this->worker = std::thread(&Peer::run, this);
  }
}

Peer::~Peer() {
  this->running.store(false);

  if (this->worker.joinable()) {
    this->worker.join();
  }

  if (this->net != nullptr) {
    this->net->close();
    delete this->net;
    this->net = nullptr;
  }
}

uint64_t Peer::get_state() const { return this->state.load(); }

std::string Peer::get_connection_code() {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  return this->connection_code;
}

void Peer::set_connection_code(std::string code) {
  this->store_code(code);
  this->join_commands.push(std::move(code));
}

void Peer::run() {
  while (this->running.load()) {
    std::queue<std::string> batch = this->join_commands.take_all();

    while (!batch.empty()) {
      this->join_room(batch.front());
      batch.pop();
    }

    this->poll_switch();

    std::this_thread::sleep_for(std::chrono::milliseconds(PEER_POLL_MS));
  }
}

void Peer::join_room(const std::string& code) {
  if (this->host.load()) {
    LOG(ERROR) << "Peer: a host cannot join a room";
    return;
  }

  // A switch is one shot, so a retry after a closed attempt needs a new one.
  // Closing first guarantees the old attempt no longer owns the acting pid.
  if (this->net->get_state() != Switch::kIdle) {
    Switch* replacement = new WebRtcSwitch();
    this->net->close();
    delete this->net;
    this->net = replacement;
  }

  this->send_queue.take_all();
  this->recv_queue.take_all();

  if (!this->net->join(Switch::Options(), code.c_str())) {
    LOG(ERROR) << "Peer: the room " << code << " was not entered";
  }
}

void Peer::poll_switch() {
  this->net->poll();

  if (this->net->peer_count() > 0) {
    const thoom_pid remote = this->net->peer_pid(0);
    std::string text;

    while (this->send_queue.pop(&text)) {
      Datagram datagram;
      datagram.peer = remote;
      datagram.reliable = true;
      datagram.bytes.assign(text.begin(), text.end());

      if (!this->net->send(datagram)) {
        LOG(WARNING) << "Peer: a queued message was not sent";
      }
    }
  }

  Datagram datagram;
  while (this->net->receive(&datagram)) {
    this->recv_queue.push(
        std::string(datagram.bytes.begin(), datagram.bytes.end()));
  }

  this->publish_state();
}

void Peer::publish_state() {
  if (this->net->peer_count() > 0) {
    this->state.store(kStateConnected);
    return;
  }

  const Switch::State state = this->net->get_state();

  if (state == Switch::kListening || state == Switch::kJoining) {
    this->state.store(kStateWaitingForPeer);
    return;
  }

  this->state.store(kStateNoConnection);
}

void Peer::store_code(const std::string& code) {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  this->connection_code = code;
}

};  // namespace thoom
