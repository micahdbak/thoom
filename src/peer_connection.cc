#include "peer_connection.h"

#include <mutex>
#include <rtc/rtc.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#define NUM_ICE_SERVERS 2

#define NTS -1

namespace thoom {

static const char* ICE_SERVERS[] = {"stun:droppr.net:5051",
                                    "turn:droppr:droppr@droppr.net:5051"};

void PeerConnection::start_collecting(bool offerer) {
  if (this->state != PeerConnection::State::DEAD) {
    std::cerr << "PeerConnection::start_collecting fatal error: already "
                 "collecting or alive"
              << std::endl;
    std::exit(1);
  }

  rtcConfiguration config = {};
  config.iceServers = ICE_SERVERS;
  config.iceServersCount = NUM_ICE_SERVERS;
  config.proxyServer = nullptr;
  config.bindAddress = nullptr;
  config.certificateType = RTC_CERTIFICATE_DEFAULT;
  config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
  config.enableIceTcp = true;
  config.enableIceUdpMux = false;
  config.disableAutoNegotiation = false;
  config.forceMediaTransport = false;
  config.portRangeBegin = 0;  // unused
  config.portRangeEnd = 0;    // unused
  config.mtu = 0;             // default
  config.maxMessageSize = 0;  // default

  this->pc = rtcCreatePeerConnection(&config);
  if (this->pc < 0) {
    std::cerr << "PeerConnection::start_collecting fatal error: "
                 "rtcCreatePeerConnection returned error code: "
              << this->pc << std::endl;
    exit(1);
  }
  rtcSetUserPointer(this->pc, this);
  rtcSetLocalCandidateCallback(this->pc, PeerConnection::on_candidate);
  rtcSetLocalDescriptionCallback(this->pc, PeerConnection::on_description);
  rtcSetDataChannelCallback(this->pc, PeerConnection::on_datachannel);
  rtcSetClosedCallback(this->pc, PeerConnection::on_close);
  rtcSetErrorCallback(this->pc, PeerConnection::on_error);

  if (offerer) {
    this->dc = rtcCreateDataChannel(this->pc, "messages");
    if (this->dc < 0) {
      std::cerr << "PeerConnection::start_collecting fatal error: "
                   "rtcCreateDataChannel returned error code: "
                << this->dc << std::endl;
      exit(1);
    }
    rtcSetOpenCallback(this->dc, PeerConnection::on_open);
    rtcSetClosedCallback(this->dc, PeerConnection::on_close);
    rtcSetErrorCallback(this->dc, PeerConnection::on_error);
    rtcSetMessageCallback(this->dc, PeerConnection::on_message);
  }

  this->state = PeerConnection::State::COLLECTING;
}

void PeerConnection::kill() {
  if (this->dc != -1) {
    rtcDelete(this->dc);
    this->dc = -1;
  }

  if (this->pc != -1) {
    rtcDelete(this->pc);
    this->pc = -1;
  }

  this->state = PeerConnection::State::DEAD;
}

void PeerConnection::set_remote_description(const std::string& sdp,
                                            const std::string& type) {
  if (this->state != PeerConnection::COLLECTING || this->pc < 0) {
    std::cerr << "PeerConnection::set_remote_description fatal error: bad state"
              << std::endl;
    std::exit(1);
  }

  rtcSetRemoteDescription(this->pc, sdp.c_str(), type.c_str());
}

std::pair<std::string, std::string> PeerConnection::get_local_description() {
  std::lock_guard<std::mutex> guard(this->data_mutex);
  std::pair<std::string, std::string> desc = this->description;
  this->description = std::pair<std::string, std::string>("", "");
  return desc;
}

void PeerConnection::add_candidate(const std::string& cand,
                                   const std::string& mid) {
  if (this->state != PeerConnection::COLLECTING || this->pc < 0) {
    std::cerr << "PeerConnection::add_candidate fatal error: bad state"
              << std::endl;
    std::exit(1);
  }

  rtcAddRemoteCandidate(this->pc, cand.c_str(), mid.c_str());
}

std::pair<std::string, std::string> PeerConnection::get_candidate() {
  std::lock_guard<std::mutex> guard(this->data_mutex);

  if (this->candidates.empty()) {
    return std::pair<std::string, std::string>("", "");
  }

  std::pair<std::string, std::string> candidate = this->candidates.front();
  this->candidates.pop();
  return candidate;
}

void PeerConnection::send(const std::string& message) {
  if (this->state != PeerConnection::ALIVE || this->pc < 0) {
    std::cerr << "PeerConnection::send warning: couldn't send message"
              << std::endl;
    return;
  }

  rtcSendMessage(this->dc, message.c_str(), NTS);
}

std::string PeerConnection::receive() {
  std::lock_guard<std::mutex> guard(this->data_mutex);

  if (this->messages.empty()) {
    return "";
  }

  std::string msg = this->messages.front();
  this->messages.pop();
  return msg;
}

void PeerConnection::on_description(int, const char* sdp, const char* type,
                                    void* user_ptr) {
  // std::cout << "PeerConnection::on_description" << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  std::lock_guard<std::mutex> guard(connection->data_mutex);
  connection->description.first = sdp;
  connection->description.second = type;
}

void PeerConnection::on_candidate(int, const char* cand, const char* mid,
                                  void* user_ptr) {
  // std::cout << "PeerConnection::on_candidate" << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  std::lock_guard<std::mutex> guard(connection->data_mutex);
  connection->candidates.push(std::pair<std::string, std::string>(cand, mid));
}

void PeerConnection::on_datachannel(int, int dc, void* user_ptr) {
  // std::cout << "PeerConnection::on_datachannel" << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  connection->dc = dc;
  rtcSetOpenCallback(dc, PeerConnection::on_open);
  rtcSetClosedCallback(dc, PeerConnection::on_close);
  rtcSetErrorCallback(dc, PeerConnection::on_error);
  rtcSetMessageCallback(dc, PeerConnection::on_message);
  connection->state = PeerConnection::State::ALIVE;
}

void PeerConnection::on_open(int, void* user_ptr) {
  // std::cout << "PeerConnection::on_open" << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  connection->state = PeerConnection::State::ALIVE;
}

void PeerConnection::on_close(int, void* user_ptr) {
  // std::cout << "PeerConnection::on_close" << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  connection->kill();
}

void PeerConnection::on_error(int, const char* error, void* user_ptr) {
  std::cerr << "PeerConnection::on_error: " << error << std::endl;
  PeerConnection* connection = (PeerConnection*)user_ptr;
  connection->kill();

  // overwrite state change to DEAD in kill
  connection->state = PeerConnection::State::ERROR;
}

void PeerConnection::on_message(int, const char* message, int size,
                                void* user_ptr) {
  PeerConnection* connection = (PeerConnection*)user_ptr;
  if (size > 0) {
    std::cerr
        << "PeerConnection::on_message fatal error: received binary message"
        << std::endl;
    std::exit(1);
  }
  std::lock_guard<std::mutex> guard(connection->data_mutex);
  connection->messages.push(message);
}

};  // namespace thoom
