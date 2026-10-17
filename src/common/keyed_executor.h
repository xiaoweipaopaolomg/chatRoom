#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace chatroom {

class KeyedExecutor {
 public:
  using Task = std::function<void()>;
  using ErrorHandler = std::function<void(std::exception_ptr)>;

  KeyedExecutor(std::size_t workers, std::size_t capacity_per_worker,
                ErrorHandler on_error = {});
  ~KeyedExecutor();

  KeyedExecutor(const KeyedExecutor&) = delete;
  KeyedExecutor& operator=(const KeyedExecutor&) = delete;

  bool Submit(std::uint64_t key, Task task);
  void Shutdown() noexcept;
  std::size_t worker_count() const noexcept;

 private:
  struct Lane;
  std::vector<std::unique_ptr<Lane>> lanes_;
  ErrorHandler on_error_;
};

}  // namespace chatroom
