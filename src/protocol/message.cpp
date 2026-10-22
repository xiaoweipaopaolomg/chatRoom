#include "protocol/message.h"

#include <array>
#include <exception>
#include <optional>

#include "protocol/limits.h"

namespace chatroom::protocol {
namespace {

Result<Command> Invalid(std::string message) {
  return Error{ErrorCode::kInvalidMessage, std::move(message)};
}

bool IsStringBytesInRange(const nlohmann::json& value, std::size_t minimum,
                          std::size_t maximum) {
  if (!value.is_string()) {
    return false;
  }
  const auto& text = value.get_ref<const std::string&>();
  return text.size() >= minimum && text.size() <= maximum;
}

std::optional<CommandType> FindCommand(std::string_view type) {
  if (type == "session.hello") return CommandType::kSessionHello;
  if (type == "room.create") return CommandType::kCreateRoom;
  if (type == "room.join") return CommandType::kJoinRoom;
  if (type == "room.leave") return CommandType::kLeaveRoom;
  if (type == "chat.send") return CommandType::kSendChat;
  if (type == "ping") return CommandType::kPing;
  return std::nullopt;
}

}  // namespace

Result<Command> ParseCommand(std::string_view input) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(input);
  } catch (const nlohmann::json::exception&) {
    return Invalid("body is not valid UTF-8 JSON");
  }
  if (!root.is_object()) {
    return Invalid("message root must be an object");
  }
  if (!root.contains("type") || !root["type"].is_string() ||
      !root.contains("request_id") || !root["request_id"].is_string() ||
      !root.contains("payload") || !root["payload"].is_object()) {
    return Invalid("type, request_id and object payload are required");
  }
  const auto request_id = root["request_id"].get<std::string>();
  if (request_id.empty() || request_id.size() > kMaximumRequestIdBytes) {
    return Invalid("request_id must contain 1..64 bytes");
  }
  const auto type_text = root["type"].get<std::string>();
  const auto type = FindCommand(type_text);
  if (!type) {
    return Error{ErrorCode::kUnsupportedType, "unsupported command type"};
  }
  const auto& payload = root["payload"];
  switch (*type) {
    case CommandType::kSessionHello:
      if (!payload.contains("nickname") ||
          !IsStringBytesInRange(payload["nickname"], 1, kMaximumNicknameBytes)) {
        return Invalid("nickname must contain 1..32 UTF-8 bytes");
      }
      break;
    case CommandType::kCreateRoom:
      if (!payload.contains("name") ||
          !IsStringBytesInRange(payload["name"], 1, kMaximumRoomNameBytes)) {
        return Invalid("room name must contain 1..64 UTF-8 bytes");
      }
      break;
    case CommandType::kJoinRoom:
      if (!payload.contains("room_id") || !payload["room_id"].is_number_unsigned() ||
          payload["room_id"].get<RoomId>() == 0) {
        return Invalid("room_id must be a positive unsigned integer");
      }
      break;
    case CommandType::kSendChat:
      if (!payload.contains("text") || !payload["text"].is_string()) {
        return Invalid("chat text must be a string");
      }
      if (payload["text"].get_ref<const std::string&>().empty()) {
        return Invalid("chat text must not be empty");
      }
      if (payload["text"].get_ref<const std::string&>().size() > kMaximumChatTextBytes) {
        return Error{ErrorCode::kMessageTooLarge, "chat text exceeds 4096 UTF-8 bytes"};
      }
      break;
    case CommandType::kLeaveRoom:
    case CommandType::kPing:
      break;
  }
  return Command{*type, request_id, payload};
}

nlohmann::json MakeOk(std::string_view request_id) {
  return {{"type", "response.ok"}, {"request_id", request_id}};
}

nlohmann::json MakeError(std::string_view request_id, const Error& error) {
  return {{"type", "response.error"},
          {"request_id", request_id},
          {"error", {{"code", ToString(error.code)}, {"message", error.message}}}};
}

nlohmann::json MakeRoomSnapshot(std::string_view request_id, RoomId room_id,
                                std::string_view room_name,
                                const std::vector<std::string>& members) {
  return {{"type", "room.snapshot"},
          {"request_id", request_id},
          {"room", {{"id", room_id}, {"name", room_name}, {"members", members}}}};
}

nlohmann::json MakeMemberEvent(std::string_view type, RoomId room_id,
                               std::string_view nickname) {
  return {{"type", type}, {"room_id", room_id}, {"nickname", nickname}};
}

nlohmann::json MakeChatEvent(RoomId room_id, MessageId message_id,
                             std::string_view sender, std::string_view text) {
  return {{"type", "chat.message"},
          {"room_id", room_id},
          {"message_id", message_id},
          {"sender", sender},
          {"text", text}};
}

nlohmann::json MakePong(std::string_view request_id,
                        const nlohmann::json& client_time) {
  return {{"type", "pong"}, {"request_id", request_id}, {"client_time", client_time}};
}

}  // namespace chatroom::protocol
