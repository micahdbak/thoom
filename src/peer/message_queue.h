#pragma once

#include <mutex>
#include <queue>
#include <string>

namespace thoom {

class MessageQueue {
 public:
  void push(std::string message);

  bool pop(std::string* message);

  std::queue<std::string> take_all();

 private:
  std::mutex mutex;
  std::queue<std::string> messages;
};

};  // namespace thoom
