#include "protocol/frame_codec.h"

#include <limits>
#include <stdexcept>

namespace chatroom::protocol {

std::string EncodeFrame(std::string_view body) {
  if (body.empty() || body.size() > kMaximumFrameBodyBytes ||
      body.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("frame body must contain 1..65536 bytes");
  }
  const auto length = static_cast<std::uint32_t>(body.size());
  std::string encoded(4, '\0');
  encoded[0] = static_cast<char>((length >> 24U) & 0xffU);
  encoded[1] = static_cast<char>((length >> 16U) & 0xffU);
  encoded[2] = static_cast<char>((length >> 8U) & 0xffU);
  encoded[3] = static_cast<char>(length & 0xffU);
  encoded.append(body.data(), body.size());
  return encoded;
}

FrameDecoder::FrameDecoder(std::size_t maximum_body_bytes)
    : maximum_body_bytes_(maximum_body_bytes) {
  if (maximum_body_bytes == 0 ||
      maximum_body_bytes > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("maximum frame body size is invalid");
  }
}

DecodeBatch FrameDecoder::Push(std::string_view bytes) {
  DecodeBatch output;
  if (failed_) {
    output.error = Error{ErrorCode::kInvalidMessage, "frame decoder is in a failed state"};
    return output;
  }
  buffer_.append(bytes.data(), bytes.size());

  std::size_t consumed = 0;
  while (buffer_.size() - consumed >= 4) {
    const auto* header = reinterpret_cast<const unsigned char*>(buffer_.data() + consumed);
    const std::uint32_t length = (static_cast<std::uint32_t>(header[0]) << 24U) |
                                 (static_cast<std::uint32_t>(header[1]) << 16U) |
                                 (static_cast<std::uint32_t>(header[2]) << 8U) |
                                 static_cast<std::uint32_t>(header[3]);
    if (length == 0 || static_cast<std::size_t>(length) > maximum_body_bytes_) {
      failed_ = true;
      buffer_.clear();
      output.frames.clear();
      output.error = Error{ErrorCode::kInvalidMessage, "frame length is outside 1..65536 bytes"};
      return output;
    }
    const std::size_t frame_size = 4U + static_cast<std::size_t>(length);
    if (buffer_.size() - consumed < frame_size) {
      break;
    }
    output.frames.emplace_back(buffer_.data() + consumed + 4U, length);
    consumed += frame_size;
  }

  if (consumed != 0) {
    buffer_.erase(0, consumed);
  }
  return output;
}

}  // namespace chatroom::protocol
