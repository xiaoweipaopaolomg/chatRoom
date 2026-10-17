#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sql { class Connection; }

namespace chatroom {

struct MySqlPoolConfig {
  std::string host{"127.0.0.1"};
  std::uint16_t port{3306};
  std::string database{"chatroom"};
  std::string user{"chatroom"};
  std::string password;
  std::size_t pool_size{4};
  std::chrono::milliseconds acquire_timeout{1000};
};

class MySqlConnectionPool {
 public:
  class Lease {
   public:
    Lease() = default;
    ~Lease();
    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    sql::Connection& operator*() const;
    sql::Connection* operator->() const noexcept;
    explicit operator bool() const noexcept;
    void MarkBroken() noexcept { broken_ = true; }

   private:
    friend class MySqlConnectionPool;
    Lease(MySqlConnectionPool* pool, std::unique_ptr<sql::Connection> connection);
    void Reset() noexcept;
    MySqlConnectionPool* pool_{nullptr};
    std::unique_ptr<sql::Connection> connection_;
    bool broken_{false};
  };

  explicit MySqlConnectionPool(MySqlPoolConfig config);
  ~MySqlConnectionPool();
  MySqlConnectionPool(const MySqlConnectionPool&) = delete;
  MySqlConnectionPool& operator=(const MySqlConnectionPool&) = delete;

  Lease Acquire();
  const MySqlPoolConfig& config() const noexcept;

 private:
  struct Impl;
  void Release(std::unique_ptr<sql::Connection> connection, bool broken) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chatroom
