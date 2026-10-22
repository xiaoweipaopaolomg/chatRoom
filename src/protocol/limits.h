#pragma once

#include <cstddef>

namespace chatroom::protocol {

// C++17 inline variables keep protocol limits in one header without requiring
// a separate definition or risking divergent client/server values.
inline constexpr std::size_t kMaximumFrameBodyBytes = 65'536;
inline constexpr std::size_t kMaximumRequestIdBytes = 64;
inline constexpr std::size_t kMaximumNicknameBytes = 32;
inline constexpr std::size_t kMaximumRoomNameBytes = 64;
inline constexpr std::size_t kMaximumChatTextBytes = 4'096;

}  // namespace chatroom::protocol
