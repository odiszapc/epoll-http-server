cmake_minimum_required(VERSION 2.8)
project(epoll_http_server)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g")

set(SOURCE_FILES main.cpp http_parser.c http_request.cpp worker.cpp server.hpp common.hpp)
add_executable(final ${SOURCE_FILES})
target_link_libraries(final pthread)