#include "switch/messages.h"

#include "switch/bytes.h"

namespace thoom {

void write_hello(ByteWriter* writer, const Hello& hello) {
  writer->write_u8(hello.role);
}

bool read_hello(ByteReader* reader, Hello* hello) {
  hello->role = reader->read_u8();
  return reader->ok() && reader->remaining() == 0;
}

void write_welcome(ByteWriter* writer, const Welcome& welcome) {
  writer->write_u64(welcome.pid);
}

bool read_welcome(ByteReader* reader, Welcome* welcome) {
  welcome->pid = reader->read_u64();
  return reader->ok() && reader->remaining() == 0;
}

void write_reject(ByteWriter* writer, const Reject& reject) {
  writer->write_u8(reject.reason);
}

bool read_reject(ByteReader* reader, Reject* reject) {
  reject->reason = reader->read_u8();
  return reader->ok() && reader->remaining() == 0;
}

};  // namespace thoom
