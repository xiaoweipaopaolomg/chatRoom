#include <gtest/gtest.h>

#include "protocol/message.h"

using namespace chatroom;
using namespace chatroom::protocol;

TEST(MessageTest, ParsesSendChat) {
  auto parsed = ParseCommand(
      R"({"type":"chat.send","request_id":"42","payload":{"text":"hello"}})");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->type, CommandType::kSendChat);
  EXPECT_EQ(parsed->payload.at("text"), "hello");
}

TEST(MessageTest, RejectsMissingRequestId) {
  auto parsed = ParseCommand(R"({"type":"ping","payload":{}})");
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().code, ErrorCode::kInvalidMessage);
}

TEST(MessageTest, DistinguishesUnknownTypeAndOversizedChat) {
  auto unknown = ParseCommand(R"({"type":"nope","request_id":"1","payload":{}})");
  ASSERT_FALSE(unknown);
  EXPECT_EQ(unknown.error().code, ErrorCode::kUnsupportedType);

  nlohmann::json input{{"type", "chat.send"}, {"request_id", "2"},
                       {"payload", {{"text", std::string(4097, 'x')}}}};
  auto oversized = ParseCommand(input.dump());
  ASSERT_FALSE(oversized);
  EXPECT_EQ(oversized.error().code, ErrorCode::kMessageTooLarge);
}

TEST(MessageTest, ErrorEnvelopePreservesRequestId) {
  const auto json = MakeError("42", {ErrorCode::kNotInRoom, "join a room first"});
  EXPECT_EQ(json.at("type"), "response.error");
  EXPECT_EQ(json.at("request_id"), "42");
  EXPECT_EQ(json.at("error").at("code"), "NOT_IN_ROOM");
}
