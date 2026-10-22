#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/result.h"
#include "common/types.h"

namespace chatroom {

struct Session {
  ConnectionRef connection;
  std::string nickname;
};

struct Room {
  RoomId id{};
  std::string name;
};

struct Removal {
  RoomId room_id{};
  std::string nickname;
  std::vector<ConnectionRef> remaining;
};

class RoomManager {
 public:
  [[nodiscard]] Result<void> RegisterSession(ConnectionRef connection, std::string nickname);
  [[nodiscard]] Result<void> CreateLoadedRoom(RoomId room_id, std::string name);
  [[nodiscard]] Result<void> Join(RoomId room_id, ConnectionRef connection);
  [[nodiscard]] Result<Removal> Leave(ConnectionRef connection);
  [[nodiscard]] std::optional<Removal> RemoveConnection(ConnectionRef connection);

  [[nodiscard]] std::optional<Session> FindSession(ConnectionRef connection) const;
  [[nodiscard]] std::optional<Room> FindRoom(RoomId room_id) const;
  [[nodiscard]] std::vector<ConnectionRef> Members(RoomId room_id) const;
  [[nodiscard]] std::optional<RoomId> RoomFor(ConnectionRef connection) const;
  [[nodiscard]] std::size_t SessionCount() const;
  [[nodiscard]] std::size_t RoomCount() const;

 private:
  struct RoomState {
    RoomState(RoomId room_id, std::string room_name)
        : id(room_id), name(std::move(room_name)) {}
    RoomId id;
    std::string name;
    mutable std::mutex mutex;
    std::unordered_map<ConnectionRef, std::string, ConnectionRefHash> members;
  };

  std::shared_ptr<RoomState> FindRoomState(RoomId room_id) const;

  mutable std::mutex sessions_mutex_;
  std::unordered_map<ConnectionRef, Session, ConnectionRefHash> sessions_;
  std::unordered_map<std::string, ConnectionRef> nicknames_;

  mutable std::mutex rooms_mutex_;
  std::unordered_map<RoomId, std::shared_ptr<RoomState>> rooms_;

  mutable std::mutex membership_mutex_;
  std::unordered_map<ConnectionRef, RoomId, ConnectionRefHash> room_by_connection_;
};

}  // namespace chatroom
