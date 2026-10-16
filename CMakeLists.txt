cmake_minimum_required(VERSION 3.16)
project(sirenopt_sensor_pipeline CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

add_executable(pipeline app/main.cpp)
add_executable(unit_tests tests/test_main.cpp)

enable_testing()
add_test(NAME unit COMMAND unit_tests)
