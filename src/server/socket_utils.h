#pragma once

#include <cstdint>
#include <string_view>

namespace chatroom::socket_utils {

int CreateListener(std::string_view address, std::uint16_t port, int backlog);
void SetNonBlocking(int fd);

}  // namespace chatroom::socket_utils
