#include "persistence/in_memory_chat_repository.h"

#include <algorithm>

namespace chatroom {

void InMemoryChatRepository::MaybeFail(Operation operation) {
  if (failure_ && failure_->first == operation) {
    const auto code = failure_->second;
    failure_.reset();
    throw RepositoryException(code, "injected repository failure");
  }
}

RoomRecord InMemoryChatRepository::CreateRoomWithJoin(std::string_view name,
                                                       std::string_view creator) {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeFail(Operation::kCreateRoomWithJoin);
  RoomRecord room{next_room_id_, std::string(name), std::string(creator)};
  MembershipRecord membership{next_membership_id_, room.id, std::string(creator),
                              MembershipEventType::kJoin};
  ++next_room_id_;
  ++next_membership_id_;
  rooms_.push_back(room);
  memberships_.push_back(std::move(membership));
  return room;
}

bool InMemoryChatRepository::RoomExists(RoomId room_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeFail(Operation::kRoomExists);
  return std::any_of(rooms_.begin(), rooms_.end(),
                     [room_id](const RoomRecord& room) { return room.id == room_id; });
}

MembershipRecord InMemoryChatRepository::AppendMembership(
    RoomId room_id, std::string_view nickname, MembershipEventType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeFail(Operation::kAppendMembership);
  const bool exists = std::any_of(
      rooms_.begin(), rooms_.end(),
      [room_id](const RoomRecord& room) { return room.id == room_id; });
  if (!exists) {
    throw RepositoryException(ErrorCode::kDbError, "membership references missing room");
  }
  MembershipRecord record{next_membership_id_++, room_id, std::string(nickname), type};
  memberships_.push_back(record);
  return record;
}

MessageRecord InMemoryChatRepository::AppendMessage(
    RoomId room_id, std::string_view sender, std::string_view content) {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeFail(Operation::kAppendMessage);
  const bool exists = std::any_of(
      rooms_.begin(), rooms_.end(),
      [room_id](const RoomRecord& room) { return room.id == room_id; });
  if (!exists) {
    throw RepositoryException(ErrorCode::kDbError, "message references missing room");
  }
  MessageRecord record{next_message_id_++, room_id, std::string(sender),
                       std::string(content)};
  messages_.push_back(record);
  return record;
}

std::vector<RoomRecord> InMemoryChatRepository::LoadActiveRooms() {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeFail(Operation::kLoadActiveRooms);
  return rooms_;
}

void InMemoryChatRepository::FailNext(Operation operation, ErrorCode code) {
  std::lock_guard<std::mutex> lock(mutex_);
  failure_ = std::make_pair(operation, code);
}

std::vector<RoomRecord> InMemoryChatRepository::Rooms() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rooms_;
}

std::vector<MessageRecord> InMemoryChatRepository::Messages() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return messages_;
}

std::vector<MembershipRecord> InMemoryChatRepository::MembershipEvents() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return memberships_;
}

}  // namespace chatroom
