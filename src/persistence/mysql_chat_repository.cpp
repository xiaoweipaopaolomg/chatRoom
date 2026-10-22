#include "persistence/mysql_chat_repository.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

namespace chatroom {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedMicroseconds(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

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

struct MySqlChatRepository::BatchImpl {
  struct Pending {
    RoomId room_id;
    std::string sender;
    std::string content;
    std::promise<MessageRecord> result;
  };

  BatchImpl(MySqlChatRepository& owner, MySqlMessageBatchConfig value)
      : repository(owner), config(value) {
    if (config.writer_count == 0 || config.max_batch_size == 0 ||
        config.queue_capacity == 0 || config.max_wait.count() < 0)
      throw std::invalid_argument("invalid MySQL message batch configuration");
    try {
      for (std::size_t i = 0; i < config.writer_count; ++i)
        writers.emplace_back([this] { Run(); });
    } catch (...) {
      Stop();
      throw;
    }
  }

  ~BatchImpl() { Stop(); }

  MessageRecord Append(RoomId room_id, std::string_view sender, std::string_view content) {
    auto pending = std::make_shared<Pending>(Pending{room_id, std::string(sender),
                                                     std::string(content), {}});
    auto future = pending->result.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (closing || queue.size() >= config.queue_capacity)
        throw RepositoryException(ErrorCode::kDbBusy, "message batch queue is full");
      queue.push_back(std::move(pending));
    }
    available.notify_one();
    return future.get();
  }

  void Stop() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex);
      closing = true;
    }
    available.notify_all();
    for (auto& writer : writers) if (writer.joinable()) writer.join();
  }

  void Run() {
    for (;;) {
      std::vector<std::shared_ptr<Pending>> batch;
      {
        std::unique_lock<std::mutex> lock(mutex);
        available.wait(lock, [this] { return closing || !queue.empty(); });
        if (queue.empty()) return;
        const auto deadline = Clock::now() + config.max_wait;
        while (!closing && queue.size() < config.max_batch_size &&
               Clock::now() < deadline)
          available.wait_until(lock, deadline, [this] {
            return closing || queue.size() >= config.max_batch_size;
          });
        const auto count = std::min(queue.size(), config.max_batch_size);
        batch.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
          batch.push_back(std::move(queue.front()));
          queue.pop_front();
        }
      }
      try {
        std::vector<std::tuple<RoomId, std::string, std::string>> entries;
        entries.reserve(batch.size());
        for (const auto& item : batch)
          entries.emplace_back(item->room_id, item->sender, item->content);
        auto records = repository.WriteBatch(entries);
        for (std::size_t i = 0; i < batch.size(); ++i)
          batch[i]->result.set_value(std::move(records[i]));
      } catch (...) {
        const auto failure = std::current_exception();
        for (const auto& item : batch) item->result.set_exception(failure);
      }
    }
  }

  MySqlChatRepository& repository;
  MySqlMessageBatchConfig config;
  std::mutex mutex;
  std::condition_variable available;
  std::deque<std::shared_ptr<Pending>> queue;
  std::vector<std::thread> writers;
  bool closing{false};
};

MySqlChatRepository::MySqlChatRepository(MySqlConnectionPool& pool,
                                         MySqlMessageBatchConfig batch_config)
    : pool_(pool) {
  if (batch_config.writer_count != 0)
    batch_ = std::make_unique<BatchImpl>(*this, batch_config);
}

MySqlChatRepository::~MySqlChatRepository() { batch_.reset(); }

