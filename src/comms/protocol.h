#pragma once

#include <cstdint>
#include <vector>

namespace thoom {

constexpr uint8_t kProtocolVersion = 1;

constexpr uint32_t kFrameHeaderSize = 7;
constexpr uint32_t kMaxPayload = 65536;

constexpr uint8_t kFrameFlagsNone = 0;

enum FrameKind {
  kFrameHello = 0,
  kFrameWelcome,
  kFrameReject,
  kFrameData,
};

struct Frame {
  uint8_t version;
  uint8_t kind;
  uint8_t flags;
  const uint8_t* payload;
  uint32_t size;
};

enum DecodeResult {
  kDecodeOk = 0,
  kDecodeShort,
  kDecodeLength,
};

DecodeResult decode_frame(const void* data, uint32_t size, Frame* frame);

bool encode_frame(uint8_t version, uint8_t kind, uint8_t flags,
                  const void* payload, uint32_t size,
                  std::vector<uint8_t>* bytes);

};  // namespace thoom
