#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "application/chat_service.h"
#include "common/keyed_executor.h"
#include "common/logger.h"
#include "domain/room_manager.h"
#include "persistence/chat_repository.h"
#include "persistence/in_memory_chat_repository.h"
#ifdef CHATROOM_HAS_MYSQL
#include "persistence/mysql_chat_repository.h"
#endif
#include "server/config.h"
#include "server/reactor.h"

namespace {
chatroom::Reactor* active_reactor = nullptr;
extern "C" void Stop(int) { if (active_reactor) active_reactor->RequestStop(); }
}

int main(int argc, char** argv) {
  std::string config_path = "config/server.example.json";
  if (argc == 3 && std::string(argv[1]) == "--config") config_path = argv[2];
  else if (argc != 1) { std::cerr << "usage: chatroom_server [--config path]\n"; return 2; }
  chatroom::Logger logger;
  try {
    const auto config = chatroom::LoadConfig(config_path);
    logger.Info("server.configuration", nlohmann::json::parse(config.ToSafeString()));
    chatroom::RoomManager rooms;
    std::unique_ptr<chatroom::IChatRepository> repository;
#ifdef CHATROOM_HAS_MYSQL
    std::unique_ptr<chatroom::MySqlConnectionPool> pool;
    if (config.use_mysql) {
      pool = std::make_unique<chatroom::MySqlConnectionPool>(config.database);
      auto mysql = std::make_unique<chatroom::MySqlChatRepository>(*pool);
      mysql->ValidateSchema();
      repository = std::move(mysql);
    }
#else
    if (config.use_mysql) throw std::runtime_error("server was built without MySQL Connector/C++");
#endif
    if (!repository) repository = std::make_unique<chatroom::InMemoryChatRepository>();
    for (const auto& room : repository->LoadActiveRooms())
      if (!rooms.CreateLoadedRoom(room.id, room.name))
        throw std::runtime_error("duplicate room id while loading repository");
    chatroom::ChatService service(rooms, *repository);
    chatroom::KeyedExecutor executor(config.worker_count, config.worker_queue_capacity,
      [&logger](std::exception_ptr) { logger.Error("worker.exception"); });
    chatroom::Reactor reactor(config.reactor, executor, service);
    active_reactor = &reactor;
    std::signal(SIGINT, Stop); std::signal(SIGTERM, Stop); std::signal(SIGPIPE, SIG_IGN);
    logger.Info("server.started", {{"port", reactor.bound_port()}});
    reactor.Run();
    active_reactor = nullptr;
    executor.Shutdown();
    logger.Info("server.stopped");
    return 0;
  } catch (const std::exception& error) {
    logger.Error("server.fatal", {{"message", error.what()}});
    return 1;
  }
}
