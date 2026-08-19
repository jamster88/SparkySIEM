cmake_minimum_required(VERSION 3.16)
project(SparkySIEM CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "SparkySIEM watches files with inotify, which is Linux-only, so it cannot be "
        "built on ${CMAKE_SYSTEM_NAME}. Build and run the tests in a container instead:\n"
        "    ./docker_stuff/run_tests.sh")
endif()

add_compile_options(-Wall -Wextra)

# librdkafka: prefer pkg-config, fall back to plain link flags.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(RDKAFKACPP IMPORTED_TARGET rdkafka++)
endif()

# The application logic lives in a library so that both the executable and the
# test binary link against exactly the same objects.
add_library(sparky_core STATIC
    FileMonitor.cpp
    KafkaSink.cpp
    Message.cpp)
target_include_directories(sparky_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RDKAFKACPP_FOUND)
    target_link_libraries(sparky_core PUBLIC PkgConfig::RDKAFKACPP)
else()
    target_link_libraries(sparky_core PUBLIC rdkafka++ rdkafka)
endif()

add_executable(sparky_siem main.cpp)
target_link_libraries(sparky_siem PRIVATE sparky_core)

option(SPARKY_BUILD_TESTS "Build the unit tests" ON)
if(SPARKY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
