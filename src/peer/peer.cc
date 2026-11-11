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

bool Peer::init(bool offerer) {
  if (Peer::instance != nullptr) {
    LOG(ERROR) << "Peer::init error: a peer already exists";
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
  this->net = new WebRtcSwitch();
  this->worker = std::thread(&Peer::run, this);
  this->set_offerer(offerer);
}

Peer::~Peer() {
  this->running.store(false);

  if (this->worker.joinable()) {
    this->worker.join();
  }

  delete this->net;
  this->net = nullptr;
}

void Peer::set_offerer(bool offerer) {
  this->offerer.store(offerer);
  this->commands.push(Command{Command::kRestart, std::string()});
}

uint64_t Peer::get_state() const { return this->state.load(); }

void Peer::set_state(uint64_t state) {
  this->state.store(state);

  if (state == kStateTryReset) {
    this->commands.push(Command{Command::kRestart, std::string()});
  }
}

std::string Peer::get_connection_code() {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  return this->connection_code;
}

void Peer::set_connection_code(std::string code) {
  this->store_code(code);
  this->commands.push(Command{Command::kJoin, std::move(code)});
}

void Peer::run() {
  while (this->running.load()) {
    std::queue<Command> batch = this->commands.take_all();

    while (!batch.empty()) {
      const Command& command = batch.front();

      // Only a queued command replaces the switch, because a switch is one
      // shot. The steady state is poll_switch below.
      this->begin_session();

      if (command.kind == Command::kJoin &&
          !this->net->join(Switch::Options(), command.code.c_str())) {
        LOG(ERROR) << "Peer: the room " << command.code << " was not entered";
      }

      batch.pop();
    }

    this->poll_switch();

    std::this_thread::sleep_for(std::chrono::milliseconds(PEER_POLL_MS));
  }
}

void Peer::begin_session() {
  // The replacement is created before the old switch is destroyed, so the
  // library stays loaded instead of being unloaded and reloaded on every
  // restart.
  Switch* replacement = new WebRtcSwitch();

  delete this->net;
  this->net = replacement;

  this->send_queue.take_all();
  this->recv_queue.take_all();

  if (!this->offerer.load()) {
    if (this->net->host(Switch::Options())) {
      this->store_code(this->net->get_token());
    } else {
      LOG(ERROR) << "Peer: a room could not be opened";
    }
  }

  this->publish_state();
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
