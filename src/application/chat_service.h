#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "application/room_lock_table.h"
#include "common/types.h"
#include "domain/room_manager.h"
#include "persistence/chat_repository.h"
#include "protocol/message.h"

namespace chatroom {

struct Outbound {
  ConnectionRef recipient;
  nlohmann::json message;
};

using OutboundBatch = std::vector<Outbound>;

class ChatService {
 public:
  ChatService(RoomManager& rooms, IChatRepository& repository)
      : rooms_(rooms), repository_(repository) {}

  OutboundBatch Handle(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandleDisconnect(ConnectionRef connection) noexcept;

 private:
  OutboundBatch HandleHello(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandleCreate(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandleJoin(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandleLeave(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandleChat(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch HandlePing(ConnectionRef sender, const protocol::Command& command);
  OutboundBatch ErrorTo(ConnectionRef recipient, std::string_view request_id,
                        Error error) const;

  RoomManager& rooms_;
  IChatRepository& repository_;
  RoomLockTable room_locks_;
  std::mutex create_mutex_;
};

}  // namespace chatroom
