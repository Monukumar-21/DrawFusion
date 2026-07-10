cmake_minimum_required(VERSION 3.10)
project(HelloWorldProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 1. Tell CMake to go find the spdlog library
find_package(spdlog CONFIG REQUIRED)

add_executable(hello_world main.cpp)

# 2. Tell CMake to link the spdlog binary into our program
target_link_libraries(hello_world PRIVATE spdlog::spdlog)
