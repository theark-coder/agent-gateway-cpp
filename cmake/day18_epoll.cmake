# Day 18: Linux epoll LT multi-client echo experiment.
# Keep it separate from the Day 17 blocking echo and the future HTTP service.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(agent_tcp_epoll_server
        examples/tcp_epoll_server.cpp
    )
    target_link_libraries(agent_tcp_epoll_server PRIVATE agent_core)
    target_compile_options(agent_tcp_epoll_server PRIVATE -Wall -Wextra -Wpedantic)

    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(
            NAME tcp_epoll_day18
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/day18/test_epoll.py"
                "$<TARGET_FILE:agent_tcp_epoll_server>"
        )
        set_tests_properties(tcp_epoll_day18 PROPERTIES TIMEOUT 30)
    endif()
endif()
