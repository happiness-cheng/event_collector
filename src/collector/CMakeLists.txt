add_library(collector STATIC
    collector.cpp
)

target_include_directories(collector PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(collector PUBLIC
    Boost::system
)
