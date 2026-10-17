#include <cstdlib>
#include <memory>
#include <string>

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
  EXPECT_FALSE(loaded.empty());
}

TEST_F(MySqlRepositoryTest, PersistsUnicodeMessageAndMembership) {
  const auto room = repository.CreateRoomWithJoin("中文房间", "alice");
  const auto join = repository.AppendMembership(room.id, "小明", MembershipEventType::kJoin);
  const auto message = repository.AppendMessage(room.id, "小明", "你好，C++");
  EXPECT_GT(join.id, 0U);
  EXPECT_GT(message.id, 0U);
  EXPECT_EQ(message.content, "你好，C++");
}

TEST_F(MySqlRepositoryTest, ForeignKeyFailureHasTypedError) {
  try {
    (void)repository.AppendMessage(999999999, "nobody", "missing room");
    FAIL() << "expected repository error";
  } catch (const RepositoryException& error) {
    EXPECT_EQ(error.code(), ErrorCode::kDbError);
  }
}

}  // namespace
}  // namespace chatroom
