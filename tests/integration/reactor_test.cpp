#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "application/chat_service.h"
#include "common/keyed_executor.h"
#include "domain/room_manager.h"
#include "persistence/in_memory_chat_repository.h"
#include "protocol/frame_codec.h"
#include "server/reactor.h"

using namespace chatroom;

namespace {
int Connect(std::uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  timeval timeout{2, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(fd); return -1;
  }
  return fd;
}

std::vector<std::string> ReadFrames(int fd, std::size_t wanted) {
  protocol::FrameDecoder decoder;
  std::vector<std::string> frames;
  char bytes[4096];
  while (frames.size() < wanted) {
    const auto count = recv(fd, bytes, sizeof(bytes), 0);
    if (count <= 0) break;
    auto batch = decoder.Push(std::string_view(bytes, static_cast<std::size_t>(count)));
    frames.insert(frames.end(), batch.frames.begin(), batch.frames.end());
  }
  return frames;
}
}  // namespace

TEST(ReactorTest, CommandsFromOneWriteObserveFifoOrder) {
  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service(rooms, repository);
  KeyedExecutor executor(2, 16);
  ReactorConfig config; config.address = "127.0.0.1"; config.port = 0;
  Reactor reactor(config, executor, service);
  std::thread server([&] { reactor.Run(); });
  const int client = Connect(reactor.bound_port());
  ASSERT_GE(client, 0);
  const std::string hello = R"({"type":"session.hello","request_id":"1","payload":{"nickname":"alice"}})";
  const std::string create = R"({"type":"room.create","request_id":"2","payload":{"name":"C++"}})";
  const auto bytes = protocol::EncodeFrame(hello) + protocol::EncodeFrame(create);
  ASSERT_EQ(send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL),
            static_cast<ssize_t>(bytes.size()));
  const auto frames = ReadFrames(client, 2);
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(nlohmann::json::parse(frames[0]).at("type"), "response.ok");
  EXPECT_EQ(nlohmann::json::parse(frames[1]).at("type"), "room.snapshot");
  close(client);
  reactor.RequestStop(); server.join(); executor.Shutdown();
}

TEST(ReactorTest, FragmentedFrameIsReassembled) {
  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service(rooms, repository);
  KeyedExecutor executor(1, 8);
  ReactorConfig config; config.address = "127.0.0.1"; config.port = 0;
  Reactor reactor(config, executor, service);
  std::thread server([&] { reactor.Run(); });
  const int client = Connect(reactor.bound_port()); ASSERT_GE(client, 0);
  const auto bytes = protocol::EncodeFrame(
      R"({"type":"ping","request_id":"p","payload":{"client_time":9}})");
  ASSERT_EQ(send(client, bytes.data(), 2, MSG_NOSIGNAL), 2);
  ASSERT_EQ(send(client, bytes.data() + 2, bytes.size() - 2, MSG_NOSIGNAL),
            static_cast<ssize_t>(bytes.size() - 2));
  const auto frames = ReadFrames(client, 1);
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(nlohmann::json::parse(frames[0]).at("type"), "pong");
  close(client); reactor.RequestStop(); server.join(); executor.Shutdown();
}

TEST(ReactorTest, FullResultQueueDoesNotDropPongResponses) {
  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service(rooms, repository);
  KeyedExecutor executor(1, 128);
  ReactorConfig config; config.address = "127.0.0.1"; config.port = 0;
  config.result_queue_capacity = 1;
  Reactor reactor(config, executor, service);
  std::thread server([&] { reactor.Run(); });
  const int client = Connect(reactor.bound_port());
  ASSERT_GE(client, 0);
  std::string bytes;
  for (int index = 0; index < 100; ++index)
    bytes += protocol::EncodeFrame(nlohmann::json{{"type", "ping"},
        {"request_id", std::to_string(index)}, {"payload", nlohmann::json::object()}}.dump());
  ASSERT_EQ(send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL),
            static_cast<ssize_t>(bytes.size()));
  const auto frames = ReadFrames(client, 100);
  EXPECT_EQ(frames.size(), 100U);
  close(client); reactor.RequestStop(); server.join(); executor.Shutdown();
}

TEST(ReactorTest, FullWorkerQueueEventuallyCleansDisconnectedSession) {
  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service(rooms, repository);
  KeyedExecutor executor(1, 1);
  ReactorConfig config; config.address = "127.0.0.1"; config.port = 0;
  Reactor reactor(config, executor, service);
  std::thread server([&] { reactor.Run(); });
  const int client = Connect(reactor.bound_port());
  ASSERT_GE(client, 0);
  const auto hello = protocol::EncodeFrame(
      R"({"type":"session.hello","request_id":"h","payload":{"nickname":"alice"}})");
  ASSERT_EQ(send(client, hello.data(), hello.size(), MSG_NOSIGNAL),
            static_cast<ssize_t>(hello.size()));
  ASSERT_EQ(ReadFrames(client, 1).size(), 1U);
  ASSERT_EQ(rooms.SessionCount(), 1U);
  std::promise<void> entered;
  std::promise<void> release;
  auto allow_finish = release.get_future().share();
  ASSERT_TRUE(executor.Submit(0, [&] { entered.set_value(); allow_finish.wait(); }));
  ASSERT_EQ(entered.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready);
  ASSERT_TRUE(executor.Submit(0, [] {}));
  close(client);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  release.set_value();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (rooms.SessionCount() != 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT_EQ(rooms.SessionCount(), 0U);
  reactor.RequestStop(); server.join(); executor.Shutdown();
}

TEST(ReactorTest, ShutdownDrainsAlreadySubmittedResponses) {
  RoomManager rooms;
  InMemoryChatRepository repository;
  ChatService service(rooms, repository);
  KeyedExecutor executor(1, 128);
  ReactorConfig config; config.address = "127.0.0.1"; config.port = 0;
  config.result_queue_capacity = 1;
  Reactor reactor(config, executor, service);
  std::thread server([&] { reactor.Run(); });
  const int client = Connect(reactor.bound_port());
  ASSERT_GE(client, 0);

  std::promise<void> release;
  auto allow_finish = release.get_future().share();
  ASSERT_TRUE(executor.Submit(0, [&] { allow_finish.wait(); }));
  std::string bytes;
  for (int index = 0; index < 100; ++index)
    bytes += protocol::EncodeFrame(nlohmann::json{{"type", "ping"},
        {"request_id", std::to_string(index)}, {"payload", nlohmann::json::object()}}.dump());
  EXPECT_EQ(send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL),
            static_cast<ssize_t>(bytes.size()));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (executor.pending() < 101 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const bool submitted = executor.pending() == 101;
  reactor.RequestStop();
  release.set_value();
  const auto frames = ReadFrames(client, 100);
  server.join(); executor.Shutdown(); close(client);
  EXPECT_TRUE(submitted);
  EXPECT_EQ(frames.size(), 100U);
}
