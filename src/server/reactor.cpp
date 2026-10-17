#include "server/reactor.h"

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "protocol/frame_codec.h"
#include "protocol/message.h"
#include "server/socket_utils.h"

namespace chatroom {
namespace {
constexpr std::uint32_t kBaseEvents = EPOLLIN | EPOLLET | EPOLLRDHUP;
void AddEpoll(int epoll_fd, int fd, std::uint32_t events) {
  epoll_event event{}; event.events = events; event.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    throw std::runtime_error("epoll_ctl add failed");
}
}  // namespace

Reactor::Reactor(ReactorConfig config, KeyedExecutor& executor, ChatService& service)
    : config_(std::move(config)), executor_(executor), service_(service),
      results_(config_.result_queue_capacity) {
  listener_fd_ = socket_utils::CreateListener(config_.address, config_.port, config_.backlog);
  sockaddr_in endpoint{}; socklen_t length = sizeof(endpoint);
  if (getsockname(listener_fd_, reinterpret_cast<sockaddr*>(&endpoint), &length) == 0)
    bound_port_ = ntohs(endpoint.sin_port);
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (epoll_fd_ < 0 || event_fd_ < 0) throw std::runtime_error("epoll/eventfd creation failed");
  AddEpoll(epoll_fd_, listener_fd_, EPOLLIN | EPOLLET);
  AddEpoll(epoll_fd_, event_fd_, EPOLLIN | EPOLLET);
}

Reactor::~Reactor() {
  for (auto& item : connections_) close(item.first);
  if (listener_fd_ >= 0) close(listener_fd_);
  if (event_fd_ >= 0) close(event_fd_);
  if (epoll_fd_ >= 0) close(epoll_fd_);
}

void Reactor::Wake() noexcept {
  const std::uint64_t one = 1;
  const auto result = write(event_fd_, &one, sizeof(one));
  (void)result;
}
void Reactor::RequestStop() noexcept { stopping_.store(true); Wake(); }
bool Reactor::Post(OutboundBatch batch) noexcept {
  if (!results_.TryPush(std::move(batch))) return false;
  Wake(); return true;
}

void Reactor::Run() {
  std::array<epoll_event, 128> events{};
  while (!stopping_.load()) {
    const int count = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), -1);
    if (count < 0) { if (errno == EINTR) continue; throw std::runtime_error("epoll_wait failed"); }
    for (int index = 0; index < count; ++index) {
      const int fd = events[static_cast<std::size_t>(index)].data.fd;
      if (fd == listener_fd_) AcceptReady();
      else if (fd == event_fd_) DrainResults();
      else ConnectionReady(fd, events[static_cast<std::size_t>(index)].events);
    }
  }
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listener_fd_, nullptr);
  close(listener_fd_); listener_fd_ = -1;
  DrainResults();
  while (!connections_.empty()) CloseConnection(connections_.begin()->first);
}

void Reactor::AcceptReady() {
  for (;;) {
    const int fd = accept4(listener_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      throw std::runtime_error("accept4 failed");
    }
    if (connections_.size() >= config_.maximum_connections) { close(fd); continue; }
    const auto generation = ++generations_[fd];
    ConnectionRef reference{static_cast<std::uint64_t>(fd), generation};
    auto connection = std::make_unique<Connection>(fd, reference, config_.maximum_output_bytes);
    AddEpoll(epoll_fd_, fd, kBaseEvents);
    connections_.emplace(fd, std::move(connection));
  }
}

void Reactor::ConnectionReady(int fd, std::uint32_t events) {
  auto found = connections_.find(fd);
  if (found == connections_.end()) return;
  if ((events & (EPOLLERR | EPOLLHUP)) != 0U) { CloseConnection(fd); return; }
  if ((events & EPOLLIN) != 0U && !ReadReady(*found->second)) { CloseConnection(fd); return; }
  found = connections_.find(fd); if (found == connections_.end()) return;
  if ((events & EPOLLOUT) != 0U && !WriteReady(*found->second)) { CloseConnection(fd); return; }
  if ((events & EPOLLRDHUP) != 0U) CloseConnection(fd);
}

bool Reactor::ReadReady(Connection& connection) {
  std::array<char, 16 * 1024> bytes{};
  for (;;) {
    const ssize_t count = recv(connection.fd(), bytes.data(), bytes.size(), 0);
    if (count == 0) return false;
    if (count < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
      return false;
    }
    auto decoded = connection.decoder().Push(
        std::string_view(bytes.data(), static_cast<std::size_t>(count)));
    if (decoded.error) return false;
    for (auto& frame : decoded.frames) {
      auto parsed = protocol::ParseCommand(frame);
      if (!parsed) {
        try {
          const auto raw = nlohmann::json::parse(frame);
          if (parsed.error().code == ErrorCode::kUnsupportedType &&
              raw.contains("request_id") && raw["request_id"].is_string()) {
            const auto body = protocol::MakeError(raw["request_id"].get<std::string>(),
                                                   parsed.error()).dump();
            if (!connection.QueueOutput(protocol::EncodeFrame(body))) return false;
            UpdateInterest(connection); continue;
          }
        } catch (...) {}
        return false;
      }
      const auto reference = connection.reference();
      const auto request_id = parsed->request_id;
      auto command = std::move(parsed.value());
      const bool submitted = executor_.Submit(reference.id,
          [this, reference, command = std::move(command)]() mutable {
            auto output = service_.Handle(reference, command);
            (void)Post(std::move(output));
          });
      if (!submitted) {
        const auto body = protocol::MakeError(request_id,
            {ErrorCode::kServerBusy, "business queue is full"}).dump();
        if (!connection.QueueOutput(protocol::EncodeFrame(body))) return false;
        UpdateInterest(connection);
      }
    }
  }
}

bool Reactor::WriteReady(Connection& connection) {
  while (connection.has_output()) {
    const auto pending = connection.PendingOutput();
    const ssize_t count = send(connection.fd(), pending.data(), pending.size(), MSG_NOSIGNAL);
    if (count < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      return false;
    }
    connection.ConsumeWritten(static_cast<std::size_t>(count));
  }
  UpdateInterest(connection); return true;
}

void Reactor::DrainResults() {
  std::uint64_t value{};
  while (read(event_fd_, &value, sizeof(value)) > 0) {}
  while (auto batch = results_.TryPop())
    for (const auto& outbound : *batch) Deliver(outbound);
}

void Reactor::Deliver(const Outbound& outbound) {
  const int fd = static_cast<int>(outbound.recipient.id);
  auto found = connections_.find(fd);
  if (found == connections_.end() || found->second->reference() != outbound.recipient) return;
  if (!found->second->QueueOutput(protocol::EncodeFrame(outbound.message.dump()))) {
    CloseConnection(fd); return;
  }
  UpdateInterest(*found->second);
}

void Reactor::UpdateInterest(const Connection& connection) {
  epoll_event event{};
  event.events = kBaseEvents | (connection.has_output() ? EPOLLOUT : 0U);
  event.data.fd = connection.fd();
  (void)epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection.fd(), &event);
}

void Reactor::CloseConnection(int fd) {
  auto found = connections_.find(fd); if (found == connections_.end()) return;
  const auto reference = found->second->reference();
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr); close(fd); connections_.erase(found);
  (void)executor_.Submit(reference.id, [this, reference] {
    auto output = service_.HandleDisconnect(reference);
    (void)Post(std::move(output));
  });
}

}  // namespace chatroom
