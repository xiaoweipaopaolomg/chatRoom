#include "domain/room_manager.h"

#include <algorithm>
#include <utility>

namespace chatroom {

Result<void> RoomManager::RegisterSession(ConnectionRef connection, std::string nickname) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  if (sessions_.find(connection) != sessions_.end()) {
    return Error{ErrorCode::kInvalidMessage, "connection already has a session"};
  }
  if (nicknames_.find(nickname) != nicknames_.end()) {
    return Error{ErrorCode::kNicknameTaken, "nickname is already online"};
  }
  Session session{connection, std::move(nickname)};
  nicknames_.emplace(session.nickname, connection);
  sessions_.emplace(connection, std::move(session));
  return {};
}

Result<void> RoomManager::CreateLoadedRoom(RoomId room_id, std::string name) {
  if (room_id == 0 || name.empty()) {
    return Error{ErrorCode::kInvalidMessage, "room id and name are required"};
  }
  std::lock_guard<std::mutex> lock(rooms_mutex_);
  const auto inserted = rooms_.emplace(
      room_id, std::make_shared<RoomState>(room_id, std::move(name)));
  if (!inserted.second) {
    return Error{ErrorCode::kInvalidMessage, "room id already exists"};
  }
  return {};
}

std::shared_ptr<RoomManager::RoomState> RoomManager::FindRoomState(RoomId room_id) const {
  std::lock_guard<std::mutex> lock(rooms_mutex_);
  const auto found = rooms_.find(room_id);
  return found == rooms_.end() ? nullptr : found->second;
}

Result<void> RoomManager::Join(RoomId room_id, ConnectionRef connection) {
  std::unique_lock<std::mutex> sessions_lock(sessions_mutex_);
  const auto session = sessions_.find(connection);
  if (session == sessions_.end()) {
    return Error{ErrorCode::kSessionRequired, "identify the session first"};
  }
  auto room = FindRoomState(room_id);
  if (!room) {
    return Error{ErrorCode::kRoomNotFound, "room does not exist"};
  }
  std::lock_guard<std::mutex> membership_lock(membership_mutex_);
  if (room_by_connection_.find(connection) != room_by_connection_.end()) {
    return Error{ErrorCode::kAlreadyInRoom, "connection is already in a room"};
  }
  std::lock_guard<std::mutex> room_lock(room->mutex);
  room->members.emplace(connection, session->second.nickname);
  room_by_connection_.emplace(connection, room_id);
  return {};
}

Result<Removal> RoomManager::Leave(ConnectionRef connection) {
  std::unique_lock<std::mutex> sessions_lock(sessions_mutex_);
  const auto session = sessions_.find(connection);
  if (session == sessions_.end()) {
    return Error{ErrorCode::kSessionRequired, "identify the session first"};
  }
  std::lock_guard<std::mutex> membership_lock(membership_mutex_);
  const auto membership = room_by_connection_.find(connection);
  if (membership == room_by_connection_.end()) {
    return Error{ErrorCode::kNotInRoom, "connection is not in a room"};
  }
  const RoomId room_id = membership->second;
  auto room = FindRoomState(room_id);
  if (!room) {
    return Error{ErrorCode::kRoomNotFound, "room does not exist"};
  }
  std::lock_guard<std::mutex> room_lock(room->mutex);
  room->members.erase(connection);
  room_by_connection_.erase(membership);
  Removal removal{room_id, session->second.nickname, {}};
  removal.remaining.reserve(room->members.size());
  for (const auto& member : room->members) {
    removal.remaining.push_back(member.first);
  }
  return removal;
}

std::optional<Removal> RoomManager::RemoveConnection(ConnectionRef connection) {
  std::unique_lock<std::mutex> sessions_lock(sessions_mutex_);
  const auto session = sessions_.find(connection);
  if (session == sessions_.end()) {
    return std::nullopt;
  }

  std::optional<Removal> removal;
  {
    std::lock_guard<std::mutex> membership_lock(membership_mutex_);
    const auto membership = room_by_connection_.find(connection);
    if (membership != room_by_connection_.end()) {
      const RoomId room_id = membership->second;
      auto room = FindRoomState(room_id);
      if (room) {
        std::lock_guard<std::mutex> room_lock(room->mutex);
        room->members.erase(connection);
        Removal value{room_id, session->second.nickname, {}};
        value.remaining.reserve(room->members.size());
        for (const auto& member : room->members) {
          value.remaining.push_back(member.first);
        }
        removal = std::move(value);
      }
      room_by_connection_.erase(membership);
    }
  }
  nicknames_.erase(session->second.nickname);
  sessions_.erase(session);
  return removal;
}

std::optional<Session> RoomManager::FindSession(ConnectionRef connection) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  const auto found = sessions_.find(connection);
  if (found == sessions_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<Room> RoomManager::FindRoom(RoomId room_id) const {
  auto state = FindRoomState(room_id);
  if (!state) {
    return std::nullopt;
  }
  return Room{state->id, state->name};
}

std::vector<ConnectionRef> RoomManager::Members(RoomId room_id) const {
  auto room = FindRoomState(room_id);
  if (!room) {
    return {};
  }
  std::lock_guard<std::mutex> lock(room->mutex);
  std::vector<ConnectionRef> members;
  members.reserve(room->members.size());
  for (const auto& member : room->members) {
    members.push_back(member.first);
  }
  return members;
}

std::optional<RoomId> RoomManager::RoomFor(ConnectionRef connection) const {
  std::lock_guard<std::mutex> lock(membership_mutex_);
  const auto found = room_by_connection_.find(connection);
  if (found == room_by_connection_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::size_t RoomManager::SessionCount() const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  return sessions_.size();
}

std::size_t RoomManager::RoomCount() const {
  std::lock_guard<std::mutex> lock(rooms_mutex_);
  return rooms_.size();
}

}  // namespace chatroom
