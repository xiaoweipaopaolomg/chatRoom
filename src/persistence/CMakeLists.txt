add_library(chatroom_persistence in_memory_chat_repository.cpp)
target_include_directories(chatroom_persistence PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(chatroom_persistence PUBLIC chatroom_common)
chatroom_target_defaults(chatroom_persistence)

option(BUILD_MYSQL_ADAPTER "Build MySQL Connector/C++ persistence adapter" ON)
if(BUILD_MYSQL_ADAPTER)
  find_path(MYSQLCPPCONN_INCLUDE_DIR cppconn/driver.h)
  find_library(MYSQLCPPCONN_LIBRARY NAMES mysqlcppconn)
  find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient)
  if(MYSQLCPPCONN_INCLUDE_DIR AND MYSQLCPPCONN_LIBRARY)
    add_library(chatroom_mysql_persistence
      mysql_connection_pool.cpp
      mysql_chat_repository.cpp)
    target_include_directories(chatroom_mysql_persistence
      PUBLIC ${PROJECT_SOURCE_DIR}/src
      PRIVATE ${MYSQLCPPCONN_INCLUDE_DIR})
    target_link_libraries(chatroom_mysql_persistence
      PUBLIC chatroom_persistence
      PRIVATE ${MYSQLCPPCONN_LIBRARY})
    if(MYSQLCLIENT_LIBRARY)
      target_link_libraries(chatroom_mysql_persistence PRIVATE ${MYSQLCLIENT_LIBRARY})
    endif()
    target_compile_definitions(chatroom_mysql_persistence PUBLIC CHATROOM_HAS_MYSQL=1)
    chatroom_target_defaults(chatroom_mysql_persistence)
  else()
    message(STATUS "MySQL Connector/C++ not found; production adapter target disabled")
  endif()
endif()
