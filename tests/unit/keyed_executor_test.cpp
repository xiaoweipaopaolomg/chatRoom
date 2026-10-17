#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "common/keyed_executor.h"

using chatroom::KeyedExecutor;

TEST(KeyedExecutorTest, SameKeyRunsInFifoOrder) {
  KeyedExecutor executor(2, 8);
  std::mutex mutex;
  std::vector<int> order;
  std::promise<void> finished;
  for (int value = 0; value < 3; ++value) {
    ASSERT_TRUE(executor.Submit(77, [&, value] {
      std::lock_guard<std::mutex> lock(mutex);
      order.push_back(value);
      if (order.size() == 3) finished.set_value();
    }));
  }
  EXPECT_EQ(finished.get_future().wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  executor.Shutdown();
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
}

TEST(KeyedExecutorTest, DifferentLanesCanRunConcurrently) {
  KeyedExecutor executor(2, 2);
  std::mutex mutex;
  std::condition_variable ready;
  int entered = 0;
  std::promise<void> both_entered;
  auto task = [&] {
    std::unique_lock<std::mutex> lock(mutex);
    ++entered;
    if (entered == 2) both_entered.set_value();
    ready.wait(lock, [&] { return entered == 2; });
    ready.notify_all();
  };
  ASSERT_TRUE(executor.Submit(0, task));
  ASSERT_TRUE(executor.Submit(1, task));
  EXPECT_EQ(both_entered.get_future().wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  ready.notify_all();
  executor.Shutdown();
}

TEST(KeyedExecutorTest, ReportsTaskExceptionsAndSurvives) {
  std::promise<void> reported;
  std::promise<void> survived;
  KeyedExecutor executor(1, 2, [&](std::exception_ptr error) {
    EXPECT_NE(error, nullptr);
    reported.set_value();
  });
  ASSERT_TRUE(executor.Submit(0, [] { throw std::runtime_error("boom"); }));
  ASSERT_TRUE(executor.Submit(0, [&] { survived.set_value(); }));
  EXPECT_EQ(reported.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_EQ(survived.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready);
}
