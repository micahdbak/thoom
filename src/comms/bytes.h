#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thoom {

class ByteWriter {
 public:
  void write_u8(uint8_t value);
  void write_u16(uint16_t value);
  void write_u32(uint32_t value);
  void write_u64(uint64_t value);
  void write_float(float value);
  void write_bool(bool value);
  void write_string(const std::string& value);
  void write_blob(const void* data, uint32_t size);

  const std::vector<uint8_t>& bytes() const;
  void clear();

 private:
  std::vector<uint8_t> buffer;
};

class ByteReader {
 public:
  ByteReader(const void* data, uint32_t size);

  bool ok() const;
  uint32_t remaining() const;

  uint8_t read_u8();
  uint16_t read_u16();
  uint32_t read_u32();
  uint64_t read_u64();
  float read_float();
  bool read_bool();
  std::string read_string();
  const uint8_t* read_blob(uint32_t* size);

 private:
  const uint8_t* take(uint32_t size);

  const uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t offset = 0;
  bool failed = false;
};

};  // namespace thoom
