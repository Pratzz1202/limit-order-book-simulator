cmake_minimum_required(VERSION 3.16)
project(orderbook_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB SRC_FILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(orderbook_simulator ${SRC_FILES})

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(orderbook_simulator PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(orderbook_simulator PRIVATE -fsanitize=address,undefined)
endif()

target_include_directories(orderbook_simulator PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(orderbook_simulator PRIVATE -O3 -march=native -DNDEBUG)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(orderbook_simulator stdc++fs)
  endif()
endif()
