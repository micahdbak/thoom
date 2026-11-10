#include "switch/frame.h"

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

  if (frame == nullptr || bytes == nullptr || size < kFrameHeaderSize) {
    return false;
  }

  const uint8_t version = bytes[VERSION_OFFSET];
  const uint8_t kind = bytes[KIND_OFFSET];
  const uint8_t flags = bytes[FLAGS_OFFSET];

  const uint32_t length = size - kFrameHeaderSize;
  if (length > kMaxPayload) {
    return false;
  }

  frame->version = version;
  frame->kind = kind;
  frame->flags = flags;
  frame->payload = bytes + kFrameHeaderSize;
  frame->size = length;
  return true;
}

bool encode_frame(uint8_t version, uint8_t kind, uint8_t flags,
                  const void* payload, uint32_t size,
                  std::vector<uint8_t>* bytes) {
  if (bytes == nullptr || (payload == nullptr && size > 0) ||
      size > kMaxPayload) {
    return false;
  }

  // A payload inside the output is copied before the output is replaced; a
  // claimed range that leaves the output is rejected rather than read.
  const uintptr_t start = reinterpret_cast<uintptr_t>(payload);
  const uintptr_t first = reinterpret_cast<uintptr_t>(bytes->data());
  const uintptr_t last = first + bytes->size();
  if (size > 0 && !bytes->empty() && start >= first && start < last &&
      size > last - start) {
    return false;
  }

  std::vector<uint8_t> frame;
  frame.reserve(kFrameHeaderSize + size);
  frame.push_back(version);
  frame.push_back(kind);
  frame.push_back(flags);

  if (size > 0) {
    const uint8_t* body = static_cast<const uint8_t*>(payload);
    frame.insert(frame.end(), body, body + size);
  }

  bytes->swap(frame);
  return true;
}

};  // namespace thoom
