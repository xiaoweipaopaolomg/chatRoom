#include "application/chat_service.h"

#include <mutex>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>

#include "protocol/frame_codec.h"
#include "protocol/limits.h"

namespace chatroom {
namespace {

Error RepositoryError(const RepositoryException& exception) {
  const auto code = exception.code() == ErrorCode::kDbBusy
                        ? ErrorCode::kDbBusy
                        : ErrorCode::kDbError;
  return Error{code, code == ErrorCode::kDbBusy ? "database pool is busy"
                                                 : "database operation failed"};
}

}  // namespace

OutboundBatch ChatService::ErrorTo(ConnectionRef recipient, std::string_view request_id,
                                   Error error) const {
  return {{recipient, protocol::MakeError(request_id, error)}};
}

OutboundBatch ChatService::Handle(ConnectionRef sender,
                                  const protocol::Command& command) {
  switch (command.type) {
    case protocol::CommandType::kSessionHello: return HandleHello(sender, command);
    case protocol::CommandType::kCreateRoom: return HandleCreate(sender, command);
    case protocol::CommandType::kJoinRoom: return HandleJoin(sender, command);
    case protocol::CommandType::kLeaveRoom: return HandleLeave(sender, command);
    case protocol::CommandType::kSendChat: return HandleChat(sender, command);
    case protocol::CommandType::kPing: return HandlePing(sender, command);
  }
  return ErrorTo(sender, command.request_id,
                 {ErrorCode::kUnsupportedType, "unsupported command type"});
}

OutboundBatch ChatService::HandleHello(ConnectionRef sender,
                                       const protocol::Command& command) {
  auto result = rooms_.RegisterSession(sender, command.payload.at("nickname").get<std::string>());
  if (!result) {
    return ErrorTo(sender, command.request_id, result.error());
  }
  return {{sender, protocol::MakeOk(command.request_id)}};
}

OutboundBatch ChatService::HandleCreate(ConnectionRef sender,
                                        const protocol::Command& command) {
  const auto session = rooms_.FindSession(sender);
  if (!session) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kSessionRequired, "identify the session first"});
  }
  if (rooms_.RoomFor(sender)) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kAlreadyInRoom, "leave the current room first"});
  }

  std::lock_guard<std::mutex> create_lock(create_mutex_);
  if (rooms_.RoomFor(sender)) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kAlreadyInRoom, "leave the current room first"});
  }
  try {
    const auto record = repository_.CreateRoomWithJoin(
        command.payload.at("name").get_ref<const std::string&>(), session->nickname);
    auto created = rooms_.CreateLoadedRoom(record.id, record.name);
    if (!created) {
      return ErrorTo(sender, command.request_id,
                     {ErrorCode::kDbError, "persisted room conflicts with memory state"});
    }
    auto joined = rooms_.Join(record.id, sender);
    if (!joined) {
      return ErrorTo(sender, command.request_id, joined.error());
    }
    return {{sender, protocol::MakeRoomSnapshot(command.request_id, record.id, record.name,
                                                {session->nickname})}};
  } catch (const RepositoryException& exception) {
    return ErrorTo(sender, command.request_id, RepositoryError(exception));
  }
}

OutboundBatch ChatService::HandleJoin(ConnectionRef sender,
                                      const protocol::Command& command) {
  const auto session = rooms_.FindSession(sender);
  if (!session) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kSessionRequired, "identify the session first"});
  }
  if (rooms_.RoomFor(sender)) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kAlreadyInRoom, "leave the current room first"});
  }
  const RoomId room_id = command.payload.at("room_id").get<RoomId>();
  const auto room = rooms_.FindRoom(room_id);
  if (!room) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kRoomNotFound, "room does not exist"});
  }

  std::lock_guard<std::shared_mutex> room_lock(room_locks_.For(room_id));
  if (rooms_.RoomFor(sender)) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kAlreadyInRoom, "leave the current room first"});
  }
  std::vector<std::string> prospective_members;
  for (const auto member : rooms_.Members(room_id)) {
    if (const auto online = rooms_.FindSession(member))
      prospective_members.push_back(online->nickname);
  }
  prospective_members.push_back(session->nickname);
  if (protocol::MakeRoomSnapshot(command.request_id, room_id, room->name,
                                 prospective_members).dump().size() >
      protocol::kMaximumFrameBodyBytes) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kServerBusy, "room member list exceeds frame limit"});
  }
  try {
    repository_.AppendMembership(room_id, session->nickname, MembershipEventType::kJoin);
    auto joined = rooms_.Join(room_id, sender);
    if (!joined) {
      return ErrorTo(sender, command.request_id, joined.error());
    }
  } catch (const RepositoryException& exception) {
    return ErrorTo(sender, command.request_id, RepositoryError(exception));
  }

  const auto members = rooms_.Members(room_id);
  std::vector<std::string> nicknames;
  nicknames.reserve(members.size());
  for (const auto member : members) {
    const auto member_session = rooms_.FindSession(member);
    if (member_session) nicknames.push_back(member_session->nickname);
  }
  OutboundBatch output;
  output.push_back({sender, protocol::MakeRoomSnapshot(command.request_id, room_id,
                                                       room->name, nicknames)});
  for (const auto member : members) {
    if (member != sender) {
      output.push_back({member, protocol::MakeMemberEvent("member.joined", room_id,
                                                          session->nickname)});
    }
  }
  return output;
}

