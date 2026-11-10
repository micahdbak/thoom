#include "comms/bytes.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The wire is little-endian. Upper layers read and write through these
// classes so no other file carries an opinion about byte order.

static const uint8_t EMPTY_BYTE = 0;

namespace thoom {

void ByteWriter::write_u8(uint8_t value) { this->buffer.push_back(value); }

void ByteWriter::write_u16(uint16_t value) {
  this->write_u8(static_cast<uint8_t>(value));
  this->write_u8(static_cast<uint8_t>(value >> 8));
}

void ByteWriter::write_u32(uint32_t value) {
  this->write_u8(static_cast<uint8_t>(value));
  this->write_u8(static_cast<uint8_t>(value >> 8));
  this->write_u8(static_cast<uint8_t>(value >> 16));
  this->write_u8(static_cast<uint8_t>(value >> 24));
}

void ByteWriter::write_u64(uint64_t value) {
  this->write_u32(static_cast<uint32_t>(value));
  this->write_u32(static_cast<uint32_t>(value >> 32));
}

void ByteWriter::write_float(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  this->write_u32(bits);
}

void ByteWriter::write_bool(bool value) { this->write_u8(value ? 1 : 0); }

void ByteWriter::write_string(const std::string& value) {
  this->write_blob(value.data(), static_cast<uint32_t>(value.size()));
}

void ByteWriter::write_blob(const void* data, uint32_t size) {
  if (data == nullptr) {
    size = 0;
  }

  this->write_u32(size);
  if (size == 0) {
    return;
  }

  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  this->buffer.insert(this->buffer.end(), bytes, bytes + size);
}

const std::vector<uint8_t>& ByteWriter::bytes() const { return this->buffer; }

void ByteWriter::clear() { this->buffer.clear(); }

ByteReader::ByteReader(const void* data, uint32_t size) {
  if (data == nullptr && size > 0) {
    this->failed = true;
    return;
  }

  this->data = size > 0 ? static_cast<const uint8_t*>(data) : &EMPTY_BYTE;
  this->size = size;
}

bool ByteReader::ok() const { return !this->failed; }

uint32_t ByteReader::remaining() const {
  if (this->failed) {
    return 0;
  }

  return this->size - this->offset;
}

const uint8_t* ByteReader::take(uint32_t size) {
  if (this->failed || size > this->size - this->offset) {
    this->failed = true;
    return nullptr;
  }

  const uint8_t* result = this->data + this->offset;
  this->offset += size;
  return result;
}

uint8_t ByteReader::read_u8() {
  const uint8_t* bytes = this->take(1);
  if (bytes == nullptr) {
    return 0;
  }

  return bytes[0];
}

uint16_t ByteReader::read_u16() {
  const uint8_t* bytes = this->take(2);
  if (bytes == nullptr) {
    return 0;
  }

  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t ByteReader::read_u32() {
  const uint8_t* bytes = this->take(4);
  if (bytes == nullptr) {
    return 0;
  }

  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t ByteReader::read_u64() {
  const uint64_t low = this->read_u32();
  const uint64_t high = this->read_u32();
  return low | (high << 32);
}

float ByteReader::read_float() {
  const uint32_t bits = this->read_u32();
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(bits));
  return value;
}

bool ByteReader::read_bool() { return this->read_u8() != 0; }

std::string ByteReader::read_string() {
  uint32_t size = 0;
  const uint8_t* bytes = this->read_blob(&size);
  if (bytes == nullptr) {
    return std::string();
  }

  return std::string(reinterpret_cast<const char*>(bytes), size);
}

const uint8_t* ByteReader::read_blob(uint32_t* size) {
  const uint32_t length = this->read_u32();
  if (this->failed) {
    return nullptr;
  }

  const uint8_t* bytes = this->take(length);
  if (bytes == nullptr) {
    return nullptr;
  }

  if (size != nullptr) {
    *size = length;
  }

  return bytes;
}

};  // namespace thoom
