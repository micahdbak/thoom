#pragma once

#include <cstdint>
#include <deque>

#include "connection.h"

class LoopbackConnection : public thoom::Connection {
 public:
  static void make_pair(LoopbackConnection** first,
                        LoopbackConnection** second);

  LoopbackConnection(const LoopbackConnection& other) = delete;
  LoopbackConnection& operator=(const LoopbackConnection& other) = delete;
  ~LoopbackConnection() override = default;

  void poll() override;
  bool receive(thoom::Message* message) override;
  bool send(const thoom::Message& message) override;
  State state() const override;
  void close() override;

  void inject(const void* data, uint32_t size, bool reliable);

  LoopbackConnection* peer = nullptr;

 private:
  LoopbackConnection() = default;

  std::deque<thoom::Message> incoming;
  State state_value = kReady;
};
