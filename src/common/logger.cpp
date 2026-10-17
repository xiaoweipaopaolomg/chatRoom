#include "common/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace chatroom {

void Logger::Log(std::string_view level, std::string_view event, nlohmann::json fields) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream timestamp;
  timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  fields["timestamp"] = timestamp.str();
  fields["level"] = level;
  fields["event"] = event;
  std::lock_guard<std::mutex> lock(mutex_);
  std::clog << fields.dump() << '\n';
}

}  // namespace chatroom
