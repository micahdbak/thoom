#include "switch/protocol.h"

#include <cstdint>
#include <vector>

#define VERSION_OFFSET 0
#define KIND_OFFSET 1
#define FLAGS_OFFSET 2
#define LENGTH_OFFSET 3

namespace thoom {

// The header's length field must equal the payload bytes that arrived, so both
// truncation and trailing bytes come back as kDecodeLength.
DecodeResult decode_frame(const void* data, uint32_t size, Frame* frame) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);

  if (bytes == nullptr || size < kFrameHeaderSize) {
    return kDecodeShort;
  }

  frame->version = bytes[VERSION_OFFSET];
  frame->kind = bytes[KIND_OFFSET];
  frame->flags = bytes[FLAGS_OFFSET];

  const uint32_t length =
      static_cast<uint32_t>(bytes[LENGTH_OFFSET]) |
      (static_cast<uint32_t>(bytes[LENGTH_OFFSET + 1]) << 8) |
      (static_cast<uint32_t>(bytes[LENGTH_OFFSET + 2]) << 16) |
      (static_cast<uint32_t>(bytes[LENGTH_OFFSET + 3]) << 24);

  if (length > kMaxPayload || length != size - kFrameHeaderSize) {
    return kDecodeLength;
  }

  frame->payload = bytes + kFrameHeaderSize;
  frame->size = length;
  return kDecodeOk;
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
  bytes->push_back(static_cast<uint8_t>(size));
  bytes->push_back(static_cast<uint8_t>(size >> 8));
  bytes->push_back(static_cast<uint8_t>(size >> 16));
  bytes->push_back(static_cast<uint8_t>(size >> 24));

  if (size > 0) {
    const uint8_t* body = static_cast<const uint8_t*>(payload);
    bytes->insert(bytes->end(), body, body + size);
  }

  return true;
}

};  // namespace thoom
