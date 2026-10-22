#include "load_client/load_engine.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <charconv>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "load_client/histogram.h"
#include "protocol/frame_codec.h"

namespace chatroom::load {
using Clock = std::chrono::steady_clock;

struct LoadEngine::Impl {
  enum class State { Connecting, Hello, Identified, RoomPending, InRoom, Failed };
  struct Client {
    int fd{-1}; State state{State::Connecting}; std::string nickname;
    std::string output; std::size_t offset{0}; protocol::FrameDecoder decoder;
    std::uint64_t room_id{0};
  };
  explicit Impl(LoadConfig value) : config(std::move(value)) {}
  LoadConfig config; int epoll_fd{-1}; std::vector<Client> clients;
  std::unordered_map<int, std::size_t> by_fd;
  std::vector<std::uint64_t> room_ids;
  std::unordered_map<std::uint64_t, std::uint64_t> room_members;
  struct Pending { Clock::time_point started; std::uint64_t fanout; };
  std::unordered_map<std::uint64_t, Pending> pending;
  std::unordered_map<std::string, std::uint64_t> error_codes;
  std::string initialization_error;
  Histogram latency_us; std::uint64_t next_request{1}, sent{0}, broadcasts{0}, expected_broadcasts{0}, errors{0}, rejected{0}, transport_errors{0}, protocol_errors{0};

  ~Impl() {
    for (auto& client : clients) {
      if (client.fd >= 0) close(client.fd);
    }
    if (epoll_fd >= 0) close(epoll_fd);
  }

  void TransportError(Client& client) {
    if (client.state != State::Failed) { client.state=State::Failed; ++errors; ++transport_errors; }
  }

  void Queue(Client& client, const std::string& type, nlohmann::json payload) {
    const auto id = std::to_string(next_request++);
    nlohmann::json command{{"type", type}, {"request_id", id}, {"payload", std::move(payload)}};
    client.output += protocol::EncodeFrame(command.dump()); Flush(client);
  }
  void Flush(Client& client) {
    while (client.offset < client.output.size()) {
      const auto count = send(client.fd, client.output.data() + client.offset,
                              client.output.size() - client.offset, MSG_NOSIGNAL);
      if (count < 0) { if (errno == EINTR) continue; if (errno == EAGAIN) return; TransportError(client); return; }
      client.offset += static_cast<std::size_t>(count);
    }
    client.output.clear(); client.offset=0;
  }
  void Message(Client& client, const nlohmann::json& message) {
    const auto type = message.value("type", "");
    if (type == "response.ok" && client.state == State::Hello) client.state=State::Identified;
    else if (type == "room.snapshot") {
      client.room_id=message.at("room").at("id").get<std::uint64_t>(); client.state=State::InRoom;
    } else if (type == "chat.message") {
      ++broadcasts; const auto text=message.value("text", "");
      if (text.rfind("load:",0)==0 && message.value("sender","")==client.nickname) {
        const auto id=std::stoull(text.substr(5)); const auto found=pending.find(id);
        if(found!=pending.end()) { latency_us.Record(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-found->second.started).count())); pending.erase(found); }
      }
    } else if (type == "response.error") {
      ++errors;
      const auto code=message.at("error").value("code", "UNKNOWN");
      ++error_codes[code];
      if (client.state != State::InRoom && initialization_error.empty()) {
        initialization_error = code + ": " + message.at("error").value("message", "");
        client.state = State::Failed;
      }
      const auto request_id=message.value("request_id", "");
      std::uint64_t id=0;
      const auto parsed=std::from_chars(request_id.data(), request_id.data()+request_id.size(), id);
      if (parsed.ec == std::errc{} && parsed.ptr == request_id.data()+request_id.size()) {
        const auto found=pending.find(id);
        if (found!=pending.end()) {
          expected_broadcasts-=found->second.fanout;
          pending.erase(found);
          ++rejected;
        }
      }
    }
  }
  void Read(Client& client) {
    char buffer[16384];
    for (;;) {
      const auto count=recv(client.fd,buffer,sizeof(buffer),0);
      if(count==0){TransportError(client);return;}
      if(count<0){if(errno==EINTR)continue;if(errno==EAGAIN)return;TransportError(client);return;}
      auto batch=client.decoder.Push(std::string_view(buffer,static_cast<std::size_t>(count)));
      if(batch.error){client.state=State::Failed;++errors;++protocol_errors;return;}
      for(const auto& frame:batch.frames) try{Message(client,nlohmann::json::parse(frame));}catch(...){++errors;++protocol_errors;}
    }
  }
  void Poll(int timeout_ms) {
    epoll_event events[256]; const int count=epoll_wait(epoll_fd,events,256,timeout_ms);
    if(count<0){if(errno==EINTR)return;throw std::runtime_error("load epoll_wait failed");}
    for(int i=0;i<count;++i){auto found=by_fd.find(events[i].data.fd);if(found==by_fd.end())continue;auto& client=clients[found->second];
      if((events[i].events&(EPOLLERR|EPOLLHUP))!=0U){TransportError(client);continue;}
      if(client.state==State::Connecting && (events[i].events&EPOLLOUT)!=0U){int error=0;socklen_t size=sizeof(error);getsockopt(client.fd,SOL_SOCKET,SO_ERROR,&error,&size);if(error){TransportError(client);}else{client.state=State::Hello;Queue(client,"session.hello",{{"nickname",client.nickname}});}}
      if ((events[i].events & EPOLLIN) != 0U) Read(client);
      if ((events[i].events & EPOLLOUT) != 0U) Flush(client);
    }
  }
  template<class Predicate> void Wait(Predicate predicate, int seconds=15) {
    const auto deadline=Clock::now()+std::chrono::seconds(seconds);
    for (;;) {
      if (!initialization_error.empty())
        throw std::runtime_error("load initialization failed: " + initialization_error);
      for (const auto& client : clients)
        if (client.state == State::Failed)
          throw std::runtime_error("load initialization failed: connection closed");
      if (predicate()) return;
      if (Clock::now()>=deadline) throw std::runtime_error("load initialization timed out");
      Poll(10);
    }
  }
};

