#include "persistence/mysql_chat_repository.h"

#include <memory>
#include <string>

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

namespace chatroom {
namespace {

RepositoryException Convert(const sql::SQLException& error) {
  const std::string state = error.getSQLState();
  return RepositoryException(ErrorCode::kDbError,
      "MySQL operation failed (SQLSTATE " + state + ", vendor " +
      std::to_string(error.getErrorCode()) + ")");
}

template <typename Function>
auto WithLease(MySqlConnectionPool& pool, Function&& function)
    -> decltype(function(std::declval<MySqlConnectionPool::Lease&>())) {
  auto lease = pool.Acquire();
  try {
    return function(lease);
  } catch (const sql::SQLException& error) {
    if (error.getErrorCode() == 2006 || error.getErrorCode() == 2013) lease.MarkBroken();
    throw Convert(error);
  }
}

}  // namespace

RoomRecord MySqlChatRepository::CreateRoomWithJoin(std::string_view name,
                                                    std::string_view creator) {
  return WithLease(pool_, [&](auto& lease) {
    lease->setAutoCommit(false);
    try {
      std::unique_ptr<sql::PreparedStatement> room(lease->prepareStatement(
          "INSERT INTO rooms(name, creator_nickname) VALUES (?, ?)"));
      room->setString(1, std::string(name));
      room->setString(2, std::string(creator));
      room->executeUpdate();
      std::unique_ptr<sql::Statement> id_query(lease->createStatement());
      std::unique_ptr<sql::ResultSet> ids(id_query->executeQuery("SELECT LAST_INSERT_ID()"));
      ids->next();
      const auto room_id = static_cast<RoomId>(ids->getUInt64(1));
      std::unique_ptr<sql::PreparedStatement> membership(lease->prepareStatement(
          "INSERT INTO membership_events(room_id, nickname, event_type) VALUES (?, ?, 'JOIN')"));
      membership->setUInt64(1, room_id);
      membership->setString(2, std::string(creator));
      membership->executeUpdate();
      lease->commit();
      lease->setAutoCommit(true);
      return RoomRecord{room_id, std::string(name), std::string(creator)};
    } catch (...) {
      try { lease->rollback(); lease->setAutoCommit(true); } catch (...) { lease.MarkBroken(); }
      throw;
    }
  });
}

bool MySqlChatRepository::RoomExists(RoomId room_id) {
  return WithLease(pool_, [&](auto& lease) {
    std::unique_ptr<sql::PreparedStatement> query(lease->prepareStatement(
        "SELECT 1 FROM rooms WHERE id=? AND status='ACTIVE'"));
    query->setUInt64(1, room_id);
    std::unique_ptr<sql::ResultSet> rows(query->executeQuery());
    return rows->next();
  });
}

MembershipRecord MySqlChatRepository::AppendMembership(
    RoomId room_id, std::string_view nickname, MembershipEventType type) {
  return WithLease(pool_, [&](auto& lease) {
    std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
        "INSERT INTO membership_events(room_id, nickname, event_type) VALUES (?, ?, ?)"));
    statement->setUInt64(1, room_id);
    statement->setString(2, std::string(nickname));
    statement->setString(3, type == MembershipEventType::kJoin ? "JOIN" : "LEAVE");
    statement->executeUpdate();
    std::unique_ptr<sql::Statement> id_query(lease->createStatement());
    std::unique_ptr<sql::ResultSet> ids(id_query->executeQuery("SELECT LAST_INSERT_ID()"));
    ids->next();
    return MembershipRecord{static_cast<std::uint64_t>(ids->getUInt64(1)), room_id,
                            std::string(nickname), type};
  });
}

MessageRecord MySqlChatRepository::AppendMessage(
    RoomId room_id, std::string_view sender, std::string_view content) {
  return WithLease(pool_, [&](auto& lease) {
    std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
        "INSERT INTO messages(room_id, sender_nickname, content) VALUES (?, ?, ?)"));
    statement->setUInt64(1, room_id);
    statement->setString(2, std::string(sender));
    statement->setString(3, std::string(content));
    statement->executeUpdate();
    std::unique_ptr<sql::Statement> id_query(lease->createStatement());
    std::unique_ptr<sql::ResultSet> ids(id_query->executeQuery("SELECT LAST_INSERT_ID()"));
    ids->next();
    return MessageRecord{static_cast<MessageId>(ids->getUInt64(1)), room_id,
                         std::string(sender), std::string(content)};
  });
}

std::vector<RoomRecord> MySqlChatRepository::LoadActiveRooms() {
  return WithLease(pool_, [&](auto& lease) {
    std::unique_ptr<sql::Statement> statement(lease->createStatement());
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery(
        "SELECT id, name, creator_nickname FROM rooms WHERE status='ACTIVE' ORDER BY id"));
    std::vector<RoomRecord> result;
    while (rows->next()) {
      result.push_back({static_cast<RoomId>(rows->getUInt64(1)), rows->getString(2),
                        rows->getString(3)});
    }
    return result;
  });
}

void MySqlChatRepository::ValidateSchema() {
  WithLease(pool_, [&](auto& lease) {
    std::unique_ptr<sql::PreparedStatement> query(lease->prepareStatement(
        "SELECT COUNT(*) FROM schema_versions WHERE version=1"));
    std::unique_ptr<sql::ResultSet> rows(query->executeQuery());
    if (!rows->next() || rows->getUInt(1) != 1) {
      throw RepositoryException(ErrorCode::kDbError, "required schema version 1 is missing");
    }
    return 0;
  });
}

}  // namespace chatroom
