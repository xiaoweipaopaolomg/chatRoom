option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
endif()

add_library(chatroom_warnings INTERFACE)
target_compile_features(chatroom_warnings INTERFACE cxx_std_17)
target_compile_options(chatroom_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_library(chatroom_sanitizers INTERFACE)
if(ENABLE_ASAN)
  target_compile_options(chatroom_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(chatroom_sanitizers INTERFACE -fsanitize=address)
endif()
if(ENABLE_UBSAN)
  target_compile_options(chatroom_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(chatroom_sanitizers INTERFACE -fsanitize=undefined)
endif()
if(ENABLE_TSAN)
  target_compile_options(chatroom_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(chatroom_sanitizers INTERFACE -fsanitize=thread)
endif()

function(chatroom_target_defaults target)
  target_link_libraries(${target} PRIVATE chatroom_warnings chatroom_sanitizers)
endfunction()
