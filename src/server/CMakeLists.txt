add_library(chatroom_server_core
  connection.cpp
  socket_utils.cpp
  reactor.cpp
  config.cpp
  ../common/logger.cpp)
target_include_directories(chatroom_server_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(chatroom_server_core PUBLIC
  chatroom_application chatroom_common chatroom_protocol nlohmann_json::nlohmann_json)
chatroom_target_defaults(chatroom_server_core)

add_executable(chatroom_server main.cpp)
target_link_libraries(chatroom_server PRIVATE
  chatroom_server_core chatroom_persistence)
if(TARGET chatroom_mysql_persistence)
  target_link_libraries(chatroom_server PRIVATE chatroom_mysql_persistence)
endif()
chatroom_target_defaults(chatroom_server)
