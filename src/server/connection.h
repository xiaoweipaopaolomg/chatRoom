#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "common/types.h"
#include "protocol/frame_codec.h"

namespace chatroom {

class Connection {
 public:
  Connection(int fd, ConnectionRef reference, std::size_t maximum_output_bytes)
      : fd_(fd), reference_(reference), maximum_output_bytes_(maximum_output_bytes) {}
  int fd() const noexcept { return fd_; }
  ConnectionRef reference() const noexcept { return reference_; }
  protocol::FrameDecoder& decoder() noexcept { return decoder_; }
  bool QueueOutput(std::string bytes);
  std::string_view PendingOutput() const noexcept;
  void ConsumeWritten(std::size_t bytes);
  bool has_output() const noexcept { return output_offset_ < output_.size(); }

 private:
  int fd_;
  ConnectionRef reference_;
  std::size_t maximum_output_bytes_;
  protocol::FrameDecoder decoder_;
  std::string output_;
  std::size_t output_offset_{0};
};

}  // namespace chatroom
