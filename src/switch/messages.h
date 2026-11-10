#pragma once

#include <cstdint>

#include "switch/bytes.h"

namespace thoom {

struct Hello {
  uint8_t role;
};

struct Welcome {
  uint64_t pid;
};

struct Reject {
  uint8_t reason;
};

void write_hello(ByteWriter* writer, const Hello& hello);

bool read_hello(ByteReader* reader, Hello* hello);

void write_welcome(ByteWriter* writer, const Welcome& welcome);

bool read_welcome(ByteReader* reader, Welcome* welcome);

void write_reject(ByteWriter* writer, const Reject& reject);

bool read_reject(ByteReader* reader, Reject* reject);

};  // namespace thoom
