#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "server/config.h"

using namespace chatroom;

TEST(ConfigTest, EnvironmentOverridesJsonWithoutExposingSecret) {
  const std::string path = "/tmp/chatroom-config-test.json";
  {
    std::ofstream output(path);
    output << R"({"server":{"port":7777},"database":{"password":"file-secret"}})";
  }
  setenv("CHATROOM_DB_PASSWORD", "environment-secret", 1);
  const auto config = LoadConfig(path);
  unsetenv("CHATROOM_DB_PASSWORD");
  EXPECT_EQ(config.database.password, "environment-secret");
  EXPECT_EQ(config.ToSafeString().find("environment-secret"), std::string::npos);
  EXPECT_EQ(config.ToSafeString().find("file-secret"), std::string::npos);
}

TEST(ConfigTest, RejectsNonPositiveLimits) {
  const std::string path = "/tmp/chatroom-invalid-config-test.json";
  {
    std::ofstream output(path);
    output << R"({"server":{"port":7777,"worker_count":0}})";
  }
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
}
