#include "peer/peer_thread.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "connection.h"
#include "peer/peer.h"
#include "webrtc/webrtc.h"
#include "webrtc/webrtc_channels.h"
#include "webrtc/webrtc_signaller.h"

#define PEER_WAIT_MS 10
#define PEER_PING_MS 500
#define PEER_RETRY_MS 500

namespace thoom {

PeerThread::PeerThread(Peer* peer, bool offerer) {
  this->peer = peer;
  this->offerer.store(offerer);
  webrtc_init();

  this->connection = new WebRtcChannels();
  this->signaller = new WebRtcSignaller();

  this->message_thread = new std::thread(&PeerThread::message_loop, this);
}

PeerThread::~PeerThread() {
  {
    std::lock_guard<std::mutex> guard(this->control_mutex);
    this->stop = true;
  }
  this->control_cv.notify_one();

  this->message_thread->join();
  delete this->message_thread;
  this->message_thread = nullptr;

  // The library must outlive every object that holds one of its ids, and only
  // one thread may touch it. Both are true here: the loop has ended, and the
  // objects are destroyed before the library.
  this->shut_down();

  delete this->connection;
  this->connection = nullptr;

  delete this->signaller;
  this->signaller = nullptr;

  webrtc_quit();
}

void PeerThread::set_offerer(bool offerer) {
  this->offerer.store(offerer);

  {
    std::lock_guard<std::mutex> guard(this->control_mutex);
    this->ready = true;
  }

  this->control_cv.notify_one();
}

void PeerThread::message_loop() {
  while (!this->stop_requested()) {
    if (!this->is_ready() || this->peer == nullptr) {
      this->wait_ready();
      continue;
    }

    switch (this->peer->get_state()) {
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
        this->signaller->disconnect();
        this->peer->set_connection_code("");
        this->start_over();
        break;
    }
  }

  this->shut_down();
}

void PeerThread::no_connection() {
  this->signaller->poll();

  const bool failed_attempt = this->signaller->failed();

  if (this->signaller->state() == WebRtcSignaller::kNotConnected) {
    std::string code;

    if (this->offerer.load()) {
      code = this->peer->get_connection_code();
    } else {
      code = WebRtcSignaller::make_code();
      this->peer->set_connection_code(code);
    }

    if (!code.empty() &&
        !this->signaller->connect(code, this->offerer.load())) {
      this->pause(PEER_RETRY_MS);
      return;
    }
  }

  if (this->signaller->state() == WebRtcSignaller::kConnected) {
    this->peer->set_state(Peer::kStateWaitingForPeer);
    this->got_ping = false;
  }

  this->pause(failed_attempt ? PEER_RETRY_MS : PEER_WAIT_MS);
}

void PeerThread::waiting_for_peer() {
  this->signaller->poll();
  this->connection->poll();

  if (this->connection->state() == Connection::kReady) {
    this->signaller->disconnect();
    this->peer->set_state(Peer::kStateConnected);
    return;
  }

  if (this->signaller->state() != WebRtcSignaller::kConnected ||
      this->connection->state() == Connection::kFailed) {
    this->start_over();
    return;
  }

  if (!this->got_ping) {
    if (!this->signaller->send_ping()) {
      this->start_over();
      return;
    }

    WebRtcSignaller::Event event = {};
    if (!this->signaller->receive(&event)) {
      this->signaller->wait(PEER_PING_MS);
      return;
    }

    // The server answers fail to every message while no peer has joined.
    if (event.kind == WebRtcSignaller::Event::kFail) {
      this->signaller->wait(PEER_PING_MS);
      return;
    }

    this->got_ping = true;
    this->connection->start_collecting(this->offerer.load());

    if (!this->apply_event(event)) {
      this->start_over();
      return;
    }
  }

  WebRtcSignaller::Event event = {};
  while (this->signaller->receive(&event)) {
    if (!this->apply_event(event)) {
      this->start_over();
      return;
    }
  }

  const std::pair<std::string, std::string> description =
      this->connection->local_description();
  if (!description.first.empty() &&
      !this->signaller->send_description(description.first,
                                         description.second)) {
    this->start_over();
    return;
  }

  const std::pair<std::string, std::string> candidate =
      this->connection->candidate();
  if (!candidate.first.empty() &&
      !this->signaller->send_candidate(candidate.first, candidate.second)) {
    this->start_over();
    return;
  }

  this->signaller->wait(PEER_WAIT_MS);
}

void PeerThread::connected() {
  this->connection->poll();

  if (this->connection->state() != Connection::kReady) {
    this->start_over();
    return;
  }

  std::string message;
  while (this->peer->send_queue.pop(&message)) {
    Message outgoing;
    outgoing.bytes.assign(message.begin(), message.end());
    outgoing.reliable = true;
    this->connection->send(outgoing);
  }

  Message incoming;
  while (this->connection->receive(&incoming)) {
    this->peer->recv_queue.push(
        std::string(incoming.bytes.begin(), incoming.bytes.end()));
  }

  this->connection->wait(PEER_WAIT_MS);
}

bool PeerThread::apply_event(const WebRtcSignaller::Event& event) {
  switch (event.kind) {
    case WebRtcSignaller::Event::kPing:
      return true;
    case WebRtcSignaller::Event::kDescription:
      this->connection->set_remote_description(event.first, event.second);
      return true;
    case WebRtcSignaller::Event::kCandidate:
      this->connection->add_candidate(event.first, event.second);
      return true;
    case WebRtcSignaller::Event::kFail:
      return false;
  }

  return false;
}

void PeerThread::shut_down() {
  this->connection->close();
  this->signaller->disconnect();
}

void PeerThread::start_over() {
  this->connection->close();
  this->signaller->disconnect();
  this->peer->set_state(Peer::kStateNoConnection);

  this->pause(PEER_RETRY_MS);
}

bool PeerThread::stop_requested() {
  std::lock_guard<std::mutex> guard(this->control_mutex);
  return this->stop;
}

bool PeerThread::is_ready() {
  std::lock_guard<std::mutex> guard(this->control_mutex);
  return this->ready;
}

void PeerThread::wait_ready() {
  std::unique_lock<std::mutex> lock(this->control_mutex);
  this->control_cv.wait_for(lock, std::chrono::milliseconds(PEER_RETRY_MS),
                            [this] { return this->ready || this->stop; });
}

void PeerThread::pause(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(this->control_mutex);
  this->control_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this] { return this->stop; });
}

};  // namespace thoom
