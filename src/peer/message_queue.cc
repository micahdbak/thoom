#include "peer/message_queue.h"

#include <mutex>
#include <queue>
#include <string>
#include <utility>

namespace thoom {

void MessageQueue::push(std::string message) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->messages.push(std::move(message));
}

bool MessageQueue::pop(std::string* message) {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->messages.empty()) {
    return false;
  }

  *message = std::move(this->messages.front());
  this->messages.pop();
  return true;
}

std::queue<std::string> MessageQueue::take_all() {
  std::lock_guard<std::mutex> guard(this->mutex);

  std::queue<std::string> messages;
  while (!this->messages.empty()) {
    messages.push(std::move(this->messages.front()));
    this->messages.pop();
  }

  return messages;
}

};  // namespace thoom
