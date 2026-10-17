#include "server/connection.h"

#include <algorithm>
#include <stdexcept>

namespace chatroom {

bool Connection::QueueOutput(std::string bytes) {
  const std::size_t pending = output_.size() - output_offset_;
  if (bytes.size() > maximum_output_bytes_ - std::min(maximum_output_bytes_, pending)) {
    return false;
  }
  if (output_offset_ != 0 && output_offset_ >= output_.size() / 2) {
    output_.erase(0, output_offset_);
    output_offset_ = 0;
  }
  output_.append(bytes);
  return true;
}

std::string_view Connection::PendingOutput() const noexcept {
  return std::string_view(output_).substr(output_offset_);
}

void Connection::ConsumeWritten(std::size_t bytes) {
  const auto pending = output_.size() - output_offset_;
  if (bytes > pending) throw std::out_of_range("written byte count exceeds output buffer");
  output_offset_ += bytes;
  if (output_offset_ == output_.size()) {
    output_.clear();
    output_offset_ = 0;
  }
}

}  // namespace chatroom
