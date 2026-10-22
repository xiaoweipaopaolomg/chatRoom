#include <future>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "common/bounded_queue.h"

using chatroom::BoundedQueue;

TEST(BoundedQueueTest, RejectsAtCapacityWithoutBlocking) {
  BoundedQueue<int> queue(1);
  EXPECT_TRUE(queue.TryPush(1));
  EXPECT_FALSE(queue.TryPush(2));
}

TEST(BoundedQueueTest, CloseWakesWaiterAndDrainsExistingValues) {
  BoundedQueue<int> queue(2);
  ASSERT_TRUE(queue.TryPush(7));
  queue.Close();
  EXPECT_EQ(queue.WaitPop(), 7);
  EXPECT_FALSE(queue.WaitPop().has_value());

  BoundedQueue<int> waiting(1);
  auto result = std::async(std::launch::async, [&waiting] { return waiting.WaitPop(); });
  waiting.Close();
  EXPECT_EQ(result.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_FALSE(result.get().has_value());
}

TEST(BoundedQueueTest, WaitPushPreservesItemUntilConsumerMakesSpace) {
  BoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.TryPush(1));
  auto producer = std::async(std::launch::async, [&queue] { return queue.WaitPush(2); });
  EXPECT_EQ(producer.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  EXPECT_EQ(queue.TryPop(), 1);
  EXPECT_EQ(producer.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_TRUE(producer.get());
  EXPECT_EQ(queue.TryPop(), 2);
}

TEST(BoundedQueueTest, CloseWakesBlockedProducer) {
  BoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.TryPush(1));
  auto producer = std::async(std::launch::async, [&queue] { return queue.WaitPush(2); });
  queue.Close();
  EXPECT_EQ(producer.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_FALSE(producer.get());
  EXPECT_EQ(queue.TryPop(), 1);
}
