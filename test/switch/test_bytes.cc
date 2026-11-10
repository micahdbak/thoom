#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "switch/bytes.h"

namespace {

void expect_failed(const thoom::ByteReader& reader) {
  EXPECT_FALSE(reader.ok());
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(Bytes, WritesFixedWidthLittleEndian) {
  thoom::ByteWriter writer;

  writer.write_u8(0x12);
  writer.write_u16(0x3456);
  writer.write_u32(0x789abcde);
  writer.write_u64(0x0123456789abcdefULL);

  const std::vector<uint8_t> expected = {0x12, 0x56, 0x34, 0xde, 0xbc,
                                         0x9a, 0x78, 0xef, 0xcd, 0xab,
                                         0x89, 0x67, 0x45, 0x23, 0x01};
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(Bytes, WritesFloatBits) {
  thoom::ByteWriter writer;

  writer.write_float(1.5f);

  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({0x00, 0x00, 0xc0, 0x3f}));
}

TEST(Bytes, WritesBoolAsByte) {
  thoom::ByteWriter writer;

  writer.write_bool(true);
  writer.write_bool(false);

  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({1, 0}));
}

TEST(Bytes, WritesStringWithLength) {
  thoom::ByteWriter writer;

  writer.write_string("abc");
  writer.write_string("");

  const std::vector<uint8_t> expected = {3, 0, 0, 0, 'a', 'b', 'c', 0, 0, 0, 0};
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(Bytes, WritesStringWithEmbeddedNulls) {
  const std::string value("a\0b", 3);

  thoom::ByteWriter writer;
  writer.write_string(value);

  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({3, 0, 0, 0, 'a', 0, 'b'}));
}

TEST(Bytes, WritesBlobWithLength) {
  thoom::ByteWriter writer;

  writer.write_blob("xy", 2);

  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({2, 0, 0, 0, 'x', 'y'}));
}

TEST(Bytes, WritesEmptyBlobForNull) {
  thoom::ByteWriter writer;

  writer.write_blob(nullptr, 4);

  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({0, 0, 0, 0}));
}

TEST(Bytes, ReadsValuesInOrder) {
  thoom::ByteWriter writer;
  writer.write_u8(0x12);
  writer.write_u16(0x3456);
  writer.write_u32(0x789abcde);
  writer.write_u64(0x0123456789abcdefULL);
  writer.write_float(1.5f);
  writer.write_bool(true);
  writer.write_string("abc");
  writer.write_blob("xy", 2);

  thoom::ByteReader reader(writer.bytes().data(),
                           static_cast<uint32_t>(writer.bytes().size()));

  EXPECT_EQ(reader.read_u8(), 0x12);
  EXPECT_EQ(reader.read_u16(), 0x3456);
  EXPECT_EQ(reader.read_u32(), 0x789abcde);
  EXPECT_EQ(reader.read_u64(), 0x0123456789abcdefULL);
  EXPECT_FLOAT_EQ(reader.read_float(), 1.5f);
  EXPECT_TRUE(reader.read_bool());
  EXPECT_EQ(reader.read_string(), "abc");

  uint32_t blob_size = 0;
  const uint8_t* blob = reader.read_blob(&blob_size);
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob_size, 2u);
  EXPECT_EQ(blob[0], 'x');
  EXPECT_EQ(blob[1], 'y');

  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(Bytes, ReadsBoolFromAnyNonZeroByte) {
  const std::vector<uint8_t> input = {2};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  EXPECT_TRUE(reader.read_bool());
  EXPECT_TRUE(reader.ok());
}

TEST(Bytes, ReadsBlobWithoutSizeOut) {
  thoom::ByteWriter writer;
  writer.write_blob("xy", 2);

  thoom::ByteReader reader(writer.bytes().data(),
                           static_cast<uint32_t>(writer.bytes().size()));

  const uint8_t* blob = reader.read_blob(nullptr);
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob[0], 'x');
  EXPECT_EQ(blob[1], 'y');
  EXPECT_TRUE(reader.ok());
}

TEST(Bytes, ReportsRemaining) {
  const std::vector<uint8_t> input = {1, 2, 3};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  EXPECT_EQ(reader.remaining(), 3u);
  reader.read_u8();
  EXPECT_EQ(reader.remaining(), 2u);
  reader.read_u16();
  EXPECT_EQ(reader.remaining(), 0u);
  EXPECT_TRUE(reader.ok());
}

