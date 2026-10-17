#include "persistence/mysql_connection_pool.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <cppconn/connection.h>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <mysql_driver.h>

#include "persistence/chat_repository.h"

namespace chatroom {

struct MySqlConnectionPool::Impl {
  explicit Impl(MySqlPoolConfig value) : config(std::move(value)) {}
  MySqlPoolConfig config;
  sql::Driver* driver{nullptr};
  std::mutex mutex;
  std::condition_variable available;
  std::deque<std::unique_ptr<sql::Connection>> idle;
  bool closing{false};
};

namespace {
std::unique_ptr<sql::Connection> Connect(sql::Driver& driver,
                                         const MySqlPoolConfig& config) {
  const auto uri = "tcp://" + config.host + ":" + std::to_string(config.port);
  std::unique_ptr<sql::Connection> connection(
      driver.connect(uri, config.user, config.password));
  connection->setSchema(config.database);
  return connection;
}
}  // namespace

MySqlConnectionPool::MySqlConnectionPool(MySqlPoolConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {
  if (impl_->config.pool_size == 0 || impl_->config.acquire_timeout.count() <= 0) {
    throw std::invalid_argument("MySQL pool size and timeout must be positive");
  }
  try {
    impl_->driver = sql::mysql::get_mysql_driver_instance();
    for (std::size_t i = 0; i < impl_->config.pool_size; ++i) {
      impl_->idle.push_back(Connect(*impl_->driver, impl_->config));
    }
  } catch (const sql::SQLException& error) {
    throw RepositoryException(ErrorCode::kDbError,
                              "failed to initialize MySQL connection pool: SQLSTATE " +
                                  error.getSQLState());
  }
}

MySqlConnectionPool::~MySqlConnectionPool() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->closing = true;
  impl_->idle.clear();
  impl_->available.notify_all();
}

MySqlConnectionPool::Lease MySqlConnectionPool::Acquire() {
  std::unique_lock<std::mutex> lock(impl_->mutex);
  const bool ready = impl_->available.wait_for(lock, impl_->config.acquire_timeout,
      [this] { return impl_->closing || !impl_->idle.empty(); });
  if (!ready || impl_->idle.empty()) {
    throw RepositoryException(ErrorCode::kDbBusy, "database pool acquire timed out");
  }
  auto connection = std::move(impl_->idle.front());
  impl_->idle.pop_front();
  return Lease(this, std::move(connection));
}

const MySqlPoolConfig& MySqlConnectionPool::config() const noexcept {
  return impl_->config;
}

void MySqlConnectionPool::Release(std::unique_ptr<sql::Connection> connection,
                                  bool broken) noexcept {
  if (broken) {
    try {
      connection = Connect(*impl_->driver, impl_->config);
    } catch (...) {
      connection.reset();
    }
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->closing && connection) {
    impl_->idle.push_back(std::move(connection));
    impl_->available.notify_one();
  }
}

MySqlConnectionPool::Lease::Lease(
    MySqlConnectionPool* pool, std::unique_ptr<sql::Connection> connection)
    : pool_(pool), connection_(std::move(connection)) {}

MySqlConnectionPool::Lease::~Lease() { Reset(); }

MySqlConnectionPool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      connection_(std::move(other.connection_)),
      broken_(std::exchange(other.broken_, false)) {}

MySqlConnectionPool::Lease& MySqlConnectionPool::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    Reset();
    pool_ = std::exchange(other.pool_, nullptr);
    connection_ = std::move(other.connection_);
    broken_ = std::exchange(other.broken_, false);
  }
  return *this;
}

sql::Connection& MySqlConnectionPool::Lease::operator*() const { return *connection_; }
sql::Connection* MySqlConnectionPool::Lease::operator->() const noexcept {
  return connection_.get();
}
MySqlConnectionPool::Lease::operator bool() const noexcept {
  return connection_ != nullptr;
}
void MySqlConnectionPool::Lease::Reset() noexcept {
  if (pool_ && connection_) pool_->Release(std::move(connection_), broken_);
  pool_ = nullptr;
}

}  // namespace chatroom
