#pragma once
#include <cstdint>
#include <vector>

namespace chatroom::load {
class Histogram {
 public:
  void Record(std::uint64_t value) { values_.push_back(value); }
  std::uint64_t Percentile(double fraction) const;
  std::size_t size() const noexcept { return values_.size(); }
 private:
  std::vector<std::uint64_t> values_;
};
}  // namespace chatroom::load