TEST(Bytes, FailsEveryTruncation) {
  thoom::ByteWriter writer;
  writer.write_u8(0x12);
  writer.write_u16(0x3456);
  writer.write_u32(0x789abcde);
  writer.write_u64(0x0123456789abcdefULL);
  writer.write_float(1.5f);
  writer.write_bool(true);
  writer.write_string("ab");
  writer.write_blob("xy", 2);

  const std::vector<uint8_t> sample = writer.bytes();

  for (size_t length = 0; length < sample.size(); length++) {
    thoom::ByteReader reader(sample.data(), static_cast<uint32_t>(length));
    reader.read_u8();
    reader.read_u16();
    reader.read_u32();
    reader.read_u64();
    reader.read_float();
    reader.read_bool();
    reader.read_string();

    uint32_t blob_size = 0;
    reader.read_blob(&blob_size);

    SCOPED_TRACE(length);
    expect_failed(reader);
  }

  thoom::ByteReader complete(sample.data(),
                             static_cast<uint32_t>(sample.size()));
  complete.read_u8();
  complete.read_u16();
  complete.read_u32();
  complete.read_u64();
  complete.read_float();
  complete.read_bool();
  complete.read_string();
  uint32_t blob_size = 0;
  complete.read_blob(&blob_size);
  EXPECT_TRUE(complete.ok());
}

TEST(Bytes, RejectsNullDataWithSize) {
  thoom::ByteReader reader(nullptr, 4);

  expect_failed(reader);
  EXPECT_EQ(reader.read_u8(), 0);
  EXPECT_EQ(reader.read_u32(), 0);
}

TEST(Bytes, AcceptsNullDataWithZeroSize) {
  thoom::ByteReader reader(nullptr, 0);

  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(reader.remaining(), 0u);
  EXPECT_EQ(reader.read_u8(), 0);
  expect_failed(reader);
}

TEST(Bytes, RejectsBlobLargerThanRemaining) {
  const std::vector<uint8_t> input = {0xff, 0xff, 0xff, 0xff};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  uint32_t blob_size = 0;
  EXPECT_EQ(reader.read_blob(&blob_size), nullptr);
  expect_failed(reader);
}

TEST(Bytes, RejectsStringLargerThanRemaining) {
  const std::vector<uint8_t> input = {0x10, 0x00, 0x00, 0x00, 'a'};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  EXPECT_EQ(reader.read_string(), "");
  expect_failed(reader);
}

TEST(Bytes, ReadPastEndStaysFailed) {
  const std::vector<uint8_t> input = {7};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  EXPECT_EQ(reader.read_u8(), 7);
  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(reader.read_u32(), 0);
  expect_failed(reader);
  EXPECT_EQ(reader.read_u16(), 0);
  expect_failed(reader);
}

TEST(Bytes, ClearResetsWriter) {
  thoom::ByteWriter writer;
  writer.write_u32(1);

  writer.clear();

  EXPECT_TRUE(writer.bytes().empty());
  writer.write_u8(7);
  EXPECT_EQ(writer.bytes(), std::vector<uint8_t>({7}));
}

