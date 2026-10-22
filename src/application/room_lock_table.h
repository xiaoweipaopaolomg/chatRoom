#pragma once

#include <array>
#include <cstddef>
#include <shared_mutex>

#include "common/types.h"

namespace chatroom {

class RoomLockTable {
 public:
  static constexpr std::size_t kStripeCount = 256;
  std::shared_mutex& For(RoomId room_id) noexcept { return locks_[room_id % kStripeCount]; }

 private:
  std::array<std::shared_mutex, kStripeCount> locks_;
};

}  // namespace chatroom