OutboundBatch ChatService::HandleLeave(ConnectionRef sender,
                                       const protocol::Command& command) {
  const auto session = rooms_.FindSession(sender);
  if (!session) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kSessionRequired, "identify the session first"});
  }
  const auto room_id = rooms_.RoomFor(sender);
  if (!room_id) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kNotInRoom, "join a room first"});
  }
  std::lock_guard<std::shared_mutex> room_lock(room_locks_.For(*room_id));
  try {
    repository_.AppendMembership(*room_id, session->nickname, MembershipEventType::kLeave);
    auto removal = rooms_.Leave(sender);
    if (!removal) {
      return ErrorTo(sender, command.request_id, removal.error());
    }
    OutboundBatch output{{sender, protocol::MakeOk(command.request_id)}};
    for (const auto member : removal->remaining) {
      output.push_back({member, protocol::MakeMemberEvent("member.left", *room_id,
                                                          session->nickname)});
    }
    return output;
  } catch (const RepositoryException& exception) {
    return ErrorTo(sender, command.request_id, RepositoryError(exception));
  }
}

OutboundBatch ChatService::HandleChat(ConnectionRef sender,
                                      const protocol::Command& command) {
  const auto session = rooms_.FindSession(sender);
  if (!session) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kSessionRequired, "identify the session first"});
  }
  const auto room_id = rooms_.RoomFor(sender);
  if (!room_id) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kNotInRoom, "join a room first"});
  }
  const auto& text = command.payload.at("text").get_ref<const std::string&>();
  if (text.size() > protocol::kMaximumChatTextBytes) {
    return ErrorTo(sender, command.request_id,
                   {ErrorCode::kMessageTooLarge, "chat text exceeds 4096 UTF-8 bytes"});
  }

  std::shared_lock<std::shared_mutex> room_lock(room_locks_.For(*room_id));
  try {
    const auto record = repository_.AppendMessage(*room_id, session->nickname, text);
    const auto message = std::make_shared<const nlohmann::json>(
        protocol::MakeChatEvent(*room_id, record.id, session->nickname, text));
    const auto frame = std::make_shared<const std::string>(
        protocol::EncodeFrame(message->dump()));
    const auto members = rooms_.Members(*room_id);
    OutboundBatch output;
    output.reserve(members.size());
    for (const auto member : members) {
      output.push_back({member, message, frame});
    }
    return output;
  } catch (const RepositoryException& exception) {
    return ErrorTo(sender, command.request_id, RepositoryError(exception));
  }
}

OutboundBatch ChatService::HandlePing(ConnectionRef sender,
                                      const protocol::Command& command) {
  const auto client_time = command.payload.contains("client_time")
                               ? command.payload.at("client_time")
                               : nlohmann::json(nullptr);
  return {{sender, protocol::MakePong(command.request_id, client_time)}};
}

OutboundBatch ChatService::HandleDisconnect(ConnectionRef connection) noexcept {
  try {
    const auto removal = rooms_.RemoveConnection(connection);
    if (!removal) {
      return {};
    }
    OutboundBatch output;
    for (const auto member : removal->remaining) {
      output.push_back({member, protocol::MakeMemberEvent("member.left", removal->room_id,
                                                          removal->nickname)});
    }
    try {
      std::lock_guard<std::shared_mutex> room_lock(room_locks_.For(removal->room_id));
      repository_.AppendMembership(removal->room_id, removal->nickname,
                                   MembershipEventType::kLeave);
    } catch (const RepositoryException&) {
      // Disconnect cleanup is intentionally memory-first and persistence is best effort.
    }
    return output;
  } catch (...) {
    return {};
  }
}

}  // namespace chatroom