TEST(Bytes, WriterHandlesSelfAliasingBlob) {
  thoom::ByteWriter writer;
  writer.write_u32(0x11223344);

  writer.write_blob(writer.bytes().data(),
                    static_cast<uint32_t>(writer.bytes().size()));

  const std::vector<uint8_t> expected = {0x44, 0x33, 0x22, 0x11, 4,    0,
                                         0,    0,    0x44, 0x33, 0x22, 0x11};
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(Bytes, WriterHandlesSelfAliasingSubrange) {
  thoom::ByteWriter writer;
  writer.write_u32(0x11223344);

  writer.write_blob(writer.bytes().data() + 1, 2);

  const std::vector<uint8_t> expected = {0x44, 0x33, 0x22, 0x11, 2,
                                         0,    0,    0,    0x33, 0x22};
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(Bytes, WriterHandlesSelfAliasingAfterGrowth) {
  thoom::ByteWriter writer;
  for (uint8_t value = 0; value < 64; value++) {
    writer.write_u8(value);
  }

  const std::vector<uint8_t> before = writer.bytes();
  writer.write_blob(before.data(), static_cast<uint32_t>(before.size()));

  ASSERT_EQ(writer.bytes().size(), 64u + 4u + 64u);
  EXPECT_EQ(writer.bytes()[64], 64);
  for (size_t i = 0; i < 64; i++) {
    EXPECT_EQ(writer.bytes()[68 + i], before[i]);
  }
}

TEST(Bytes, WriterClampsAliasedOverrun) {
  thoom::ByteWriter writer;
  writer.write_u32(0x11223344);

  writer.write_blob(writer.bytes().data() + 2, 10);

  const std::vector<uint8_t> expected = {0x44, 0x33, 0x22, 0x11, 2,
                                         0,    0,    0,    0x22, 0x11};
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(Bytes, WriterHandlesRepeatedAliasedWrites) {
  thoom::ByteWriter writer;

  for (int i = 0; i < 100; i++) {
    writer.clear();
    writer.write_u32(0x11223344);
    writer.write_blob(writer.bytes().data(),
                      static_cast<uint32_t>(writer.bytes().size()));
    EXPECT_EQ(writer.bytes().size(), 12u);
    EXPECT_EQ(writer.bytes()[4], 4);
  }
}

TEST(Bytes, ReadsUnalignedInput) {
  thoom::ByteWriter writer;
  writer.write_u8(0x12);
  writer.write_u32(0x11223344);
  writer.write_string("ab");
  writer.write_float(1.5f);

  const std::vector<uint8_t> sample = writer.bytes();
  std::vector<uint8_t> storage(sample.size() + 1, 0);
  for (size_t i = 0; i < sample.size(); i++) {
    storage[i + 1] = sample[i];
  }

  thoom::ByteReader reader(storage.data() + 1,
                           static_cast<uint32_t>(sample.size()));

  EXPECT_EQ(reader.read_u8(), 0x12);
  EXPECT_EQ(reader.read_u32(), 0x11223344);
  EXPECT_EQ(reader.read_string(), "ab");
  EXPECT_FLOAT_EQ(reader.read_float(), 1.5f);
  EXPECT_TRUE(reader.ok());
}

TEST(Bytes, ReadsFloatBitPatterns) {
  const uint32_t patterns[] = {0x00000000, 0x80000000, 0x7f800000,
                               0xff800000, 0x7fc00000, 0x00000001};

  for (const uint32_t pattern : patterns) {
    SCOPED_TRACE(pattern);

    float value = 0.0f;
    std::memcpy(&value, &pattern, sizeof(value));

    thoom::ByteWriter writer;
    writer.write_float(value);

    thoom::ByteReader reader(writer.bytes().data(),
                             static_cast<uint32_t>(writer.bytes().size()));
    const float result = reader.read_float();

    uint32_t result_bits = 0;
    std::memcpy(&result_bits, &result, sizeof(result_bits));

    EXPECT_EQ(result_bits, pattern);
    EXPECT_TRUE(reader.ok());
  }
}

TEST(Bytes, ReadsEmptyString) {
  const std::vector<uint8_t> input = {0, 0, 0, 0};

  thoom::ByteReader reader(input.data(), static_cast<uint32_t>(input.size()));

  EXPECT_EQ(reader.read_string(), "");
  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(Bytes, ReadsZeroLengthBlobAtEnd) {
  thoom::ByteWriter writer;
  writer.write_u32(0x11223344);
  writer.write_blob(nullptr, 0);

  thoom::ByteReader reader(writer.bytes().data(),
                           static_cast<uint32_t>(writer.bytes().size()));

  EXPECT_EQ(reader.read_u32(), 0x11223344);

  uint32_t blob_size = 1;
  const uint8_t* blob = reader.read_blob(&blob_size);
  EXPECT_NE(blob, nullptr);
  EXPECT_EQ(blob_size, 0u);
  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(Bytes, SurvivesRepeatedFailedStrings) {
  const std::vector<uint8_t> forged = {0xff, 0xff, 0xff, 0xff};

  for (int i = 0; i < 1000; i++) {
    thoom::ByteReader reader(forged.data(),
                             static_cast<uint32_t>(forged.size()));
    uint32_t blob_size = 0;
    EXPECT_EQ(reader.read_blob(&blob_size), nullptr);
    EXPECT_FALSE(reader.ok());
    EXPECT_EQ(reader.read_string(), "");
    EXPECT_FALSE(reader.ok());
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
