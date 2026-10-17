#include <gtest/gtest.h>

#include "server/connection.h"

using namespace chatroom;

TEST(ConnectionTest, PartialWriteRetainsUnsentSuffix) {
  Connection connection(-1, {1, 1}, 1024);
  ASSERT_TRUE(connection.QueueOutput("abcdef"));
  connection.ConsumeWritten(2);
  EXPECT_EQ(connection.PendingOutput(), "cdef");
}

TEST(ConnectionTest, RejectsOutputBeyondLimit) {
  Connection connection(-1, {1, 1}, 4);
  EXPECT_FALSE(connection.QueueOutput("12345"));
  ASSERT_TRUE(connection.QueueOutput("1234"));
  connection.ConsumeWritten(2);
  EXPECT_TRUE(connection.QueueOutput("56"));
  EXPECT_EQ(connection.PendingOutput(), "3456");
}

TEST(ConnectionTest, RejectsOverConsumption) {
  Connection connection(-1, {1, 1}, 8);
  ASSERT_TRUE(connection.QueueOutput("abc"));
  EXPECT_THROW(connection.ConsumeWritten(4), std::out_of_range);
}
