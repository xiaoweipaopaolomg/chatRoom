#pragma once

#include "persistence/chat_repository.h"
#include "persistence/mysql_connection_pool.h"

namespace chatroom {

class MySqlChatRepository final : public IChatRepository {
 public:
  explicit MySqlChatRepository(MySqlConnectionPool& pool) : pool_(pool) {}
  RoomRecord CreateRoomWithJoin(std::string_view name,
                                std::string_view creator) override;
  bool RoomExists(RoomId room_id) override;
  MembershipRecord AppendMembership(RoomId room_id, std::string_view nickname,
                                    MembershipEventType type) override;
  MessageRecord AppendMessage(RoomId room_id, std::string_view sender,
                              std::string_view content) override;
  std::vector<RoomRecord> LoadActiveRooms() override;
  void ValidateSchema();

 private:
  MySqlConnectionPool& pool_;
};

}  // namespace chatroom
