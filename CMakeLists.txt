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
    src/thread_pool.cpp
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
# Day 14: graceful shutdown demo
if(UNIX)
    add_executable(
        agent_shutdown_demo
        src/shutdown_demo.cpp
    )

    target_link_libraries(
        agent_shutdown_demo
        PRIVATE agent_core Threads::Threads
    )

    target_compile_options(
        agent_shutdown_demo
        PRIVATE -Wall -Wextra -Wpedantic
    )
endif()
target_link_libraries(
    agent_core
    PUBLIC Threads::Threads
)
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
    # Day 10: ThreadPool tests
    add_executable(
        agent_thread_pool_tests
        tests/test_thread_pool.cpp
    )

    target_link_libraries(
        agent_thread_pool_tests
        PRIVATE agent_core
    )

    if(MSVC)
        target_compile_options(
            agent_thread_pool_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_thread_pool_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME thread_pool
        COMMAND agent_thread_pool_tests
    )
    # Day 10: BoundedQueue tests
    add_executable(
        agent_bounded_queue_tests
        tests/test_bounded_queue.cpp
    )

    target_link_libraries(
        agent_bounded_queue_tests
        PRIVATE agent_core
    )

    if(MSVC)
        target_compile_options(
            agent_bounded_queue_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_bounded_queue_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME bounded_queue
        COMMAND agent_bounded_queue_tests
    )
    # Day 11: ThreadPool integration tests
    add_executable(
        agent_thread_pool_day11_tests
        tests/test_thread_pool_day11.cpp
    )

    target_link_libraries(
        agent_thread_pool_day11_tests
        PRIVATE agent_core Threads::Threads
    )

    if(MSVC)
        target_compile_options(
            agent_thread_pool_day11_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_thread_pool_day11_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME thread_pool_day11
        COMMAND agent_thread_pool_day11_tests
    )
    # Day 12: atomic cancellation and statistics
    add_executable(
        agent_thread_pool_day12_tests
        tests/test_thread_pool_day12.cpp
    )

    target_link_libraries(
        agent_thread_pool_day12_tests
        PRIVATE agent_core Threads::Threads
    )

    if(MSVC)
        target_compile_options(
            agent_thread_pool_day12_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_thread_pool_day12_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME thread_pool_day12
        COMMAND agent_thread_pool_day12_tests
    )
    # Day 14: SIGTERM graceful shutdown integration
    if(UNIX)
        add_test(
            NAME thread_pool_sigterm_shutdown
            COMMAND bash
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shutdown_integration.sh"
                "$<TARGET_FILE:agent_shutdown_demo>"
                "${CMAKE_CURRENT_BINARY_DIR}/day14_shutdown_test"
        )

        set_tests_properties(
            thread_pool_sigterm_shutdown
            PROPERTIES TIMEOUT 20
        )
    endif()
    # Day 15: task_id diagnostic logging
    if(UNIX)
        add_executable(
            agent_day15_task_logging
            practice/day15_task_logging.cpp
        )

        target_link_libraries(
            agent_day15_task_logging
            PRIVATE agent_core Threads::Threads
        )

        target_compile_options(
            agent_day15_task_logging
            PRIVATE -Wall -Wextra -Wpedantic
        )

        add_test(
            NAME task_id_logging
            COMMAND bash
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_day15_task_logging.sh"
                "$<TARGET_FILE:agent_day15_task_logging>"
        )

        set_tests_properties(
            task_id_logging
            PROPERTIES TIMEOUT 10
        )
    endif()
    # Day 16: integrated concurrency scenarios
    add_executable(
        agent_day16_integration_tests
        tests/test_day16_integration.cpp
    )

    target_link_libraries(
        agent_day16_integration_tests
        PRIVATE agent_core Threads::Threads
    )

    if(MSVC)
        target_compile_options(
            agent_day16_integration_tests PRIVATE /W4
        )
    else()
        target_compile_options(
            agent_day16_integration_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(
        NAME thread_pool_day16_integration
        COMMAND agent_day16_integration_tests
    )

    set_tests_properties(
        thread_pool_day16_integration
        PROPERTIES TIMEOUT 20
    )
    
    # Day 17: TCP Echo Server and Client
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(
            agent_tcp_echo_server
            examples/tcp_echo_server.cpp
        )

        add_executable(
            agent_tcp_echo_client
            examples/tcp_echo_client.cpp
        )

        target_link_libraries(
            agent_tcp_echo_server PRIVATE agent_core
        )

        target_link_libraries(
            agent_tcp_echo_client PRIVATE agent_core
        )

        target_compile_options(
            agent_tcp_echo_server
            PRIVATE -Wall -Wextra -Wpedantic
        )

        target_compile_options(
            agent_tcp_echo_client
            PRIVATE -Wall -Wextra -Wpedantic
        )

        if(BUILD_TESTING)
            foreach(scenario IN ITEMS normal empty large)
                add_test(
                    NAME tcp_echo_${scenario}
                    COMMAND bash
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/day17/test_echo.sh"
                        "$<TARGET_FILE:agent_tcp_echo_server>"
                        "$<TARGET_FILE:agent_tcp_echo_client>"
                        "${scenario}"
                )

                set_tests_properties(
                    tcp_echo_${scenario}
                    PROPERTIES TIMEOUT 15
                )
            endforeach()
        endif()
    endif()

endif()
