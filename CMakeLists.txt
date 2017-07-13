cmake_minimum_required(VERSION 2.6)

project(erlent)

add_definitions(-D_POSIX_C_SOURCE=200809L)
add_definitions(-D_FILE_OFFSET_BITS=64)

set(CMAKE_CXX_FLAGS "-g -std=c++11 -pedantic -Wall -O")

include_directories("${PROJECT_SOURCE_DIR}/include")

add_library(erlent
  src/erlent/child.cc
  src/erlent/erlent.cc
  src/erlent/fuse.cc
  src/erlent/local.cc
  src/erlent/signalrelay.cc
)

add_executable(erlent-server src/server/main.cc)
target_link_libraries(erlent-server erlent pthread util)

add_executable(erlent-fuse src/fuse/main.cc)
target_link_libraries(erlent-fuse erlent fuse pthread util)

add_executable(uchroot src/uchroot/main.cc)
target_link_libraries(uchroot erlent fuse pthread util)

add_executable(binddev src/binddev/main.cc)

install(TARGETS erlent-server erlent-fuse uchroot binddev RUNTIME DESTINATION bin)
