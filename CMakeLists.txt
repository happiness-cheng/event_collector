cmake_minimum_required(VERSION 3.16)
project(event_collector)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

find_path(CLICKHOUSE_CPP_INCLUDE_DIR clickhouse/client.h
    PATHS /usr/local/include /usr/include
    REQUIRED
)
find_library(CLICKHOUSE_CPP_LIB
    NAMES clickhouse-cpp clickhouse-cpp-lib
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
)
find_library(LZ4_LIB
    NAMES lz4
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu
    REQUIRED
)
find_library(ZSTD_LIB
    NAMES zstdstatic zstd
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu
    REQUIRED
)
find_library(CITYHASH_LIB
    NAMES cityhash
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
)

find_path(REDISPLUSPLUS_INCLUDE_DIR sw/redis++/redis++.h
    PATHS /usr/local/include /usr/include
    REQUIRED
)
find_library(REDISPLUSPLUS_LIB
    NAMES redis++
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
)
find_library(HIREDIS_LIB
    NAMES hiredis
    PATHS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/event.proto)

add_subdirectory(src/collector)
add_subdirectory(src/processor)
add_subdirectory(src/monitor)

add_executable(server
    src/main.cpp
)

target_include_directories(server PRIVATE
    include
)

target_link_libraries(server PRIVATE
    collector
    processor
    monitor
    ${Protobuf_LIBRARIES}
    ${REDISPLUSPLUS_LIB}
    ${HIREDIS_LIB}
    ${CLICKHOUSE_CPP_LIB}
    ${LZ4_LIB}
    ${CITYHASH_LIB}
    ${ZSTD_LIB}
    Boost::system
    Boost::thread
    spdlog::spdlog
    Threads::Threads
)
