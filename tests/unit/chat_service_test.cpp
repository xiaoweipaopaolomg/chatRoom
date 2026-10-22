#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "application/chat_service.h"
#include "persistence/in_memory_chat_repository.h"
#include "protocol/frame_codec.h"

using namespace chatroom;

namespace {

protocol::Command Parse(std::string json) {
  auto parsed = protocol::ParseCommand(json);
  EXPECT_TRUE(parsed.has_value());
  return parsed.value();
}

class ChatServiceTest : public ::testing::Test {
 protected:
  ChatServiceTest() : service(rooms, repository) {}

  OutboundBatch Hello(ConnectionRef connection, std::string nickname,
                      std::string request_id = "hello") {
    nlohmann::json json{{"type", "session.hello"}, {"request_id", request_id},
                        {"payload", {{"nickname", std::move(nickname)}}}};
    return service.Handle(connection, Parse(json.dump()));
  }

  RoomId Create(ConnectionRef connection, std::string name = "C++") {
    nlohmann::json json{{"type", "room.create"}, {"request_id", "create"},
                        {"payload", {{"name", std::move(name)}}}};
    auto output = service.Handle(connection, Parse(json.dump()));
    EXPECT_EQ(output.size(), 1U);
    EXPECT_EQ(output.front().message->at("type"), "room.snapshot");
    return output.front().message->at("room").at("id").get<RoomId>();
  }

  OutboundBatch Join(ConnectionRef connection, RoomId room_id) {
    nlohmann::json json{{"type", "room.join"}, {"request_id", "join"},
                        {"payload", {{"room_id", room_id}}}};
    return service.Handle(connection, Parse(json.dump()));
  }

  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service;
};

std::vector<ConnectionRef> Recipients(const OutboundBatch& output) {
  std::vector<ConnectionRef> result;
  for (const auto& item : output) result.push_back(item.recipient);
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace

class GatedChatRepository final : public IChatRepository {
 public:
  RoomRecord CreateRoomWithJoin(std::string_view name, std::string_view creator) override {
    return delegate_.CreateRoomWithJoin(name, creator);
  }
  bool RoomExists(RoomId room_id) override { return delegate_.RoomExists(room_id); }
  MembershipRecord AppendMembership(RoomId room_id, std::string_view nickname,
                                    MembershipEventType type) override {
    return delegate_.AppendMembership(room_id, nickname, type);
  }
  MessageRecord AppendMessage(RoomId room_id, std::string_view sender,
                              std::string_view content) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ++entered_;
      changed_.notify_all();
      changed_.wait(lock, [this] { return released_; });
    }
    return delegate_.AppendMessage(room_id, sender, content);
  }
  std::vector<RoomRecord> LoadActiveRooms() override { return delegate_.LoadActiveRooms(); }
  bool WaitForConcurrentMessages() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(2),
                             [this] { return entered_ == 2; });
  }
  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

 private:
  InMemoryChatRepository delegate_;
  std::mutex mutex_;
  std::condition_variable changed_;
  int entered_{0};
  bool released_{false};
};

TEST(ChatServiceConcurrencyTest, DifferentSendersCanPersistInSameRoomConcurrently) {
  RoomManager rooms;
  GatedChatRepository repository;
  ChatService service(rooms, repository);
  const ConnectionRef alice{1, 1};
  const ConnectionRef bob{2, 1};
  service.Handle(alice, Parse(R"({"type":"session.hello","request_id":"h1","payload":{"nickname":"alice"}})"));
  service.Handle(bob, Parse(R"({"type":"session.hello","request_id":"h2","payload":{"nickname":"bob"}})"));
  const auto created = service.Handle(alice, Parse(
      R"({"type":"room.create","request_id":"c","payload":{"name":"room"}})"));
  ASSERT_EQ(created.size(), 1U);
  const auto room_id = created[0].message->at("room").at("id").get<RoomId>();
  service.Handle(bob, Parse(nlohmann::json{{"type", "room.join"}, {"request_id", "j"},
      {"payload", {{"room_id", room_id}}}}.dump()));
  auto first = std::async(std::launch::async, [&] {
    return service.Handle(alice, Parse(
        R"({"type":"chat.send","request_id":"a","payload":{"text":"one"}})"));
  });
  auto second = std::async(std::launch::async, [&] {
    return service.Handle(bob, Parse(
        R"({"type":"chat.send","request_id":"b","payload":{"text":"two"}})"));
  });
  const bool concurrent = repository.WaitForConcurrentMessages();
  repository.Release();
  EXPECT_TRUE(concurrent);
  EXPECT_EQ(first.get().size(), 2U);
  EXPECT_EQ(second.get().size(), 2U);
}

TEST_F(ChatServiceTest, HelloAndPingDoNotTouchRepository) {
  const ConnectionRef alice{1, 1};
  auto output = Hello(alice, "alice", "1");
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().message->at("type"), "response.ok");

  auto ping = Parse(R"({"type":"ping","request_id":"2","payload":{"client_time":123}})");
  output = service.Handle(alice, ping);
  EXPECT_EQ(output.front().message->at("type"), "pong");
  EXPECT_EQ(output.front().message->at("client_time"), 123);
  EXPECT_TRUE(repository.Rooms().empty());
}

