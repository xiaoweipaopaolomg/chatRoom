#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <gtest/gtest.h>

#include "persistence/mysql_chat_repository.h"

namespace chatroom {
namespace {

MySqlPoolConfig TestConfig() {
  MySqlPoolConfig config;
  config.host = "127.0.0.1";
  config.port = 33067;
  config.database = "chatroom";
  config.user = "chatroom";
  config.password = "chatroom_test_only";
  config.pool_size = 2;
  if (const char* value = std::getenv("CHATROOM_TEST_DB_HOST")) config.host = value;
  if (const char* value = std::getenv("CHATROOM_TEST_DB_PORT"))
    config.port = static_cast<std::uint16_t>(std::stoul(value));
  if (const char* value = std::getenv("CHATROOM_TEST_DB_PASSWORD")) config.password = value;
  return config;
}

class MySqlRepositoryTest : public ::testing::Test {
 protected:
  MySqlRepositoryTest() : pool(TestConfig()), repository(pool) {
    repository.ValidateSchema();
  }
  MySqlConnectionPool pool;
  MySqlChatRepository repository;
};

TEST_F(MySqlRepositoryTest, CreateRoomAndJoinAreOneTransaction) {
  const auto room = repository.CreateRoomWithJoin("C++", "alice");
  EXPECT_TRUE(repository.RoomExists(room.id));
  const auto loaded = repository.LoadActiveRooms();
  const auto found = std::find_if(loaded.begin(), loaded.end(),
      [&](const RoomRecord& record) { return record.id == room.id; });
  ASSERT_NE(found, loaded.end());
  EXPECT_EQ(found->name, "C++");
}

TEST_F(MySqlRepositoryTest, PersistsUnicodeMessageAndMembership) {
  const auto room = repository.CreateRoomWithJoin("中文房间", "alice");
  const auto join = repository.AppendMembership(room.id, "小明", MembershipEventType::kJoin);
  const auto message = repository.AppendMessage(room.id, "小明", "你好，C++");
  EXPECT_GT(join.id, 0U);
  EXPECT_GT(message.id, 0U);
  EXPECT_EQ(message.content, "你好，C++");
  const auto metrics = repository.GetMessageMetrics();
  EXPECT_EQ(metrics.attempts, 1U);
  EXPECT_EQ(metrics.failures, 0U);
  EXPECT_GE(metrics.total_us, metrics.insert_us + metrics.id_query_us);
  const auto leave = repository.AppendMembership(room.id, "小明", MembershipEventType::kLeave);
  EXPECT_GT(leave.id, join.id);
}

TEST(MySqlPoolTest, AcquireTimesOutWhenAllConnectionsAreLeased) {
  auto config = TestConfig();
  config.pool_size = 1;
  config.acquire_timeout = std::chrono::milliseconds(20);
  MySqlConnectionPool pool(config);
  auto held = pool.Acquire();
  try {
    (void)pool.Acquire();
    FAIL() << "expected DB_BUSY";
  } catch (const RepositoryException& error) {
    EXPECT_EQ(error.code(), ErrorCode::kDbBusy);
  }
}

TEST(MySqlPoolTest, MarkedBrokenLeaseIsReplaced) {
  auto config = TestConfig();
  config.pool_size = 1;
  MySqlConnectionPool pool(config);
  {
    auto lease = pool.Acquire();
    lease.MarkBroken();
  }
  auto replacement = pool.Acquire();
  EXPECT_TRUE(static_cast<bool>(replacement));
}

TEST_F(MySqlRepositoryTest, ForeignKeyFailureHasTypedError) {
  try {
    (void)repository.AppendMessage(999999999, "nobody", "missing room");
    FAIL() << "expected repository error";
  } catch (const RepositoryException& error) {
    EXPECT_EQ(error.code(), ErrorCode::kDbError);
  }
  const auto metrics = repository.GetMessageMetrics();
  EXPECT_EQ(metrics.attempts, 1U);
  EXPECT_EQ(metrics.failures, 1U);
}

TEST(MySqlBatchTest, ConcurrentMessagesHaveDistinctDurableIds) {
  auto config = TestConfig();
  config.pool_size = 4;
  MySqlConnectionPool pool(config);
  MySqlMessageBatchConfig batch_config;
  batch_config.writer_count = 2;
  batch_config.max_batch_size = 16;
  batch_config.max_wait = std::chrono::milliseconds(20);
  MySqlChatRepository repository(pool, batch_config);
  repository.ValidateSchema();
  const auto room = repository.CreateRoomWithJoin("batch room", "batch owner");
  std::vector<std::future<MessageRecord>> pending;
  for (int i = 0; i < 32; ++i) {
    pending.push_back(std::async(std::launch::async, [&repository, room, i] {
      return repository.AppendMessage(room.id, "batch sender", "message " + std::to_string(i));
    }));
  }
  std::set<MessageId> ids;
  std::vector<MessageRecord> records;
  for (auto& result : pending) {
    const auto record = result.get();
    EXPECT_EQ(record.room_id, room.id);
    EXPECT_TRUE(ids.insert(record.id).second);
    records.push_back(record);
  }
  EXPECT_EQ(ids.size(), 32U);
  const auto metrics = repository.GetMessageMetrics();
  EXPECT_EQ(metrics.attempts, 32U);
  EXPECT_EQ(metrics.failures, 0U);
  EXPECT_GT(metrics.batches, 0U);
  EXPECT_LT(metrics.batches, 32U);

  auto lease = pool.Acquire();
  std::unique_ptr<sql::PreparedStatement> count(lease->prepareStatement(
      "SELECT COUNT(*) FROM messages WHERE room_id=?"));
  count->setUInt64(1, room.id);
  std::unique_ptr<sql::ResultSet> rows(count->executeQuery());
  ASSERT_TRUE(rows->next());
  EXPECT_EQ(rows->getUInt64(1), 32U);
  std::unique_ptr<sql::PreparedStatement> lookup(lease->prepareStatement(
      "SELECT content FROM messages WHERE id=?"));
  for (const auto& record : records) {
    lookup->setUInt64(1, record.id);
    std::unique_ptr<sql::ResultSet> stored(lookup->executeQuery());
    ASSERT_TRUE(stored->next());
    EXPECT_EQ(stored->getString(1), record.content);
  }
}

TEST(MySqlBatchTest, InvalidForeignKeyDoesNotPoisonSubsequentBatch) {
  MySqlConnectionPool pool(TestConfig());
  MySqlMessageBatchConfig batch_config;
  batch_config.writer_count = 1;
  MySqlChatRepository repository(pool, batch_config);
  repository.ValidateSchema();
  EXPECT_THROW(repository.AppendMessage(999999999, "nobody", "invalid"), RepositoryException);
  const auto room = repository.CreateRoomWithJoin("after rollback", "owner");
  EXPECT_GT(repository.AppendMessage(room.id, "owner", "valid").id, 0U);
  const auto metrics = repository.GetMessageMetrics();
  EXPECT_EQ(metrics.attempts, 2U);
  EXPECT_EQ(metrics.failures, 1U);
  EXPECT_EQ(metrics.batches, 1U);
}

}  // namespace
}  // namespace chatroom
