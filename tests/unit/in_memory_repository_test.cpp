#include <gtest/gtest.h>

#include "persistence/in_memory_chat_repository.h"

using namespace chatroom;

TEST(InMemoryRepositoryTest, CreateRoomAlsoRecordsCreatorJoin) {
  InMemoryChatRepository repository;
  const auto room = repository.CreateRoomWithJoin("C++", "alice");
  EXPECT_GT(room.id, 0U);
  const auto events = repository.MembershipEvents();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, MembershipEventType::kJoin);
}

TEST(InMemoryRepositoryTest, FailureInjectionLeavesNoPartialRecords) {
  InMemoryChatRepository repository;
  repository.FailNext(InMemoryChatRepository::Operation::kCreateRoomWithJoin,
                      ErrorCode::kDbError);
  try {
    (void)repository.CreateRoomWithJoin("C++", "alice");
    FAIL() << "expected RepositoryException";
  } catch (const RepositoryException& error) {
    EXPECT_EQ(error.code(), ErrorCode::kDbError);
  }
  EXPECT_TRUE(repository.Rooms().empty());
  EXPECT_TRUE(repository.MembershipEvents().empty());

  const auto room = repository.CreateRoomWithJoin("C++", "alice");
  repository.FailNext(InMemoryChatRepository::Operation::kAppendMembership,
                      ErrorCode::kDbBusy);
  EXPECT_THROW(repository.AppendMembership(room.id, "bob", MembershipEventType::kJoin),
               RepositoryException);
  EXPECT_EQ(repository.MembershipEvents().size(), 1U);

  repository.FailNext(InMemoryChatRepository::Operation::kAppendMessage,
                      ErrorCode::kDbError);
  EXPECT_THROW(repository.AppendMessage(room.id, "alice", "hello"), RepositoryException);
  EXPECT_TRUE(repository.Messages().empty());
}

TEST(InMemoryRepositoryTest, IdsAndInsertionOrderAreDeterministic) {
  InMemoryChatRepository repository;
  const auto first = repository.CreateRoomWithJoin("one", "alice");
  const auto second = repository.CreateRoomWithJoin("two", "bob");
  EXPECT_EQ(first.id, 1U);
  EXPECT_EQ(second.id, 2U);
  const auto message = repository.AppendMessage(second.id, "bob", "hi");
  EXPECT_EQ(message.id, 1U);
  EXPECT_EQ(repository.LoadActiveRooms().at(0).name, "one");
}
