#include "peer/peer.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "thoom/macros.h"
#include "thoom/thoom.h"

#define PEER_GUEST_PID ((thoom_pid)1)

namespace thoom {

bool Peer::init(bool offerer) {
  if (Peer::instance != nullptr) {
    fprintf(stderr, THOOM_LOG "Peer::init error: a peer already exists.\n");
    return false;
  }

  Peer::instance = new Peer(offerer);
  return true;
}

void Peer::quit() {
  if (Peer::instance == nullptr) {
    return;
  }

  delete Peer::instance;
  Peer::instance = nullptr;
}

Peer::Peer(bool offerer) : thread(offerer) {
  this->set_pid(offerer ? PEER_GUEST_PID : THOOM_PID_HOST);
}

Peer::~Peer() = default;

void Peer::set_offerer(bool offerer) {
  this->set_pid(offerer ? PEER_GUEST_PID : THOOM_PID_HOST);
  this->thread.set_offerer(offerer);
}

thoom_pid Peer::get_pid() const { return this->pid.load(); }

void Peer::set_pid(thoom_pid pid) { this->pid.store(pid); }

uint64_t Peer::get_state() const { return this->state.load(); }

void Peer::set_state(uint64_t state) { this->state.store(state); }

std::string Peer::get_connection_code() {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  return this->connection_code;
}

void Peer::set_connection_code(std::string code) {
  std::lock_guard<std::mutex> guard(this->connection_code_mutex);
  this->connection_code = code;
}

};  // namespace thoom
