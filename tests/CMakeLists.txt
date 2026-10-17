function(chatroom_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE
    GTest::gtest_main
    chatroom_application
    chatroom_common
    chatroom_domain
    chatroom_persistence
    chatroom_protocol)
  chatroom_target_defaults(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

chatroom_add_test(types_test unit/types_test.cpp)
chatroom_add_test(frame_codec_test unit/frame_codec_test.cpp)
chatroom_add_test(message_test unit/message_test.cpp)
chatroom_add_test(bounded_queue_test unit/bounded_queue_test.cpp)
chatroom_add_test(keyed_executor_test unit/keyed_executor_test.cpp)
chatroom_add_test(room_manager_test unit/room_manager_test.cpp)
chatroom_add_test(in_memory_repository_test unit/in_memory_repository_test.cpp)
chatroom_add_test(chat_service_test unit/chat_service_test.cpp)

if(TARGET chatroom_server_core)
  add_executable(connection_test unit/connection_test.cpp)
  target_link_libraries(connection_test PRIVATE GTest::gtest_main chatroom_server_core)
  chatroom_target_defaults(connection_test)
  add_test(NAME connection_test COMMAND connection_test)

  add_executable(reactor_test integration/reactor_test.cpp)
  target_link_libraries(reactor_test PRIVATE GTest::gtest_main chatroom_server_core
    chatroom_persistence)
  chatroom_target_defaults(reactor_test)
  add_test(NAME reactor_test COMMAND reactor_test)

  add_executable(config_test unit/config_test.cpp)
  target_link_libraries(config_test PRIVATE GTest::gtest_main chatroom_server_core)
  chatroom_target_defaults(config_test)
  add_test(NAME config_test COMMAND config_test)
endif()

if(TARGET chatroom_mysql_persistence)
  add_executable(mysql_repository_test integration/mysql_repository_test.cpp)
  target_link_libraries(mysql_repository_test PRIVATE
    GTest::gtest_main chatroom_mysql_persistence)
  chatroom_target_defaults(mysql_repository_test)
  add_test(NAME mysql_repository_test COMMAND mysql_repository_test)
  set_tests_properties(mysql_repository_test PROPERTIES LABELS integration DISABLED TRUE)
endif()
