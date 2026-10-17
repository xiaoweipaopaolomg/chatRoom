#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "protocol/frame_codec.h"

using chatroom::protocol::EncodeFrame;
using chatroom::protocol::FrameDecoder;

TEST(FrameDecoderTest, ReassemblesAcrossReads) {
  FrameDecoder decoder;
  const auto bytes = EncodeFrame(R"({"type":"ping"})");
  EXPECT_TRUE(decoder.Push(std::string_view(bytes).substr(0, 2)).frames.empty());
  EXPECT_TRUE(decoder.Push(std::string_view(bytes).substr(2, 5)).frames.empty());
  auto output = decoder.Push(std::string_view(bytes).substr(7));
  ASSERT_EQ(output.frames.size(), 1U);
  EXPECT_EQ(output.frames.front(), R"({"type":"ping"})");
}

TEST(FrameDecoderTest, EmitsCoalescedFrames) {
  FrameDecoder decoder;
  auto output = decoder.Push(EncodeFrame("one") + EncodeFrame("two"));
  EXPECT_EQ(output.frames, (std::vector<std::string>{"one", "two"}));
}

TEST(FrameDecoderTest, AllowsMaximumBodyAndPreservesPartialNextFrame) {
  FrameDecoder decoder;
  const std::string maximum(65'536, 'x');
  const auto next = EncodeFrame("next");
  auto output = decoder.Push(EncodeFrame(maximum) + next.substr(0, 5));
  ASSERT_EQ(output.frames.size(), 1U);
  EXPECT_EQ(output.frames.front().size(), maximum.size());
  EXPECT_EQ(decoder.buffered_bytes(), 5U);
  output = decoder.Push(next.substr(5));
  ASSERT_EQ(output.frames.size(), 1U);
  EXPECT_EQ(output.frames.front(), "next");
}

TEST(FrameDecoderTest, RejectsZeroAndOversizedLengthsPermanently) {
  FrameDecoder zero;
  auto output = zero.Push(std::string(4, '\0'));
  EXPECT_TRUE(output.error.has_value());
  EXPECT_TRUE(zero.failed());
  EXPECT_TRUE(zero.Push(EncodeFrame("ignored")).frames.empty());

  FrameDecoder oversized;
  std::string header{"\0\1\0\1", 4};
  output = oversized.Push(header);
  EXPECT_TRUE(output.error.has_value());
  EXPECT_TRUE(oversized.failed());
}
