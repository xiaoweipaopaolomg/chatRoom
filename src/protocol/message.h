#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/error.h"
#include "common/result.h"
#include "common/types.h"

namespace chatroom::protocol {

enum class CommandType {
  kSessionHello,
  kCreateRoom,
  kJoinRoom,
  kLeaveRoom,
  kSendChat,
  kPing,
};

struct Command {
  CommandType type;
  std::string request_id;
  nlohmann::json payload;
};

Result<Command> ParseCommand(std::string_view input);

nlohmann::json MakeOk(std::string_view request_id);
nlohmann::json MakeError(std::string_view request_id, const Error& error);
nlohmann::json MakeRoomSnapshot(std::string_view request_id, RoomId room_id,
                                std::string_view room_name,
                                const std::vector<std::string>& members);
nlohmann::json MakeMemberEvent(std::string_view type, RoomId room_id,
                               std::string_view nickname);
nlohmann::json MakeChatEvent(RoomId room_id, MessageId message_id,
                             std::string_view sender, std::string_view text);
nlohmann::json MakePong(std::string_view request_id,
                        const nlohmann::json& client_time);

}  // namespace chatroom::protocol
