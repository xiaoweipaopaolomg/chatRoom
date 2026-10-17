#pragma once

#include <string>
#include <string_view>

namespace chatroom {

enum class ErrorCode {
  kInvalidMessage,
  kUnsupportedType,
  kNicknameTaken,
  kSessionRequired,
  kAlreadyInRoom,
  kRoomNotFound,
  kNotInRoom,
  kMessageTooLarge,
  kServerBusy,
  kDbBusy,
  kDbError,
};

struct Error {
  ErrorCode code{ErrorCode::kInvalidMessage};
  std::string message;
};

inline std::string_view ToString(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kInvalidMessage: return "INVALID_MESSAGE";
    case ErrorCode::kUnsupportedType: return "UNSUPPORTED_TYPE";
    case ErrorCode::kNicknameTaken: return "NICKNAME_TAKEN";
    case ErrorCode::kSessionRequired: return "SESSION_REQUIRED";
    case ErrorCode::kAlreadyInRoom: return "ALREADY_IN_ROOM";
    case ErrorCode::kRoomNotFound: return "ROOM_NOT_FOUND";
    case ErrorCode::kNotInRoom: return "NOT_IN_ROOM";
    case ErrorCode::kMessageTooLarge: return "MESSAGE_TOO_LARGE";
    case ErrorCode::kServerBusy: return "SERVER_BUSY";
    case ErrorCode::kDbBusy: return "DB_BUSY";
    case ErrorCode::kDbError: return "DB_ERROR";
  }
  return "DB_ERROR";
}

}  // namespace chatroom
