#ifndef THOOM_COMMS_H
#define THOOM_COMMS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THOOM_COMMS_PEER_NONE ((uint64_t)UINT64_MAX)

enum thoom_comms_role {
  THOOM_COMMS_ROLE_HOST = 0,
  THOOM_COMMS_ROLE_GUEST = 1,
};

enum thoom_comms_state {
  THOOM_COMMS_STATE_WAITING = 0,
  THOOM_COMMS_STATE_HANDSHAKING,
  THOOM_COMMS_STATE_READY,
  THOOM_COMMS_STATE_CLOSED,
};

enum thoom_comms_event_kind {
  THOOM_COMMS_EVENT_JOIN = 0,
  THOOM_COMMS_EVENT_LEAVE,
  THOOM_COMMS_EVENT_REJECT,
};

enum thoom_comms_reason {
  THOOM_COMMS_REASON_NONE = 0,
  THOOM_COMMS_REASON_VERSION,
  THOOM_COMMS_REASON_ROLE,
  THOOM_COMMS_REASON_FULL,
  THOOM_COMMS_REASON_PROTOCOL,
  THOOM_COMMS_REASON_TIMEOUT,
  THOOM_COMMS_REASON_TRANSPORT,
};

struct thoom_comms_event {
  enum thoom_comms_event_kind kind;
  uint64_t peer;
  enum thoom_comms_reason reason;
};

struct thoom_comms_message {
  const uint8_t* bytes;
  uint32_t size;
  bool reliable;
};

struct thoom_comms;

struct thoom_comms* thoom_comms_create(enum thoom_comms_role role);

void thoom_comms_destroy(struct thoom_comms* comms);

/* The join model is a text token: the host exposes one and the guest supplies
   it. A later backend renders its own join handle, such as a Steam lobby id, as
   this token. */
bool thoom_comms_set_code(struct thoom_comms* comms, const char* code);

const char* thoom_comms_get_code(struct thoom_comms* comms);

void thoom_comms_poll(struct thoom_comms* comms);

enum thoom_comms_state thoom_comms_state(struct thoom_comms* comms);

uint64_t thoom_comms_pid(struct thoom_comms* comms);

bool thoom_comms_next_event(struct thoom_comms* comms,
                            struct thoom_comms_event* event);

bool thoom_comms_receive(struct thoom_comms* comms,
                         struct thoom_comms_message* message);

bool thoom_comms_send(struct thoom_comms* comms, bool reliable,
                      const void* data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_COMMS_H
