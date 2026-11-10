#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "switch/frame.h"

namespace {

typedef thoom::Frame Frame;

TEST(Frame, RoleConstantsMatchTheWire) {
  EXPECT_EQ(thoom::kRoleHost, 0);
  EXPECT_EQ(thoom::kRoleGuest, 1);
}

TEST(Frame, EncodesHeaderAndPayload) {
  std::vector<uint8_t> bytes;

  ASSERT_TRUE(thoom::encode_frame(0x12, 0x34, 0x56, "\xaa\xbb", 2, &bytes));

  EXPECT_EQ(bytes, std::vector<uint8_t>({0x12, 0x34, 0x56, 0xaa, 0xbb}));
}

TEST(Frame, EncodesEmptyPayload) {
  std::vector<uint8_t> bytes;

  ASSERT_TRUE(thoom::encode_frame(1, 2, 3, nullptr, 0, &bytes));

  EXPECT_EQ(bytes, std::vector<uint8_t>({1, 2, 3}));
}

TEST(Frame, EncodeClearsOutput) {
  std::vector<uint8_t> bytes = {9, 9, 9, 9};

  ASSERT_TRUE(thoom::encode_frame(1, 2, 3, nullptr, 0, &bytes));

  EXPECT_EQ(bytes, std::vector<uint8_t>({1, 2, 3}));
}

TEST(Frame, EncodeRejectsNullOutput) {
  EXPECT_FALSE(thoom::encode_frame(1, 2, 3, nullptr, 0, nullptr));
}

TEST(Frame, EncodeRejectsNullPayloadWithSize) {
  std::vector<uint8_t> bytes = {0x5a};

  EXPECT_FALSE(thoom::encode_frame(1, 2, 3, nullptr, 5, &bytes));
  EXPECT_EQ(bytes, std::vector<uint8_t>({0x5a}));
}

TEST(Frame, EncodeRejectsOversizedPayload) {
  const std::vector<uint8_t> payload(thoom::kMaxPayload + 1, 0);
  std::vector<uint8_t> bytes = {0x5a};

  EXPECT_FALSE(thoom::encode_frame(
      1, 2, 3, payload.data(), static_cast<uint32_t>(payload.size()), &bytes));
  EXPECT_EQ(bytes, std::vector<uint8_t>({0x5a}));
}

TEST(Frame, DecodesHeaderAndPayload) {
  const std::vector<uint8_t> input = {0x11, 0x22, 0x33, 0xaa, 0xbb};
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.version, 0x11);
  EXPECT_EQ(frame.kind, 0x22);
  EXPECT_EQ(frame.flags, 0x33);
  EXPECT_EQ(frame.size, 2u);
  EXPECT_EQ(frame.payload, input.data() + thoom::kFrameHeaderSize);
  EXPECT_EQ(frame.payload[0], 0xaa);
  EXPECT_EQ(frame.payload[1], 0xbb);
}

TEST(Frame, DecodesEmptyPayload) {
  const std::vector<uint8_t> input = {1, 2, 3};
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.size, 0u);
  EXPECT_EQ(frame.payload, input.data() + thoom::kFrameHeaderSize);
}

TEST(Frame, DecodeRejectsShortInput) {
  Frame frame = {};
  const uint8_t one = 1;
  const std::vector<uint8_t> two = {1, 2};

  EXPECT_FALSE(thoom::decode_frame(nullptr, 0, &frame));
  EXPECT_FALSE(thoom::decode_frame(nullptr, 3, &frame));
  EXPECT_FALSE(thoom::decode_frame(&one, 1, &frame));
  EXPECT_FALSE(thoom::decode_frame(two.data(), 2, &frame));
}

TEST(Frame, DecodeRejectsOversizedPayload) {
  const std::vector<uint8_t> input(
      thoom::kFrameHeaderSize + thoom::kMaxPayload + 1, 0);
  Frame frame = {};

  EXPECT_FALSE(thoom::decode_frame(
      input.data(), static_cast<uint32_t>(input.size()), &frame));
}

TEST(Frame, DecodeAcceptsMaxPayload) {
  const std::vector<uint8_t> input(thoom::kFrameHeaderSize + thoom::kMaxPayload,
                                   0);
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.size, thoom::kMaxPayload);
  EXPECT_EQ(frame.payload, input.data() + thoom::kFrameHeaderSize);
  EXPECT_EQ(frame.payload[thoom::kMaxPayload - 1], 0);
}

TEST(Frame, DecodeKeepsVersionKindAndFlags) {
  const std::vector<uint8_t> input = {0xde, 0xbe, 0xad, 7};
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.version, 0xde);
  EXPECT_EQ(frame.kind, 0xbe);
  EXPECT_EQ(frame.flags, 0xad);
  EXPECT_EQ(frame.size, 1u);
  EXPECT_EQ(frame.payload[0], 7);
}

