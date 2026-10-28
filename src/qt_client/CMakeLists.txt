find_package(Qt6 6.2 QUIET COMPONENTS Core Network Widgets Test)
if(NOT Qt6_FOUND)
  message(WARNING "Qt 6 was not found; chatroom_qt_client target disabled")
  return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
add_library(chatroom_qt_client_core
  chat_transport.cpp client_model.cpp main_window.cpp)
target_include_directories(chatroom_qt_client_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(chatroom_qt_client_core PUBLIC
  Qt6::Core Qt6::Network Qt6::Widgets)
chatroom_target_defaults(chatroom_qt_client_core)

add_executable(chatroom_qt_client WIN32 main.cpp resources.qrc)
target_link_libraries(chatroom_qt_client PRIVATE chatroom_qt_client_core)
chatroom_target_defaults(chatroom_qt_client)

if(BUILD_TESTS)
  foreach(test_name qt_transport_test client_model_test main_window_test)
    add_executable(${test_name} ${PROJECT_SOURCE_DIR}/tests/unit/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE chatroom_qt_client_core Qt6::Test)
    chatroom_target_defaults(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
endif()
