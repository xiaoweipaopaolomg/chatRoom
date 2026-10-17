#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
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
