#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "application/room_lock_table.h"
#include "common/types.h"
#include "domain/room_manager.h"
#include "persistence/chat_repository.h"
#include "protocol/message.h"

namespace chatroom {

struct Outbound {
  Outbound(ConnectionRef target, nlohmann::json body,
           std::shared_ptr<const std::string> frame = {})
      : recipient(target), message(std::make_shared<const nlohmann::json>(std::move(body))),
        encoded_frame(std::move(frame)) {}
  Outbound(ConnectionRef target, std::shared_ptr<const nlohmann::json> body,
           std::shared_ptr<const std::string> frame)
      : recipient(target), message(std::move(body)), encoded_frame(std::move(frame)) {}
  ConnectionRef recipient;
  std::shared_ptr<const nlohmann::json> message;
  std::shared_ptr<const std::string> encoded_frame;
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
