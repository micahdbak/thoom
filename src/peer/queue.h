#pragma once

#include <mutex>
#include <queue>
#include <utility>

namespace thoom {

template <typename T>
class MutexQueue {
 public:
  void push(T message) {
    std::lock_guard<std::mutex> guard(this->mutex);
    this->messages.push(std::move(message));
  }

  bool pop(T* message) {
    std::lock_guard<std::mutex> guard(this->mutex);

    if (this->messages.empty()) {
      return false;
    }

    *message = std::move(this->messages.front());
    this->messages.pop();
    return true;
  }

  std::queue<T> take_all() {
    std::lock_guard<std::mutex> guard(this->mutex);

    std::queue<T> messages;
    while (!this->messages.empty()) {
      messages.push(std::move(this->messages.front()));
      this->messages.pop();
    }

    return messages;
  }

 private:
  std::mutex mutex;
  std::queue<T> messages;
};

};  // namespace thoom
