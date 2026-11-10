#pragma once

#include <cstdint>
#include <string>

#include "connection.h"
#include "webrtc/webrtc_channels.h"
#include "webrtc/webrtc_signaller.h"

namespace thoom {

class WebRtcConnection : public Connection {
 public:
  WebRtcConnection();
  ~WebRtcConnection() override;

  bool start_host();
  bool start_guest(const std::string& code);

  void poll() override;

  bool receive(Message* message) override;

  bool send(const Message& message) override;

  State state() const override;

  void close() override;

  std::string code;

 private:
  bool start(bool offerer);
  void poll_connecting();
  bool apply_event(const WebRtcSignaller::Event& event);
  void send_local();
  void fail();

  bool offerer = false;
  bool started = false;
  bool collecting = false;
  int64_t next_ping_ms = 0;
  State state_value = kDead;

  WebRtcChannels* channels = nullptr;
  WebRtcSignaller* signaller = nullptr;
};

};  // namespace thoom
