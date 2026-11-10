#include "switch/protocol.h"

#include <cstdint>
#include <vector>

#define VERSION_OFFSET 0
#define KIND_OFFSET 1
#define FLAGS_OFFSET 2

namespace thoom {

// Both backends deliver whole messages, so the frame carries no length field.
// The payload limit is a memory guard checked before anything is copied.
bool decode_frame(const void* data, uint32_t size, Frame* frame) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);

  if (bytes == nullptr || size < kFrameHeaderSize) {
    return false;
  }

  frame->version = bytes[VERSION_OFFSET];
  frame->kind = bytes[KIND_OFFSET];
  frame->flags = bytes[FLAGS_OFFSET];

  const uint32_t length = size - kFrameHeaderSize;
  if (length > kMaxPayload) {
    return false;
  }

  frame->payload = bytes + kFrameHeaderSize;
  frame->size = length;
  return true;
}

bool encode_frame(uint8_t version, uint8_t kind, uint8_t flags,
                  const void* payload, uint32_t size,
                  std::vector<uint8_t>* bytes) {
  if (bytes == nullptr || size > kMaxPayload) {
    return false;
  }

  if (payload == nullptr) {
    size = 0;
  }

  bytes->clear();
  bytes->reserve(kFrameHeaderSize + size);
  bytes->push_back(version);
  bytes->push_back(kind);
  bytes->push_back(flags);

  if (size > 0) {
    const uint8_t* body = static_cast<const uint8_t*>(payload);
    bytes->insert(bytes->end(), body, body + size);
  }

  return true;
}

};  // namespace thoom
