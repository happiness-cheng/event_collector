add_library(monitor STATIC
    monitor.cpp
)

target_include_directories(monitor PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(monitor PUBLIC
    Boost::system
)