LoadEngine::LoadEngine(LoadConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {
  if(impl_->config.connections==0||impl_->config.rooms==0||impl_->config.rooms>impl_->config.connections||impl_->config.rate<=0||impl_->config.duration_seconds<=0)throw std::invalid_argument("invalid load configuration");
}
LoadEngine::~LoadEngine() = default;

LoadEngine::LoadEngine(LoadEngine&&) noexcept = default;
LoadEngine& LoadEngine::operator=(LoadEngine&&) noexcept = default;

nlohmann::json LoadEngine::Run() {
  impl_->epoll_fd=epoll_create1(EPOLL_CLOEXEC);if(impl_->epoll_fd<0)throw std::runtime_error("cannot create load epoll");
  sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(impl_->config.port);if(inet_pton(AF_INET,impl_->config.host.c_str(),&endpoint.sin_addr)!=1)throw std::invalid_argument("load host must be IPv4");
  impl_->clients.reserve(impl_->config.connections);
  for(std::size_t i=0;i<impl_->config.connections;++i){int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)throw std::runtime_error("load socket failed");Impl::Client client;client.fd=fd;client.nickname="load"+std::to_string(i);impl_->by_fd[fd]=impl_->clients.size();impl_->clients.push_back(std::move(client));epoll_event event{};event.events=EPOLLIN|EPOLLOUT|EPOLLET;event.data.fd=fd;if(epoll_ctl(impl_->epoll_fd,EPOLL_CTL_ADD,fd,&event)<0)throw std::runtime_error("load epoll add failed");const int result=connect(fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint));if(result<0&&errno!=EINPROGRESS)impl_->TransportError(impl_->clients.back());}
  impl_->Wait([&]{for(auto& c:impl_->clients)if(c.state!=Impl::State::Identified&&c.state!=Impl::State::Failed)return false;return true;});
  for(std::size_t i=0;i<impl_->config.rooms;++i){auto& c=impl_->clients[i];c.state=Impl::State::RoomPending;impl_->Queue(c,"room.create",{{"name","load-room-"+std::to_string(i)}});}
  impl_->Wait([&]{for(std::size_t i=0;i<impl_->config.rooms;++i)if(impl_->clients[i].state!=Impl::State::InRoom)return false;return true;});
  impl_->room_ids.resize(impl_->config.rooms);for(std::size_t i=0;i<impl_->config.rooms;++i)impl_->room_ids[i]=impl_->clients[i].room_id;
  for(std::size_t i=impl_->config.rooms;i<impl_->clients.size();++i){auto& c=impl_->clients[i];if(c.state!=Impl::State::Identified)continue;c.state=Impl::State::RoomPending;impl_->Queue(c,"room.join",{{"room_id",impl_->room_ids[i%impl_->config.rooms]}});}
  impl_->Wait([&]{for(auto& c:impl_->clients)if(c.state!=Impl::State::InRoom&&c.state!=Impl::State::Failed)return false;return true;});
  for (const auto& client : impl_->clients)
    if (client.state == Impl::State::InRoom) ++impl_->room_members[client.room_id];
  const auto start=Clock::now();auto previous=start;double tokens=0;std::size_t cursor=0;
  while(Clock::now()-start<std::chrono::seconds(impl_->config.duration_seconds)){const auto now=Clock::now();tokens+=std::chrono::duration<double>(now-previous).count()*impl_->config.rate;previous=now;while(tokens>=1.0){auto& c=impl_->clients[cursor++%impl_->clients.size()];if(c.state==Impl::State::InRoom){const auto id=impl_->next_request;const auto fanout=impl_->room_members[c.room_id];impl_->pending[id]={Clock::now(),fanout};impl_->Queue(c,"chat.send",{{"text","load:"+std::to_string(id)}});++impl_->sent;impl_->expected_broadcasts+=fanout;}tokens-=1.0;}impl_->Poll(1);}
  const auto drain=Clock::now()+std::chrono::seconds(2);while((!impl_->pending.empty()||impl_->broadcasts<impl_->expected_broadcasts)&&Clock::now()<drain)impl_->Poll(10);
  utsname system{};uname(&system);const auto elapsed=std::chrono::duration<double>(Clock::now()-start).count();
  const auto wall_time=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return {{"complete",impl_->errors==0&&impl_->pending.empty()&&impl_->broadcasts==impl_->expected_broadcasts},{"utc_epoch_seconds",wall_time},{"environment",{{"os",std::string(system.sysname)+" "+system.release},{"logical_cpus",sysconf(_SC_NPROCESSORS_ONLN)},{"compiler",__VERSION__},{"build_type",CHATROOM_BUILD_TYPE}}},{"parameters",{{"host",impl_->config.host},{"port",impl_->config.port},{"connections",impl_->config.connections},{"rooms",impl_->config.rooms},{"target_rate",impl_->config.rate},{"duration_seconds",impl_->config.duration_seconds}}},{"counts",{{"sent",impl_->sent},{"broadcasts",impl_->broadcasts},{"expected_broadcasts",impl_->expected_broadcasts},{"errors",impl_->errors},{"server_rejected",impl_->rejected},{"transport_errors",impl_->transport_errors},{"protocol_errors",impl_->protocol_errors},{"error_codes",impl_->error_codes},{"unmatched",impl_->pending.size()}}},{"actual_rate",static_cast<double>(impl_->sent)/elapsed},{"latency_us",{{"samples",impl_->latency_us.size()},{"p50",impl_->latency_us.Percentile(.50)},{"p95",impl_->latency_us.Percentile(.95)},{"p99",impl_->latency_us.Percentile(.99)}}}};
}

void LoadEngine::WriteAtomic(const std::string& path, const nlohmann::json& result) {
  const std::filesystem::path destination(path);
  auto temporary = destination;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open benchmark output");
    output << result.dump(2) << '\n';
    output.flush();
    if (!output) throw std::runtime_error("cannot flush benchmark output");
  }
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) throw std::runtime_error("cannot atomically rename benchmark output: " + error.message());
}
}  // namespace chatroom::load
