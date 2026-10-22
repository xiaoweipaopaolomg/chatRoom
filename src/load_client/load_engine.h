#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace chatroom::load {
struct LoadConfig {
  std::string host{"127.0.0.1"};
  std::uint16_t port{7777};
  std::size_t connections{20};
  std::size_t rooms{4};
  double rate{100.0};
  int duration_seconds{10};
  std::string output{"benchmark.json"};
};

class LoadEngine {
 public:
  explicit LoadEngine(LoadConfig config);
  ~LoadEngine();
  LoadEngine(const LoadEngine&) = delete;
  LoadEngine& operator=(const LoadEngine&) = delete;
  LoadEngine(LoadEngine&&) noexcept;
  LoadEngine& operator=(LoadEngine&&) noexcept;
  nlohmann::json Run();
  static void WriteAtomic(const std::string& path, const nlohmann::json& result);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace chatroom::load