TEST(Frame, DecodeReturnsPayloadView) {
  std::vector<uint8_t> input = {1, 2, 3, 'a', 'b'};
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.payload, input.data() + thoom::kFrameHeaderSize);
  input[3] = 'z';
  EXPECT_EQ(frame.payload[0], 'z');
}

TEST(Frame, DecodeTreatsTrailingBytesAsPayload) {
  const std::vector<uint8_t> input = {1, thoom::kFrameHello, 0, 0x00, 0x01};
  Frame frame = {};

  ASSERT_TRUE(thoom::decode_frame(input.data(),
                                  static_cast<uint32_t>(input.size()), &frame));

  EXPECT_EQ(frame.size, 2u);
}

TEST(Frame, RoundTrips) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  std::vector<uint8_t> bytes;

  ASSERT_TRUE(thoom::encode_frame(
      9, 8, 7, payload.data(), static_cast<uint32_t>(payload.size()), &bytes));

  Frame frame = {};
  ASSERT_TRUE(thoom::decode_frame(bytes.data(),
                                  static_cast<uint32_t>(bytes.size()), &frame));

  EXPECT_EQ(frame.version, 9);
  EXPECT_EQ(frame.kind, 8);
  EXPECT_EQ(frame.flags, 7);
  EXPECT_EQ(frame.size, payload.size());
  EXPECT_EQ(frame.payload[0], 1);
  EXPECT_EQ(frame.payload[1], 2);
  EXPECT_EQ(frame.payload[2], 3);
  EXPECT_EQ(frame.payload[3], 4);
}

TEST(Frame, DecodeRejectsNullFrame) {
  const std::vector<uint8_t> input = {1, 2, 3, 4};

  EXPECT_FALSE(thoom::decode_frame(
      input.data(), static_cast<uint32_t>(input.size()), nullptr));
  EXPECT_FALSE(thoom::decode_frame(nullptr, 0, nullptr));
}

TEST(Frame, DecodeRejectsLyingSize) {
  const std::vector<uint8_t> input = {1, 2, 3};
  Frame frame = {};

  EXPECT_FALSE(thoom::decode_frame(input.data(), UINT32_MAX, &frame));
}

TEST(Frame, EncodeHandlesOutputAliasingPayload) {
  std::vector<uint8_t> bytes = {0x11, 0x22, 0x33};

  ASSERT_TRUE(thoom::encode_frame(1, 2, 3, bytes.data() + 1, 2, &bytes));

  EXPECT_EQ(bytes, std::vector<uint8_t>({1, 2, 3, 0x22, 0x33}));
}

TEST(Frame, EncodeRejectsAliasedPayloadBeyondOutput) {
  std::vector<uint8_t> bytes = {0x11, 0x22, 0x33};

  EXPECT_FALSE(thoom::encode_frame(1, 2, 3, bytes.data() + 1, 5, &bytes));
  EXPECT_EQ(bytes, std::vector<uint8_t>({0x11, 0x22, 0x33}));
}

TEST(Frame, EncodeHandlesWholeOutputAliasingPayload) {
  std::vector<uint8_t> bytes = {0xaa, 0xbb};

  ASSERT_TRUE(thoom::encode_frame(1, 2, 3, bytes.data(), 2, &bytes));

  EXPECT_EQ(bytes, std::vector<uint8_t>({1, 2, 3, 0xaa, 0xbb}));
}

TEST(Frame, EncodeRejectsOversizedWithoutReadingPayload) {
  const uint8_t payload = 0x5a;
  std::vector<uint8_t> bytes = {0x99};

  EXPECT_FALSE(
      thoom::encode_frame(1, 2, 3, &payload, thoom::kMaxPayload + 1, &bytes));
  EXPECT_EQ(bytes, std::vector<uint8_t>({0x99}));
}

TEST(Frame, EncodeAcceptsMaxPayload) {
  const std::vector<uint8_t> payload(thoom::kMaxPayload, 0x5a);
  std::vector<uint8_t> bytes;

  ASSERT_TRUE(thoom::encode_frame(
      1, 2, 3, payload.data(), static_cast<uint32_t>(payload.size()), &bytes));

  EXPECT_EQ(bytes.size(),
            static_cast<size_t>(thoom::kFrameHeaderSize) + thoom::kMaxPayload);
  EXPECT_EQ(bytes[thoom::kFrameHeaderSize], 0x5a);
  EXPECT_EQ(bytes.back(), 0x5a);
}

TEST(Frame, SurvivesRepeatedFailures) {
  const std::vector<uint8_t> short_input = {1, 2};
  const uint8_t payload = 1;
  std::vector<uint8_t> bytes;

  for (int i = 0; i < 1000; i++) {
    Frame frame = {};
    EXPECT_FALSE(thoom::decode_frame(short_input.data(), 2, &frame));
    EXPECT_FALSE(thoom::decode_frame(short_input.data(), 2, nullptr));
    EXPECT_FALSE(
        thoom::encode_frame(1, 2, 3, &payload, thoom::kMaxPayload + 1, &bytes));
    EXPECT_TRUE(bytes.empty());
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
