#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "persistence/chat_repository.h"

namespace chatroom {

class InMemoryChatRepository final : public IChatRepository {
 public:
  enum class Operation { kCreateRoomWithJoin, kRoomExists, kAppendMembership,
                         kAppendMessage, kLoadActiveRooms };

  RoomRecord CreateRoomWithJoin(std::string_view name,
                                std::string_view creator) override;
  bool RoomExists(RoomId room_id) override;
  MembershipRecord AppendMembership(RoomId room_id, std::string_view nickname,
                                    MembershipEventType type) override;
  MessageRecord AppendMessage(RoomId room_id, std::string_view sender,
                              std::string_view content) override;
  std::vector<RoomRecord> LoadActiveRooms() override;

  void FailNext(Operation operation, ErrorCode code);
  std::vector<RoomRecord> Rooms() const;
  std::vector<MessageRecord> Messages() const;
  std::vector<MembershipRecord> MembershipEvents() const;

 private:
  void MaybeFail(Operation operation);

  mutable std::mutex mutex_;
  RoomId next_room_id_{1};
  MessageId next_message_id_{1};
  std::uint64_t next_membership_id_{1};
  std::vector<RoomRecord> rooms_;
  std::vector<MessageRecord> messages_;
  std::vector<MembershipRecord> memberships_;
  std::optional<std::pair<Operation, ErrorCode>> failure_;
};

}  // namespace chatroom
