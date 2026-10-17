#pragma once

#include <cstddef>
#include <string>

#include "persistence/mysql_connection_pool.h"
#include "server/reactor.h"

namespace chatroom {

struct ServerConfig {
  ReactorConfig reactor;
  std::size_t worker_count{4};
  std::size_t worker_queue_capacity{1024};
  bool use_mysql{false};
  MySqlPoolConfig database;
  std::string ToSafeString() const;
};

ServerConfig LoadConfig(const std::string& path);

}  // namespace chatroom
