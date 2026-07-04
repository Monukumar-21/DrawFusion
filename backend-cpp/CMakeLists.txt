cmake_minimum_required(VERSION 3.20)
project(DrawFusion-CppServer
    VERSION 1.0.0
    LANGUAGES CXX
)

# ── C++ Standard ─────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find Packages (vcpkg provides these) ─────────────────────────

# gRPC + Protobuf
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# uWebSockets (via unofficial port in vcpkg)
find_package(unofficial-uwebsockets CONFIG REQUIRED)

# nlohmann-json (header-only)
find_package(nlohmann_json CONFIG REQUIRED)

# libpqxx (PostgreSQL C++ client)
find_package(libpqxx CONFIG REQUIRED)

# jwt-cpp (header-only JWT library)
find_package(jwt-cpp CONFIG REQUIRED)

# spdlog + fmt (logging)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# ── Proto Compilation ────────────────────────────────────────────
# Compile game-ai.proto into C++ sources at build time

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/../proto")
set(PROTO_FILE "${PROTO_DIR}/game-ai.proto")
set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/generated")

file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Get the paths to protoc and grpc_cpp_plugin
find_program(_PROTOC protoc)
find_program(_GRPC_CPP_PLUGIN grpc_cpp_plugin)

# If grpc_cpp_plugin not found as standalone, look in gRPC's tools
if(NOT _GRPC_CPP_PLUGIN)
    get_target_property(_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
endif()

# Generate protobuf + gRPC stubs
set(PROTO_SRCS "${PROTO_GEN_DIR}/game-ai.pb.cc")
set(PROTO_HDRS "${PROTO_GEN_DIR}/game-ai.pb.h")
set(GRPC_SRCS "${PROTO_GEN_DIR}/game-ai.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_GEN_DIR}/game-ai.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${_PROTOC}
        --proto_path=${PROTO_DIR}
        --cpp_out=${PROTO_GEN_DIR}
        --grpc_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++ gRPC stubs from game-ai.proto"
)

# Proto library (used by other targets)
add_library(proto_lib STATIC
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_include_directories(proto_lib PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(proto_lib PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

# ── AI Client Library ────────────────────────────────────────────
# Wraps gRPC calls to the Python AI service

add_library(ai_client STATIC
    src/ai_client.cpp
)
target_include_directories(ai_client PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${PROTO_GEN_DIR}
)
target_link_libraries(ai_client PUBLIC
    proto_lib
    spdlog::spdlog
)

# ── Main Server Binary ──────────────────────────────────────────

add_executable(drawfusion_server
    src/main.cpp
)
target_link_libraries(drawfusion_server PRIVATE
    ai_client
    proto_lib
    unofficial::uwebsockets::uwebsockets
    nlohmann_json::nlohmann_json
    libpqxx::pqxx
    jwt-cpp::jwt-cpp
    spdlog::spdlog
    fmt::fmt
)

# ── Library Smoke Test Binary ────────────────────────────────────
# Verifies all libraries compile and link correctly

add_executable(test_libs
    src/test_libs.cpp
)
target_link_libraries(test_libs PRIVATE
    ai_client
    proto_lib
    unofficial::uwebsockets::uwebsockets
    nlohmann_json::nlohmann_json
    libpqxx::pqxx
    jwt-cpp::jwt-cpp
    spdlog::spdlog
    fmt::fmt
)

# ── Status Messages ──────────────────────────────────────────────
message(STATUS "╔══════════════════════════════════════════════════╗")
message(STATUS "║  DrawFusion C++ Server — Build Configuration    ║")
message(STATUS "╠══════════════════════════════════════════════════╣")
message(STATUS "║  C++ Standard  : C++${CMAKE_CXX_STANDARD}")
message(STATUS "║  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Build Type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "║  Proto File    : ${PROTO_FILE}")
message(STATUS "║  Proto Gen Dir : ${PROTO_GEN_DIR}")
message(STATUS "║  Protoc        : ${_PROTOC}")
message(STATUS "║  gRPC Plugin   : ${_GRPC_CPP_PLUGIN}")
message(STATUS "╚══════════════════════════════════════════════════╝")
