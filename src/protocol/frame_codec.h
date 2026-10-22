#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.h"
#include "protocol/limits.h"

namespace chatroom::protocol {

std::string EncodeFrame(std::string_view body);

struct DecodeBatch {
  std::vector<std::string> frames;
  std::optional<Error> error;
};

class FrameDecoder {
 public:
  explicit FrameDecoder(std::size_t maximum_body_bytes = kMaximumFrameBodyBytes);
  DecodeBatch Push(std::string_view bytes);
  bool failed() const noexcept { return failed_; }
  std::size_t buffered_bytes() const noexcept { return buffer_.size(); }

 private:
  std::size_t maximum_body_bytes_;
  std::string buffer_;
  bool failed_{false};
};

}  // namespace chatroom::protocol
