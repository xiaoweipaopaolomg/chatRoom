add_library(chatroom_protocol
  frame_codec.cpp
  message.cpp
)
target_include_directories(chatroom_protocol PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(chatroom_protocol PUBLIC chatroom_common nlohmann_json::nlohmann_json)
chatroom_target_defaults(chatroom_protocol)
