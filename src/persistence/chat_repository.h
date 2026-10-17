#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.h"
#include "common/types.h"

namespace chatroom {

enum class MembershipEventType { kJoin, kLeave };

struct RoomRecord {
  RoomId id{};
  std::string name;
  std::string creator;
};

struct MessageRecord {
  MessageId id{};
  RoomId room_id{};
  std::string sender;
  std::string content;
};

struct MembershipRecord {
  std::uint64_t id{};
  RoomId room_id{};
  std::string nickname;
  MembershipEventType type{MembershipEventType::kJoin};
};

class RepositoryException : public std::runtime_error {
 public:
  RepositoryException(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}
  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

class IChatRepository {
 public:
  virtual ~IChatRepository() = default;
  virtual RoomRecord CreateRoomWithJoin(std::string_view name,
                                        std::string_view creator) = 0;
  virtual bool RoomExists(RoomId room_id) = 0;
  virtual MembershipRecord AppendMembership(RoomId room_id,
      std::string_view nickname, MembershipEventType type) = 0;
  virtual MessageRecord AppendMessage(RoomId room_id,
      std::string_view sender, std::string_view content) = 0;
  virtual std::vector<RoomRecord> LoadActiveRooms() = 0;
};

}  // namespace chatroom
