#include <gtest/gtest.h>

#include "common/error.h"
#include "common/types.h"

TEST(ConnectionRefTest, GenerationParticipatesInIdentity) {
  chatroom::ConnectionRef first{7, 1};
  chatroom::ConnectionRef reused{7, 2};
  EXPECT_NE(first, reused);
  EXPECT_EQ(first, (chatroom::ConnectionRef{7, 1}));
}

TEST(ErrorTest, UsesStableWireNames) {
  EXPECT_EQ(chatroom::ToString(chatroom::ErrorCode::kServerBusy), "SERVER_BUSY");
  EXPECT_EQ(chatroom::ToString(chatroom::ErrorCode::kDbError), "DB_ERROR");
}
