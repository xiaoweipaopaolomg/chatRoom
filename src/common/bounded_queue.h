#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace chatroom {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("bounded queue capacity must be positive");
    }
  }

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;
  ~BoundedQueue() { Close(); }

  bool TryPush(T value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ || values_.size() >= capacity_) {
        return false;
      }
      values_.push_back(std::move(value));
    }
    ready_.notify_one();
    return true;
  }

  bool WaitPush(T value) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      space_.wait(lock, [this] { return closed_ || values_.size() < capacity_; });
      if (closed_) return false;
      values_.push_back(std::move(value));
    }
    ready_.notify_one();
    return true;
  }

  std::optional<T> WaitPop() {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !values_.empty(); });
    if (values_.empty()) {
      return std::nullopt;
    }
    T value = std::move(values_.front());
    values_.pop_front();
    lock.unlock();
    space_.notify_one();
    return value;
  }

  std::optional<T> TryPop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (values_.empty()) {
      return std::nullopt;
    }
    T value = std::move(values_.front());
    values_.pop_front();
    lock.unlock();
    space_.notify_one();
    return value;
  }

  void Close() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    ready_.notify_all();
    space_.notify_all();
  }

  std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.size();
  }

  bool closed() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::condition_variable space_;
  std::deque<T> values_;
  bool closed_{false};
};

}  // namespace chatroom