TEST_F(ChatServiceTest, PersistedChatBroadcastsToEveryMember) {
  const ConnectionRef alice{1, 1};
  const ConnectionRef bob{2, 1};
  Hello(alice, "alice");
  const auto room_id = Create(alice);
  Hello(bob, "bob");
  Join(bob, room_id);
  auto command = Parse(R"({"type":"chat.send","request_id":"m1","payload":{"text":"hello"}})");
  const auto output = service.Handle(alice, command);
  EXPECT_EQ(repository.Messages().size(), 1U);
  EXPECT_EQ(Recipients(output), (std::vector<ConnectionRef>{alice, bob}));
  for (const auto& item : output) EXPECT_EQ(item.message->at("type"), "chat.message");
  EXPECT_EQ(output[0].message, output[1].message);
  ASSERT_TRUE(output[0].encoded_frame);
  EXPECT_EQ(output[0].encoded_frame, output[1].encoded_frame);
  EXPECT_EQ(*output[0].encoded_frame, protocol::EncodeFrame(output[0].message->dump()));
}

TEST_F(ChatServiceTest, FailedCreateDoesNotCreateMemoryRoom) {
  const ConnectionRef alice{1, 1};
  Hello(alice, "alice");
  repository.FailNext(InMemoryChatRepository::Operation::kCreateRoomWithJoin,
                      ErrorCode::kDbError);
  nlohmann::json json{{"type", "room.create"}, {"request_id", "c1"},
                      {"payload", {{"name", "broken"}}}};
  const auto output = service.Handle(alice, Parse(json.dump()));
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().message->at("type"), "response.error");
  EXPECT_EQ(output.front().message->at("request_id"), "c1");
  EXPECT_EQ(rooms.RoomCount(), 0U);
  EXPECT_FALSE(rooms.RoomFor(alice).has_value());
}

TEST_F(ChatServiceTest, FailedJoinAndChatHaveNoVisibleSideEffects) {
  const ConnectionRef alice{1, 1};
  const ConnectionRef bob{2, 1};
  Hello(alice, "alice");
  const auto room_id = Create(alice);
  Hello(bob, "bob");
  repository.FailNext(InMemoryChatRepository::Operation::kAppendMembership,
                      ErrorCode::kDbBusy);
  auto output = Join(bob, room_id);
  EXPECT_EQ(output.front().message->at("error").at("code"), "DB_BUSY");
  EXPECT_FALSE(rooms.RoomFor(bob).has_value());

  repository.FailNext(InMemoryChatRepository::Operation::kAppendMessage,
                      ErrorCode::kDbError);
  auto chat = Parse(R"({"type":"chat.send","request_id":"m1","payload":{"text":"lost"}})");
  output = service.Handle(alice, chat);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().message->at("type"), "response.error");
  EXPECT_TRUE(repository.Messages().empty());
}

TEST_F(ChatServiceTest, FailedExplicitLeaveKeepsMembership) {
  const ConnectionRef alice{1, 1};
  Hello(alice, "alice");
  const auto room_id = Create(alice);
  repository.FailNext(InMemoryChatRepository::Operation::kAppendMembership,
                      ErrorCode::kDbError);
  auto leave = Parse(R"({"type":"room.leave","request_id":"leave","payload":{}})");
  const auto output = service.Handle(alice, leave);
  EXPECT_EQ(output.front().message->at("type"), "response.error");
  EXPECT_EQ(rooms.RoomFor(alice), room_id);
}

TEST_F(ChatServiceTest, DisconnectRemovesMemberEvenWhenPersistenceFails) {
  const ConnectionRef alice{1, 1};
  const ConnectionRef bob{2, 1};
  Hello(alice, "alice");
  const auto room_id = Create(alice);
  Hello(bob, "bob");
  Join(bob, room_id);
  repository.FailNext(InMemoryChatRepository::Operation::kAppendMembership,
                      ErrorCode::kDbError);
  const auto output = service.HandleDisconnect(bob);
  EXPECT_FALSE(rooms.FindSession(bob).has_value());
  EXPECT_FALSE(rooms.RoomFor(bob).has_value());
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().recipient, alice);
  EXPECT_EQ(output.front().message->at("type"), "member.left");
}

TEST_F(ChatServiceTest, PreconditionsPreserveRequestId) {
  const ConnectionRef unknown{9, 1};
  auto create = Parse(R"({"type":"room.create","request_id":"exact-id","payload":{"name":"x"}})");
  const auto output = service.Handle(unknown, create);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().message->at("request_id"), "exact-id");
  EXPECT_EQ(output.front().message->at("error").at("code"), "SESSION_REQUIRED");
}

TEST_F(ChatServiceTest, OversizedMemberSnapshotRejectsJoinBeforePersistence) {
  const ConnectionRef owner{1, 1};
  Hello(owner, "owner");
  const auto room_id = Create(owner);
  for (std::uint64_t index = 2; index < 2200; ++index) {
    const ConnectionRef member{index, 1};
    const auto nickname = "member" + std::to_string(index) + std::string(20, 'x');
    ASSERT_TRUE(rooms.RegisterSession(member, nickname));
    ASSERT_TRUE(rooms.Join(room_id, member));
  }
  const ConnectionRef newcomer{2200, 1};
  Hello(newcomer, "newcomer");
  const auto before = repository.MembershipEvents().size();
  const auto output = Join(newcomer, room_id);
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().message->at("error").at("code"), "SERVER_BUSY");
  EXPECT_FALSE(rooms.RoomFor(newcomer));
  EXPECT_EQ(repository.MembershipEvents().size(), before);
}
