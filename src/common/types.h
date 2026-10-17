#pragma once

#include <cstdint>
#include <functional>

namespace chatroom {

struct ConnectionRef {
  std::uint64_t id{};
  std::uint64_t generation{};

  friend constexpr bool operator==(ConnectionRef lhs, ConnectionRef rhs) noexcept {
    return lhs.id == rhs.id && lhs.generation == rhs.generation;
  }
  friend constexpr bool operator!=(ConnectionRef lhs, ConnectionRef rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(ConnectionRef lhs, ConnectionRef rhs) noexcept {
    return lhs.id < rhs.id || (lhs.id == rhs.id && lhs.generation < rhs.generation);
  }
};

using RoomId = std::uint64_t;
using MessageId = std::uint64_t;

struct ConnectionRefHash {
  std::size_t operator()(ConnectionRef value) const noexcept {
    const auto first = std::hash<std::uint64_t>{}(value.id);
    const auto second = std::hash<std::uint64_t>{}(value.generation);
    return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
  }
};

}  // namespace chatroom
