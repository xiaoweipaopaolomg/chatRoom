#include "server/socket_utils.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace chatroom::socket_utils {
namespace {
[[noreturn]] void Fail(std::string_view operation) {
  throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}
}  // namespace

void SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) Fail("fcntl");
}

int CreateListener(std::string_view address, std::uint16_t port, int backlog) {
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) Fail("socket");
  int enabled = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
    close(fd); Fail("setsockopt");
  }
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port);
  if (address.empty() || address == "0.0.0.0") endpoint.sin_addr.s_addr = INADDR_ANY;
  else if (inet_pton(AF_INET, std::string(address).c_str(), &endpoint.sin_addr) != 1) {
    close(fd); throw std::invalid_argument("listen address must be an IPv4 address");
  }
  if (bind(fd, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) < 0) {
    const int saved = errno; close(fd); errno = saved; Fail("bind");
  }
  if (listen(fd, backlog) < 0) {
    const int saved = errno; close(fd); errno = saved; Fail("listen");
  }
  return fd;
}

}  // namespace chatroom::socket_utils
