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

bool MessageQueue::take_last(std::string* message) {
  std::lock_guard<std::mutex> guard(this->mutex);
  if (this->messages.empty()) {
    return false;
  }

  *message = std::move(this->messages.back());
  while (!this->messages.empty()) this->messages.pop();
  return true;
}

std::queue<std::string> MessageQueue::take_all() {
  std::lock_guard<std::mutex> guard(this->mutex);
  std::queue<std::string> copy;
  std::swap(copy, this->messages);
  return copy;
}

};  // namespace thoom
