#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "persistence/chat_repository.h"
#include "persistence/mysql_connection_pool.h"

namespace chatroom {

struct MySqlMessageBatchConfig {
  std::size_t writer_count{0};  // Zero keeps the original per-message autocommit path.
  std::size_t max_batch_size{32};
  std::size_t queue_capacity{1024};
  std::chrono::milliseconds max_wait{2};
};

class MySqlChatRepository final : public IChatRepository {
 public:
  struct MessageMetrics {
    std::uint64_t attempts{0};
    std::uint64_t failures{0};
    std::uint64_t total_us{0};
    std::uint64_t insert_us{0};
    std::uint64_t id_query_us{0};
    std::uint64_t batches{0};
    std::uint64_t commit_us{0};
  };

  explicit MySqlChatRepository(MySqlConnectionPool& pool,
                               MySqlMessageBatchConfig batch_config = {});
  ~MySqlChatRepository() override;
  RoomRecord CreateRoomWithJoin(std::string_view name,
                                std::string_view creator) override;
  bool RoomExists(RoomId room_id) override;
  MembershipRecord AppendMembership(RoomId room_id, std::string_view nickname,
                                    MembershipEventType type) override;
  MessageRecord AppendMessage(RoomId room_id, std::string_view sender,
                              std::string_view content) override;
  std::vector<RoomRecord> LoadActiveRooms() override;
  void ValidateSchema();
  MessageMetrics GetMessageMetrics() const noexcept;

 private:
  struct BatchImpl;
  std::vector<MessageRecord> WriteBatch(
      const std::vector<std::tuple<RoomId, std::string, std::string>>& entries);
  MySqlConnectionPool& pool_;
  std::unique_ptr<BatchImpl> batch_;
  std::atomic<std::uint64_t> message_attempts_{0};
  std::atomic<std::uint64_t> message_failures_{0};
  std::atomic<std::uint64_t> message_total_us_{0};
  std::atomic<std::uint64_t> message_insert_us_{0};
  std::atomic<std::uint64_t> message_id_query_us_{0};
  std::atomic<std::uint64_t> message_batches_{0};
  std::atomic<std::uint64_t> message_commit_us_{0};
};

}  // namespace chatroom
