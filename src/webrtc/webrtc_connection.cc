#include "webrtc/webrtc_connection.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "connection.h"
#include "webrtc/webrtc.h"
#include "webrtc/webrtc_channels.h"
#include "webrtc/webrtc_signaller.h"

// The concrete transport: one rendezvous, then a pair of data channels. The
// signaller exists only while the connection is connecting and is dropped once
// both channels are open. A failure is final; the owner destroys the transport
// and creates a fresh one if it wants to try again.

#define CONNECT_PING_MS 500

namespace thoom {

static int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

WebRtcConnection::WebRtcConnection() {
  webrtc_init();

  this->channels = new WebRtcChannels();
  this->signaller = new WebRtcSignaller();
}

WebRtcConnection::~WebRtcConnection() {
  // The library must outlive every object that holds one of its ids, and only
  // one thread may touch it. Both are true here: the owner has stopped polling,
  // and the objects are destroyed before the library.
  this->signaller->disconnect();
  this->channels->close();

  delete this->channels;
  this->channels = nullptr;

  delete this->signaller;
  this->signaller = nullptr;

  webrtc_quit();
}

bool WebRtcConnection::start_host() {
  this->code = WebRtcSignaller::make_code();
  return this->start(false);
}

bool WebRtcConnection::start_guest(const std::string& code) {
  if (!WebRtcSignaller::valid_code(code)) {
    LOG(ERROR) << "WebRtcConnection::start_guest: invalid connection code";
    return false;
  }

  this->code = code;
  return this->start(true);
}

void WebRtcConnection::poll() {
  switch (this->state_value) {
    case kDead:
    case kFailed:
      return;
    case kConnecting:
      this->poll_connecting();
      return;
    case kReady:
      this->channels->poll();
      if (this->channels->state() != kReady) {
        this->fail();
      }
      return;
  }
}

bool WebRtcConnection::receive(Message* message) {
  return this->channels->receive(message);
}

bool WebRtcConnection::send(const Message& message) {
  return this->channels->send(message);
}

Connection::State WebRtcConnection::state() const { return this->state_value; }

void WebRtcConnection::close() {
  this->signaller->disconnect();
  this->channels->close();
  this->state_value = kDead;
}

bool WebRtcConnection::start(bool offerer) {
  if (this->started) {
    return false;
  }

  this->started = true;
  this->offerer = offerer;

  if (!this->signaller->connect(this->code, offerer)) {
    this->state_value = kFailed;
    return false;
  }

  this->state_value = kConnecting;
  return true;
}

void WebRtcConnection::poll_connecting() {
  this->signaller->poll();
  this->channels->poll();

  if (this->signaller->state() == WebRtcSignaller::kNotConnected) {
    this->fail();
    return;
  }

  if (this->channels->state() == kReady) {
    this->signaller->disconnect();
    this->state_value = kReady;
    return;
  }

  if (this->channels->state() == kFailed) {
    this->fail();
    return;
  }

  if (this->signaller->state() != WebRtcSignaller::kConnected) {
    return;
  }

  const int64_t now = now_ms();

  // Keep pinging until the channels are ready, not just until this side starts
  // collecting. The answerer can collect on the offerer's ping first; if it
  // stopped pinging there, the offerer would wait for a ping that never comes
  // and no offer would ever be sent.
  if (now >= this->next_ping_ms) {
    this->next_ping_ms = now + CONNECT_PING_MS;

    if (!this->signaller->send_ping()) {
      this->fail();
      return;
    }
  }

  WebRtcSignaller::Event event = {};
  if (!this->collecting) {
    if (!this->signaller->receive(&event)) {
      return;
    }

    // The server answers fail to every message while no peer has joined.
    if (event.kind == WebRtcSignaller::Event::kFail) {
      return;
    }

    this->collecting = true;
    this->channels->start_collecting(this->offerer);

    if (!this->apply_event(event)) {
      this->fail();
      return;
    }
  }

  while (this->signaller->receive(&event)) {
    if (!this->apply_event(event)) {
      this->fail();
      return;
    }
  }

  if (this->channels->state() == kFailed) {
    this->fail();
    return;
  }

  this->send_local();
}

bool WebRtcConnection::apply_event(const WebRtcSignaller::Event& event) {
  switch (event.kind) {
    case WebRtcSignaller::Event::kPing:
      return true;
    case WebRtcSignaller::Event::kDescription:
      this->channels->set_remote_description(event.first, event.second);
      return true;
    case WebRtcSignaller::Event::kCandidate:
      this->channels->add_candidate(event.first, event.second);
      return true;
    case WebRtcSignaller::Event::kFail:
      return false;
  }

  return false;
}

void WebRtcConnection::send_local() {
  const std::pair<std::string, std::string> description =
      this->channels->local_description();
  if (!description.first.empty()) {
    this->signaller->send_description(description.first, description.second);
  }

  const std::pair<std::string, std::string> candidate =
      this->channels->candidate();
  if (!candidate.first.empty()) {
    this->signaller->send_candidate(candidate.first, candidate.second);
  }
}

void WebRtcConnection::fail() {
  this->signaller->disconnect();
  this->channels->close();
  this->state_value = kFailed;
}

};  // namespace thoom
