add_library(chatroom_common
  keyed_executor.cpp
)
target_include_directories(chatroom_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
chatroom_target_defaults(chatroom_common)