std::vector<MessageRecord> MySqlChatRepository::WriteBatch(
    const std::vector<std::tuple<RoomId, std::string, std::string>>& entries) {
  return WithLease(pool_, [&](auto& lease) {
    lease->setAutoCommit(false);
    try {
      // A plain multi-row INSERT reserves one consecutive AUTO_INCREMENT block
      // with InnoDB lock mode 1 (checked at startup). No other statement may
      // interleave an ID into this block, even if it is a bulk insert.
      std::string sql = "INSERT INTO messages(room_id, sender_nickname, content) VALUES ";
      for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) sql += ',';
        sql += "(?,?,?)";
      }
      std::unique_ptr<sql::PreparedStatement> insert(lease->prepareStatement(sql));
      std::unique_ptr<sql::Statement> id_query(lease->createStatement());
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& [room_id, sender, content] = entries[i];
        insert->setUInt64(static_cast<int>(3 * i + 1), room_id);
        insert->setString(static_cast<int>(3 * i + 2), sender);
        insert->setString(static_cast<int>(3 * i + 3), content);
      }
      const auto insert_started = Clock::now();
      if (insert->executeUpdate() != static_cast<int>(entries.size()))
        throw RepositoryException(ErrorCode::kDbError, "MySQL batch inserted an unexpected row count");
      message_insert_us_.fetch_add(ElapsedMicroseconds(insert_started),
                                   std::memory_order_relaxed);
      const auto query_started = Clock::now();
      std::unique_ptr<sql::ResultSet> ids(id_query->executeQuery(
          "SELECT LAST_INSERT_ID(), @@auto_increment_increment"));
      if (!ids->next())
        throw RepositoryException(ErrorCode::kDbError, "MySQL did not return batch message IDs");
      const auto first_id = static_cast<MessageId>(ids->getUInt64(1));
      const auto increment = static_cast<MessageId>(ids->getUInt64(2));
      if (first_id == 0 || increment == 0 ||
          (entries.size() - 1) > (std::numeric_limits<MessageId>::max() - first_id) / increment)
        throw RepositoryException(ErrorCode::kDbError, "invalid MySQL batch message ID range");
      message_id_query_us_.fetch_add(ElapsedMicroseconds(query_started),
                                     std::memory_order_relaxed);
      std::vector<MessageRecord> records;
      records.reserve(entries.size());
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& [room_id, sender, content] = entries[i];
        records.push_back({first_id + i * increment, room_id, sender, content});
      }
      const auto commit_started = Clock::now();
      lease->commit();
      message_commit_us_.fetch_add(ElapsedMicroseconds(commit_started),
                                   std::memory_order_relaxed);
      // The commit has succeeded. A connection-reset failure must not turn a
      // durable write into a reported failure (which would invite a duplicate retry).
      try { lease->setAutoCommit(true); } catch (...) { lease.MarkBroken(); }
      message_batches_.fetch_add(1, std::memory_order_relaxed);
      return records;
    } catch (...) {
      try { lease->rollback(); lease->setAutoCommit(true); } catch (...) { lease.MarkBroken(); }
      throw;
    }
  });
}

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
  message_attempts_.fetch_add(1, std::memory_order_relaxed);
  const auto started = Clock::now();
  try {
    if (batch_) {
      auto record = batch_->Append(room_id, sender, content);
      message_total_us_.fetch_add(ElapsedMicroseconds(started),
                                  std::memory_order_relaxed);
      return record;
    }
    auto record = WithLease(pool_, [&](auto& lease) {
      std::unique_ptr<sql::PreparedStatement> statement(lease->prepareStatement(
          "INSERT INTO messages(room_id, sender_nickname, content) VALUES (?, ?, ?)"));
      statement->setUInt64(1, room_id);
      statement->setString(2, std::string(sender));
      statement->setString(3, std::string(content));
      const auto insert_started = Clock::now();
      statement->executeUpdate();
      message_insert_us_.fetch_add(ElapsedMicroseconds(insert_started),
                                   std::memory_order_relaxed);
      const auto query_started = Clock::now();
      std::unique_ptr<sql::Statement> id_query(lease->createStatement());
      std::unique_ptr<sql::ResultSet> ids(id_query->executeQuery("SELECT LAST_INSERT_ID()"));
      ids->next();
      const auto id = static_cast<MessageId>(ids->getUInt64(1));
      message_id_query_us_.fetch_add(ElapsedMicroseconds(query_started),
                                     std::memory_order_relaxed);
      return MessageRecord{id, room_id, std::string(sender), std::string(content)};
    });
    message_total_us_.fetch_add(ElapsedMicroseconds(started),
                                std::memory_order_relaxed);
    return record;
  } catch (...) {
    message_failures_.fetch_add(1, std::memory_order_relaxed);
    message_total_us_.fetch_add(ElapsedMicroseconds(started),
                                std::memory_order_relaxed);
    throw;
  }
}

MySqlChatRepository::MessageMetrics MySqlChatRepository::GetMessageMetrics() const noexcept {
  return {message_attempts_.load(std::memory_order_relaxed),
          message_failures_.load(std::memory_order_relaxed),
          message_total_us_.load(std::memory_order_relaxed),
          message_insert_us_.load(std::memory_order_relaxed),
          message_id_query_us_.load(std::memory_order_relaxed),
          message_batches_.load(std::memory_order_relaxed),
          message_commit_us_.load(std::memory_order_relaxed)};
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
    if (batch_) {
      std::unique_ptr<sql::Statement> statement(lease->createStatement());
      std::unique_ptr<sql::ResultSet> mode(statement->executeQuery(
          "SELECT @@innodb_autoinc_lock_mode"));
      if (!mode->next() || mode->getInt(1) != 1)
        throw RepositoryException(ErrorCode::kDbError,
            "MySQL message batching requires innodb_autoinc_lock_mode=1");
    }
    return 0;
  });
}

}  // namespace chatroom
