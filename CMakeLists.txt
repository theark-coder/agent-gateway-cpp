cmake_minimum_required(VERSION 3.16)
project(agent_gateway_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(agent_core STATIC
    src/health.cpp
    src/task.cpp
    src/tool.cpp
    src/model_client.cpp
)
target_include_directories(agent_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(agent_core PRIVATE /W4)
else()
    target_compile_options(agent_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(agent_gateway src/main.cpp)
target_link_libraries(agent_gateway PRIVATE agent_core)

include(CTest)
find_package(Threads REQUIRED)

if(BUILD_TESTING)
    # Day 1: health smoke test
    add_executable(agent_tests tests/test_health.cpp)
    target_link_libraries(agent_tests PRIVATE agent_core)
    add_test(NAME health_smoke COMMAND agent_tests)

    # Day 3: UniqueFd RAII tests (POSIX)
    if(UNIX)
        add_executable(
            agent_unique_fd_tests
            tests/test_unique_fd.cpp
        )

        target_link_libraries(
            agent_unique_fd_tests
            PRIVATE agent_core
        )

        target_compile_options(
            agent_unique_fd_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )

        add_test(
            NAME unique_fd_raii
            COMMAND agent_unique_fd_tests
        )
    endif()

    # Day 5: smart pointer lifetime tests
    add_executable(
        agent_smart_ptr_tests
        tests/test_smart_ptr.cpp
    )

    target_link_libraries(
        agent_smart_ptr_tests
        PRIVATE agent_core
    )

    if(MSVC)
        target_compile_options(
            agent_smart_ptr_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_smart_ptr_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME smart_ptr_lifetime
        COMMAND agent_smart_ptr_tests
    )

    # Day 2-9: TaskService tests
    add_executable(
        agent_task_tests
        tests/test_task.cpp
    )

    target_link_libraries(
        agent_task_tests
        PRIVATE agent_core Threads::Threads
    )

    add_test(
        NAME task_service
        COMMAND agent_task_tests
    )

    # Day 6: polymorphic tool and model client tests
    add_executable(
        agent_tool_tests
        tests/test_tool.cpp
    )

    target_link_libraries(
        agent_tool_tests
        PRIVATE agent_core
    )

    if(MSVC)
        target_compile_options(
            agent_tool_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_tool_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME tool_polymorphism
        COMMAND agent_tool_tests
    )
endif()
