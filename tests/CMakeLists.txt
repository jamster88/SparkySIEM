find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_executable(sparky_tests
    TestSupport.cpp
    test_message.cpp
    test_file_monitor.cpp
    test_kafka_sink.cpp)

target_link_libraries(sparky_tests PRIVATE
    sparky_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads)

include(GoogleTest)
gtest_discover_tests(sparky_tests
    PROPERTIES TIMEOUT 60)
