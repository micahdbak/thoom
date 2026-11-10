#include "loopback.h"

#include <cstdint>
#include <deque>
#include <utility>

#include "connection.h"

// An in-process pair of connections. Everything runs on the calling thread, so
// delivery is immediate and a test can script every frame that reaches a
// session. inject() puts bytes in this endpoint's inbox, which is what the
// remote peer would have sent.

void LoopbackConnection::make_pair(LoopbackConnection** first,
                                   LoopbackConnection** second) {
  LoopbackConnection* left = new LoopbackConnection();
  LoopbackConnection* right = new LoopbackConnection();

  left->peer = right;
  right->peer = left;

  *first = left;
  *second = right;
}

void LoopbackConnection::poll() {}

bool LoopbackConnection::receive(thoom::Message* message) {
  if (this->incoming.empty()) {
    return false;
  }

  *message = std::move(this->incoming.front());
  this->incoming.pop_front();
  return true;
}

bool LoopbackConnection::send(const thoom::Message& message) {
  if (this->state_value != kReady || this->peer == nullptr ||
      this->peer->state_value != kReady) {
    return false;
  }

  this->peer->incoming.push_back(message);
  return true;
}

thoom::Connection::State LoopbackConnection::state() const {
  return this->state_value;
}

void LoopbackConnection::close() {
  if (this->state_value != kReady) {
    return;
  }

  this->state_value = kDead;

  if (this->peer != nullptr && this->peer->state_value == kReady) {
    this->peer->state_value = kFailed;
  }
}

void LoopbackConnection::inject(const void* data, uint32_t size,
                                bool reliable) {
  thoom::Message message;
  message.reliable = reliable;

  if (data != nullptr && size > 0) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    message.bytes.assign(bytes, bytes + size);
  }

  this->incoming.push_back(std::move(message));
}
