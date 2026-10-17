#include "server/config.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace chatroom {
namespace {
template <typename T>
void Assign(const nlohmann::json& object, const char* key, T& target) {
  if (object.contains(key)) target = object.at(key).get<T>();
}
std::string Env(const char* name, std::string fallback) {
  const char* value = std::getenv(name); return value ? value : std::move(fallback);
}
}  // namespace

ServerConfig LoadConfig(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open server config: " + path);
  nlohmann::json root; input >> root;
  ServerConfig config;
  if (root.contains("server")) {
    const auto& server = root.at("server");
    Assign(server, "address", config.reactor.address);
    Assign(server, "port", config.reactor.port);
    Assign(server, "backlog", config.reactor.backlog);
    Assign(server, "maximum_connections", config.reactor.maximum_connections);
    Assign(server, "maximum_output_bytes", config.reactor.maximum_output_bytes);
    Assign(server, "result_queue_capacity", config.reactor.result_queue_capacity);
    Assign(server, "worker_count", config.worker_count);
    Assign(server, "worker_queue_capacity", config.worker_queue_capacity);
  }
  if (root.contains("database")) {
    const auto& database = root.at("database");
    Assign(database, "enabled", config.use_mysql);
    Assign(database, "host", config.database.host);
    Assign(database, "port", config.database.port);
    Assign(database, "name", config.database.database);
    Assign(database, "user", config.database.user);
    Assign(database, "password", config.database.password);
    Assign(database, "pool_size", config.database.pool_size);
    if (database.contains("acquire_timeout_ms"))
      config.database.acquire_timeout = std::chrono::milliseconds(
          database.at("acquire_timeout_ms").get<int>());
  }
  config.database.host = Env("CHATROOM_DB_HOST", config.database.host);
  config.database.database = Env("CHATROOM_DB_NAME", config.database.database);
  config.database.user = Env("CHATROOM_DB_USER", config.database.user);
  config.database.password = Env("CHATROOM_DB_PASSWORD", config.database.password);
  if (const char* value = std::getenv("CHATROOM_PORT"))
    config.reactor.port = static_cast<std::uint16_t>(std::stoul(value));
  if (config.reactor.port == 0 || config.worker_count == 0 ||
      config.worker_queue_capacity == 0 || config.reactor.maximum_connections == 0 ||
      config.reactor.maximum_output_bytes == 0 || config.reactor.result_queue_capacity == 0 ||
      config.database.pool_size == 0 || config.database.acquire_timeout.count() <= 0)
    throw std::invalid_argument("server configuration contains a non-positive limit");
  return config;
}

std::string ServerConfig::ToSafeString() const {
  nlohmann::json value{{"server", {{"address", reactor.address}, {"port", reactor.port},
      {"worker_count", worker_count}, {"worker_queue_capacity", worker_queue_capacity},
      {"maximum_connections", reactor.maximum_connections},
      {"maximum_output_bytes", reactor.maximum_output_bytes}}},
      {"database", {{"enabled", use_mysql}, {"host", database.host},
       {"port", database.port}, {"name", database.database}, {"user", database.user},
       {"pool_size", database.pool_size}, {"password", "<redacted>"}}}};
  return value.dump();
}

}  // namespace chatroom
