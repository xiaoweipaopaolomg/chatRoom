#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "domain/room_manager.h"

using namespace chatroom;

TEST(RoomManagerTest, NicknamesAreUniqueWhileOnline) {
  RoomManager rooms;
  ASSERT_TRUE(rooms.RegisterSession({1, 1}, "alice"));
  auto result = rooms.RegisterSession({2, 1}, "alice");
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, ErrorCode::kNicknameTaken);
}

TEST(RoomManagerTest, ConnectionCannotJoinTwoRooms) {
  RoomManager rooms;
  ASSERT_TRUE(rooms.CreateLoadedRoom(10, "one"));
  ASSERT_TRUE(rooms.CreateLoadedRoom(11, "two"));
  ASSERT_TRUE(rooms.RegisterSession({1, 1}, "alice"));
  ASSERT_TRUE(rooms.Join(10, {1, 1}));
  auto second = rooms.Join(11, {1, 1});
  ASSERT_FALSE(second);
  EXPECT_EQ(second.error().code, ErrorCode::kAlreadyInRoom);
}

TEST(RoomManagerTest, StaleGenerationCannotRemoveNewSession) {
  RoomManager rooms;
  ASSERT_TRUE(rooms.RegisterSession({9, 2}, "alice"));
  EXPECT_FALSE(rooms.RemoveConnection({9, 1}).has_value());
  EXPECT_TRUE(rooms.FindSession({9, 2}).has_value());
  EXPECT_EQ(rooms.SessionCount(), 1U);
}

TEST(RoomManagerTest, RemoveConnectionIsIdempotentAndReleasesNickname) {
  RoomManager rooms;
  ASSERT_TRUE(rooms.CreateLoadedRoom(1, "room"));
  ASSERT_TRUE(rooms.RegisterSession({1, 1}, "alice"));
  ASSERT_TRUE(rooms.Join(1, {1, 1}));
  ASSERT_TRUE(rooms.RemoveConnection({1, 1}).has_value());
  EXPECT_FALSE(rooms.RemoveConnection({1, 1}).has_value());
  EXPECT_TRUE(rooms.RegisterSession({2, 1}, "alice"));
}

TEST(RoomManagerTest, ConcurrentJoinsMaintainExactIndexes) {
  RoomManager rooms;
  ASSERT_TRUE(rooms.CreateLoadedRoom(1, "one"));
  ASSERT_TRUE(rooms.CreateLoadedRoom(2, "two"));
  constexpr int count = 16;
  for (int index = 0; index < count; ++index) {
    ASSERT_TRUE(rooms.RegisterSession(
        {static_cast<std::uint64_t>(index + 1), 1}, "user" + std::to_string(index)));
  }
  std::mutex mutex;
  std::condition_variable gate;
  int ready = 0;
  bool start = false;
  std::vector<std::thread> workers;
  for (int index = 0; index < count; ++index) {
    workers.emplace_back([&, index] {
      {
        std::unique_lock<std::mutex> lock(mutex);
        ++ready;
        if (ready == count) gate.notify_all();
        gate.wait(lock, [&] { return start; });
      }
      EXPECT_TRUE(rooms.Join(index % 2 == 0 ? 1 : 2,
                             {static_cast<std::uint64_t>(index + 1), 1}));
    });
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    gate.wait(lock, [&] { return ready == count; });
    start = true;
  }
  gate.notify_all();
  for (auto& worker : workers) worker.join();
  EXPECT_EQ(rooms.Members(1).size(), 8U);
  EXPECT_EQ(rooms.Members(2).size(), 8U);
  for (int index = 0; index < count; ++index) {
    EXPECT_EQ(rooms.RoomFor({static_cast<std::uint64_t>(index + 1), 1}),
              index % 2 == 0 ? std::optional<RoomId>{1} : std::optional<RoomId>{2});
  }
}
