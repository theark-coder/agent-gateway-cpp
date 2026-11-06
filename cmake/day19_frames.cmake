# Day 19: 4-byte length-prefixed epoll LT echo, independent from Day18.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(agent_tcp_framed_server
        examples/tcp_framed_server.cpp
    )
    target_link_libraries(agent_tcp_framed_server PRIVATE agent_core)
    target_compile_options(agent_tcp_framed_server PRIVATE -Wall -Wextra -Wpedantic)

    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(case_name IN ITEMS fragmented coalesced oversized slow concurrent)
            add_test(
                NAME tcp_frames_day19_${case_name}
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/day19/test_framed.py"
                    "$<TARGET_FILE:agent_tcp_framed_server>"
                    "${case_name}"
            )
            set_tests_properties(tcp_frames_day19_${case_name} PROPERTIES TIMEOUT 35)
        endforeach()
    endif()
endif()
