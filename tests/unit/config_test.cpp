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

TEST(ConfigTest, RejectsOutOfRangeEnvironmentPort) {
  const std::string path = "/tmp/chatroom-invalid-port-config-test.json";
  { std::ofstream output(path); output << R"({"server":{"port":7777}})"; }
  setenv("CHATROOM_PORT", "65537", 1);
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  unsetenv("CHATROOM_PORT");
}

TEST(ConfigTest, RejectsOutOfRangeJsonPort) {
  const std::string path = "/tmp/chatroom-invalid-json-port-test.json";
  { std::ofstream output(path); output << R"({"server":{"port":65537}})"; }
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
}

TEST(ConfigTest, WorkerAndPoolEnvironmentOverridesAreValidated) {
  const std::string path = "/tmp/chatroom-worker-pool-config-test.json";
  { std::ofstream output(path); output << R"({"server":{"worker_count":4},"database":{"pool_size":8}})"; }
  setenv("CHATROOM_WORKER_COUNT", "16", 1);
  setenv("CHATROOM_DB_POOL_SIZE", "16", 1);
  const auto config = LoadConfig(path);
  EXPECT_EQ(config.worker_count, 16U);
  EXPECT_EQ(config.database.pool_size, 16U);
  setenv("CHATROOM_WORKER_COUNT", "-1", 1);
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  unsetenv("CHATROOM_WORKER_COUNT");
  setenv("CHATROOM_DB_POOL_SIZE", "not-a-number", 1);
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  unsetenv("CHATROOM_DB_POOL_SIZE");
}

TEST(ConfigTest, RejectsInvalidMessageBatchSettings) {
  const std::string path = "/tmp/chatroom-invalid-batch-config-test.json";
  {
    std::ofstream output(path);
    output << R"({"database":{"pool_size":2,"message_batch":{"writer_count":3}}})";
  }
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  {
    std::ofstream output(path);
    output << R"({"database":{"pool_size":4,"message_batch":{"writer_count":2,"max_batch_size":0}}})";
  }
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
}

TEST(ConfigTest, BatchWriterEnvironmentOverrideAllowsDisabledAndValidatesBounds) {
  const std::string path = "/tmp/chatroom-batch-override-config-test.json";
  {
    std::ofstream output(path);
    output << R"({"database":{"pool_size":8,"message_batch":{"writer_count":4}}})";
  }
  setenv("CHATROOM_BATCH_WRITER_COUNT", "0", 1);
  EXPECT_EQ(LoadConfig(path).message_batch.writer_count, 0U);
  setenv("CHATROOM_BATCH_WRITER_COUNT", "8", 1);
  EXPECT_EQ(LoadConfig(path).message_batch.writer_count, 8U);
  setenv("CHATROOM_BATCH_WRITER_COUNT", "9", 1);
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  setenv("CHATROOM_BATCH_WRITER_COUNT", "-1", 1);
  EXPECT_THROW((void)LoadConfig(path), std::invalid_argument);
  unsetenv("CHATROOM_BATCH_WRITER_COUNT");
}
