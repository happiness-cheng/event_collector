add_library(storage STATIC
    storage.cpp
)

target_include_directories(storage PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CLICKHOUSE_CPP_INCLUDE_DIR}
    ${REDISPLUSPLUS_INCLUDE_DIR}
)

target_link_libraries(storage PUBLIC
    ${CLICKHOUSE_CPP_LIB}
    ${LZ4_LIB}
    ${CITYHASH_LIB}
    ${ZSTD_LIB}
    spdlog::spdlog
)

add_library(rate_limiter STATIC
    rate_limiter.cpp
)

target_include_directories(rate_limiter PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${REDISPLUSPLUS_INCLUDE_DIR}
)

target_link_libraries(rate_limiter PUBLIC
    ${REDISPLUSPLUS_LIB}
    ${HIREDIS_LIB}
    spdlog::spdlog
)

add_library(processor STATIC
    processor.cpp
    ${CMAKE_SOURCE_DIR}/src/proto/event.pb.cc
)

target_include_directories(processor PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/proto
    ${Protobuf_INCLUDE_DIRS}
    ${REDISPLUSPLUS_INCLUDE_DIR}
)

target_link_libraries(processor PUBLIC
    ${Protobuf_LIBRARIES}
    ${RDKAFKA_LIB}
    spdlog::spdlog
    storage
    rate_limiter
)
