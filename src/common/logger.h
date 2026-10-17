#pragma once

#include <mutex>
#include <string_view>

#include <nlohmann/json.hpp>

namespace chatroom {

class Logger {
 public:
  void Log(std::string_view level, std::string_view event,
           nlohmann::json fields = nlohmann::json::object());
  void Info(std::string_view event, nlohmann::json fields = nlohmann::json::object()) {
    Log("info", event, std::move(fields));
  }
  void Error(std::string_view event, nlohmann::json fields = nlohmann::json::object()) {
    Log("error", event, std::move(fields));
  }
 private:
  std::mutex mutex_;
};

}  // namespace chatroom
