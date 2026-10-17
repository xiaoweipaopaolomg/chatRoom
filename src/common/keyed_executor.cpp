#include "common/keyed_executor.h"

#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#include "common/bounded_queue.h"

namespace chatroom {

struct KeyedExecutor::Lane {
  explicit Lane(std::size_t capacity) : queue(capacity) {}
  BoundedQueue<Task> queue;
  std::thread worker;
};

KeyedExecutor::KeyedExecutor(std::size_t workers, std::size_t capacity_per_worker,
                             ErrorHandler on_error)
    : on_error_(std::move(on_error)) {
  if (workers == 0) {
    throw std::invalid_argument("keyed executor worker count must be positive");
  }
  lanes_.reserve(workers);
  for (std::size_t index = 0; index < workers; ++index) {
    lanes_.push_back(std::make_unique<Lane>(capacity_per_worker));
  }
  for (auto& lane : lanes_) {
    Lane* current = lane.get();
    current->worker = std::thread([this, current] {
      while (auto task = current->queue.WaitPop()) {
        try {
          (*task)();
        } catch (...) {
          if (on_error_) {
            try {
              on_error_(std::current_exception());
            } catch (...) {
              // An error callback must not terminate a worker lane.
            }
          }
        }
      }
    });
  }
}

KeyedExecutor::~KeyedExecutor() { Shutdown(); }

bool KeyedExecutor::Submit(std::uint64_t key, Task task) {
  if (!task) {
    return false;
  }
  return lanes_[key % lanes_.size()]->queue.TryPush(std::move(task));
}

void KeyedExecutor::Shutdown() noexcept {
  for (auto& lane : lanes_) {
    lane->queue.Close();
  }
  for (auto& lane : lanes_) {
    if (lane->worker.joinable()) {
      lane->worker.join();
    }
  }
}

std::size_t KeyedExecutor::worker_count() const noexcept { return lanes_.size(); }

}  // namespace chatroom
