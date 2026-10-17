#include <exception>
#include <cstdlib>
#include <iostream>
#include <string>
#include "load_client/load_engine.h"

int main(int argc,char** argv){chatroom::load::LoadConfig config;try{std::string command;for(int i=0;i<argc;++i){if(i)command+=' ';command+=argv[i];}for(int i=1;i<argc;++i){const std::string key=argv[i];if(i+1>=argc)throw std::invalid_argument("missing option value");const std::string value=argv[++i];if(key=="--host")config.host=value;else if(key=="--port")config.port=static_cast<std::uint16_t>(std::stoul(value));else if(key=="--connections")config.connections=std::stoull(value);else if(key=="--rooms")config.rooms=std::stoull(value);else if(key=="--rate")config.rate=std::stod(value);else if(key=="--duration")config.duration_seconds=std::stoi(value);else if(key=="--output")config.output=value;else throw std::invalid_argument("unknown option: "+key);}chatroom::load::LoadEngine engine(config);auto result=engine.Run();result["command"]=command;if(const char* commit=std::getenv("CHATROOM_GIT_COMMIT"))result["git_commit"]=commit;if(const char* persistence=std::getenv("CHATROOM_BENCHMARK_PERSISTENCE"))result["persistence"]=persistence;chatroom::load::LoadEngine::WriteAtomic(config.output,result);std::cout<<result.dump()<<'\n';return result.at("counts").at("errors").get<std::uint64_t>()==0?0:1;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
