#include "peer_signaller.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "thoom/macros.h"

//#define SIGNALLING_URL "ws://localhost:8080/sc/"
#define SIGNALLING_URL "wss://cheese.micahdb.com/sc/"

#define NTS -1

namespace thoom {

void PeerSignaller::connect(const std::string& code, bool offerer) {
  if (this->state != PeerSignaller::State::NOT_CONNECTED) {
    fprintf(stderr, THOOM_LOG
            "PeerSignaller::connect fatal error: already connecting or "
            "connected\n");
    exit(1);
  }

  std::string url = SIGNALLING_URL + code;
  this->ws = rtcCreateWebSocket(url.c_str());
  if (this->ws < 0) {
    fprintf(stderr,
            THOOM_LOG
            "PeerSignaller::connect fatal error: rtcCreateWebSocket returned "
            "error code: %d\n",
            this->ws);
    exit(1);
  }

  rtcSetUserPointer(this->ws, this);
  rtcSetOpenCallback(this->ws, PeerSignaller::on_open);
  rtcSetClosedCallback(this->ws, PeerSignaller::on_close);
  rtcSetErrorCallback(this->ws, PeerSignaller::on_error);
  rtcSetMessageCallback(this->ws, PeerSignaller::on_message);

  this->state = PeerSignaller::State::CONNECTING;
}

void PeerSignaller::disconnect() {
  if (this->ws != -1) {
    rtcDelete(this->ws);
    this->ws = -1;
  }

  this->state = PeerSignaller::State::NOT_CONNECTED;
}

void PeerSignaller::send(const std::string& message) {
  if (this->state != PeerSignaller::State::CONNECTED || this->ws < 0) {
    fprintf(stderr,
            THOOM_LOG "PeerSignaller::send fatal error: not connected\n");
    exit(1);
  }

  rtcSendMessage(this->ws, message.c_str(), NTS);
}

std::string PeerSignaller::receive() {
  if (this->messages.empty()) {
    return "";
  }

  std::string msg = this->messages.front();
  this->messages.pop();
  return msg;
}

std::string PeerSignaller::block_receive() {
  // block up to a full second: 10 * 100ms = 1s
  for (int i = 0; i < 10; i++) {
    std::string msg = this->receive();

    if (!msg.empty()) {
      return msg;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return "";
}

void PeerSignaller::on_open(int, void* user_ptr) {
  // std::cout << "PeerSignaller::on_open" << std::endl;
  PeerSignaller* signaller = (PeerSignaller*)user_ptr;
  signaller->state = PeerSignaller::State::CONNECTED;
}

void PeerSignaller::on_close(int, void* user_ptr) {
  // std::cout << "PeerSignaller::on_close" << std::endl;
  PeerSignaller* signaller = (PeerSignaller*)user_ptr;
  signaller->disconnect();
}

void PeerSignaller::on_error(int, const char* error, void* user_ptr) {
  fprintf(stderr, THOOM_LOG "PeerSignaller::on_error: %s\n", error);
  PeerSignaller* signaller = (PeerSignaller*)user_ptr;
  signaller->disconnect();
}

void PeerSignaller::on_message(int, const char* message, int size,
                               void* user_ptr) {
  // std::cout << "PeerSignaller::on_message" << std::endl;
  PeerSignaller* signaller = (PeerSignaller*)user_ptr;
  signaller->messages.push(message);
}

};  // namespace thoom
