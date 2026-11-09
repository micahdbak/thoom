#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thoom {

struct Message {
  std::vector<uint8_t> bytes;
  bool reliable = true;
};

class Connection {
 public:
  enum State {
    kDead = 0,
    kConnecting,
    kReady,
    kFailed,
  };

  Connection() = default;
  virtual ~Connection() = default;

  virtual void poll() = 0;

  virtual bool receive(Message* message) = 0;

  virtual bool send(const Message& message) = 0;

  virtual State state() const = 0;

  virtual void close() = 0;
};

};  // namespace thoom
