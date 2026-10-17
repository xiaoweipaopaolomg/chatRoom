cmake_minimum_required(VERSION 3.25)
project(chatroom VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_SERVER "Build the Linux epoll server" ON)
option(BUILD_QT_CLIENT "Build the Qt 6 desktop client" OFF)
option(BUILD_TESTS "Build automated tests" ON)
option(BUILD_LOAD_TEST "Build the epoll load client" OFF)

if(BUILD_SERVER AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "BUILD_SERVER requires Linux (epoll/eventfd)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)

if(BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src/common)
add_subdirectory(src/protocol)
add_subdirectory(src/domain)
add_subdirectory(src/persistence)
add_subdirectory(src/application)

if(BUILD_SERVER)
  add_subdirectory(src/server)
endif()

if(BUILD_QT_CLIENT)
  add_subdirectory(src/qt_client)
endif()

if(BUILD_LOAD_TEST)
  add_subdirectory(src/load_client)
endif()

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
