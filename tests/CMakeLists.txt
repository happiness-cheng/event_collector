add_executable(test_validate
    test_validate.cpp
    ${CMAKE_SOURCE_DIR}/src/processor/validation.cpp
    ${CMAKE_SOURCE_DIR}/src/proto/event.pb.cc
)
target_include_directories(test_validate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_validate PRIVATE
    ${Protobuf_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_validate COMMAND test_validate)
