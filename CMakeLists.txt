cmake_minimum_required(VERSION 3.16)
project(agent_gateway_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(agent_core STATIC src/health.cpp)
target_include_directories(agent_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(agent_core PRIVATE /W4)
else()
    target_compile_options(agent_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(agent_gateway src/main.cpp)
target_link_libraries(agent_gateway PRIVATE agent_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(agent_tests tests/test_health.cpp)
    target_link_libraries(agent_tests PRIVATE agent_core)
    add_test(NAME health_smoke COMMAND agent_tests)
endif()
