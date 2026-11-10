#include "thoom/comms.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "comms/session.h"
#include "connection.h"
#include "thoom/thoom.h"
#include "webrtc/webrtc_connection.h"

// The C API owns the layer order. The transport owns the sockets, the session
// borrows it as a byte pipe, and the session is destroyed first so it never
// sees a dead connection.

struct thoom_comms {
  thoom::WebRtcConnection* connection;
  thoom::Session* session;
  std::vector<uint8_t> data;
};

// The public enums are the wire and ABI contract, so the internal values must
// match them exactly and a mapping function is not allowed to drift.
static_assert(THOOM_COMMS_PEER_NONE == THOOM_PID_INVALID);
static_assert(static_cast<int>(THOOM_COMMS_ROLE_HOST) ==
              static_cast<int>(thoom::Session::kHost));
static_assert(static_cast<int>(THOOM_COMMS_ROLE_GUEST) ==
              static_cast<int>(thoom::Session::kGuest));
static_assert(static_cast<int>(THOOM_COMMS_EVENT_JOIN) ==
              static_cast<int>(thoom::Session::kJoin));
static_assert(static_cast<int>(THOOM_COMMS_EVENT_LEAVE) ==
              static_cast<int>(thoom::Session::kLeave));
static_assert(static_cast<int>(THOOM_COMMS_EVENT_REJECT) ==
              static_cast<int>(thoom::Session::kReject));
static_assert(static_cast<int>(THOOM_COMMS_REASON_NONE) ==
              static_cast<int>(thoom::Session::kReasonNone));
static_assert(static_cast<int>(THOOM_COMMS_REASON_VERSION) ==
              static_cast<int>(thoom::Session::kReasonVersion));
static_assert(static_cast<int>(THOOM_COMMS_REASON_ROLE) ==
              static_cast<int>(thoom::Session::kReasonRole));
static_assert(static_cast<int>(THOOM_COMMS_REASON_FULL) ==
              static_cast<int>(thoom::Session::kReasonFull));
static_assert(static_cast<int>(THOOM_COMMS_REASON_PROTOCOL) ==
              static_cast<int>(thoom::Session::kReasonProtocol));
static_assert(static_cast<int>(THOOM_COMMS_REASON_TIMEOUT) ==
              static_cast<int>(thoom::Session::kReasonTimeout));
static_assert(static_cast<int>(THOOM_COMMS_REASON_TRANSPORT) ==
              static_cast<int>(thoom::Session::kReasonTransport));

static enum thoom_comms_state map_state(thoom::Session::State state) {
  switch (state) {
    case thoom::Session::kWaiting:
      return THOOM_COMMS_STATE_WAITING;
    case thoom::Session::kHandshaking:
      return THOOM_COMMS_STATE_HANDSHAKING;
    case thoom::Session::kReady:
      return THOOM_COMMS_STATE_READY;
    case thoom::Session::kRejected:
    case thoom::Session::kClosed:
      return THOOM_COMMS_STATE_CLOSED;
  }

  return THOOM_COMMS_STATE_CLOSED;
}

struct thoom_comms* thoom_comms_create(enum thoom_comms_role role) {
  if (role != THOOM_COMMS_ROLE_HOST && role != THOOM_COMMS_ROLE_GUEST) {
    return nullptr;
  }

  const bool host = role == THOOM_COMMS_ROLE_HOST;

  thoom::WebRtcConnection* connection = new thoom::WebRtcConnection();
  if (host && !connection->start_host()) {
    delete connection;
    return nullptr;
  }

  thoom::Session::Options options;
  thoom::Session* session =
      new thoom::Session(host ? thoom::Session::kHost : thoom::Session::kGuest,
                         connection, options);

  struct thoom_comms* comms = new struct thoom_comms;
  comms->connection = connection;
  comms->session = session;
  return comms;
}

void thoom_comms_destroy(struct thoom_comms* comms) {
  if (comms == nullptr) {
    return;
  }

  delete comms->session;
  delete comms->connection;
  delete comms;
}

bool thoom_comms_set_code(struct thoom_comms* comms, const char* code) {
  if (comms == nullptr || code == nullptr) {
    return false;
  }

  return comms->connection->start_guest(code);
}

const char* thoom_comms_get_code(struct thoom_comms* comms) {
  if (comms == nullptr) {
    return "";
  }

  return comms->connection->code.c_str();
}

void thoom_comms_poll(struct thoom_comms* comms) {
  if (comms == nullptr) {
    return;
  }

  comms->data.clear();

  if (comms->session->state == thoom::Session::kClosed) {
    if (comms->connection->state() != thoom::Connection::kDead) {
      comms->connection->close();
    }
    return;
  }

  comms->connection->poll();
  comms->session->poll();

  if (comms->session->state == thoom::Session::kClosed) {
    comms->connection->close();
  }
}

enum thoom_comms_state thoom_comms_state(struct thoom_comms* comms) {
  if (comms == nullptr) {
    return THOOM_COMMS_STATE_CLOSED;
  }

  return map_state(comms->session->state);
}

uint64_t thoom_comms_pid(struct thoom_comms* comms) {
  if (comms == nullptr) {
    return THOOM_COMMS_PEER_NONE;
  }

  return comms->session->local_pid;
}

bool thoom_comms_next_event(struct thoom_comms* comms,
                            struct thoom_comms_event* event) {
  if (comms == nullptr || event == nullptr) {
    return false;
  }

  thoom::Session::Event received = {};
  if (!comms->session->next_event(&received)) {
    return false;
  }

  event->kind = static_cast<enum thoom_comms_event_kind>(received.kind);
  event->peer = received.peer;
  event->reason = static_cast<enum thoom_comms_reason>(received.reason);
  return true;
}

bool thoom_comms_receive(struct thoom_comms* comms,
                         struct thoom_comms_message* message) {
  if (comms == nullptr || message == nullptr) {
    return false;
  }

  thoom::Session::Data received;
  if (!comms->session->next_data(&received)) {
    return false;
  }

  comms->data = std::move(received.bytes);

  message->bytes = comms->data.data();
  message->size = static_cast<uint32_t>(comms->data.size());
  message->reliable = received.reliable;
  return true;
}

bool thoom_comms_send(struct thoom_comms* comms, bool reliable,
                      const void* data, uint32_t size) {
  if (comms == nullptr || (data == nullptr && size > 0)) {
    return false;
  }

  return comms->session->send(reliable, data, size);
}
