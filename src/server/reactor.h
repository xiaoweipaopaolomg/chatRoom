#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "application/chat_service.h"
#include "common/bounded_queue.h"
#include "common/keyed_executor.h"
#include "server/connection.h"

namespace chatroom {

struct ReactorConfig {
  std::string address{"0.0.0.0"};
  std::uint16_t port{7777};
  int backlog{256};
  std::size_t maximum_connections{10'000};
  std::size_t maximum_output_bytes{1U << 20U};
  std::size_t result_queue_capacity{4096};
};

class Reactor {
 public:
  Reactor(ReactorConfig config, KeyedExecutor& executor, ChatService& service);
  ~Reactor();
  Reactor(const Reactor&) = delete;
  Reactor& operator=(const Reactor&) = delete;

  void Run();
  void RequestStop() noexcept;
  bool Post(OutboundBatch batch) noexcept;
  std::uint16_t bound_port() const noexcept { return bound_port_; }

 private:
  void AcceptReady();
  void ConnectionReady(int fd, std::uint32_t events);
  bool ReadReady(Connection& connection);
  bool WriteReady(Connection& connection);
  void DrainResults();
  void Deliver(const Outbound& outbound);
  void UpdateInterest(const Connection& connection);
  void CloseConnection(int fd);
  void Wake() noexcept;

  ReactorConfig config_;
  KeyedExecutor& executor_;
  ChatService& service_;
  BoundedQueue<OutboundBatch> results_;
  int listener_fd_{-1};
  int epoll_fd_{-1};
  int event_fd_{-1};
  std::uint16_t bound_port_{0};
  std::atomic<bool> stopping_{false};
  std::unordered_map<int, std::unique_ptr<Connection>> connections_;
  std::unordered_map<int, std::uint64_t> generations_;
};

}  // namespace chatroom
