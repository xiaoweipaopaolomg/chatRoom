#pragma once

#include <array>
#include <cstddef>
#include <mutex>

#include "common/types.h"

namespace chatroom {

class RoomLockTable {
 public:
  static constexpr std::size_t kStripeCount = 64;
  std::mutex& For(RoomId room_id) noexcept { return locks_[room_id % kStripeCount]; }

 private:
  std::array<std::mutex, kStripeCount> locks_;
};

}  // namespace chatroom
