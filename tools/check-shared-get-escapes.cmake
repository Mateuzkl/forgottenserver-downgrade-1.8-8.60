cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE source_files
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.h"
)

set(violations)
foreach(source_file IN LISTS source_files)
    file(STRINGS "${source_file}" source_lines)
    set(line_number 0)

    foreach(source_line IN LISTS source_lines)
        math(EXPR line_number "${line_number} + 1")
        set(scan_line "${source_line}")

        string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "" scan_line "${scan_line}")
        string(REGEX REPLACE "//.*$" "" scan_line "${scan_line}")

        if (scan_line MATCHES "^[ \t]*[*/]")
            continue()
        endif()

        if (scan_line MATCHES "\\[[^]]*\\.get[ \t]*\\([^)]*\\)[^]]*\\][ \t]*(\\(|\\{|mutable|->)")
            list(APPEND violations "${source_file}:${line_number}: shared_ptr::get() in lambda capture: ${source_line}")
        endif()

        if (scan_line MATCHES "(g_scheduler\\.addEvent|createSchedulerTask|createTimedTask|createTask|g_dispatcher\\.addTask|addGameTask|addGameTaskTimed)[^;]*\\.get[ \t]*\\(" OR
            scan_line MATCHES "\\.get[ \t]*\\([^)]*\\)[^;]*(g_scheduler\\.addEvent|createSchedulerTask|createTimedTask|createTask|g_dispatcher\\.addTask|addGameTask|addGameTaskTimed)")
            list(APPEND violations "${source_file}:${line_number}: shared_ptr::get() passed through async/task API: ${source_line}")
        endif()
    endforeach()
endforeach()

if (violations)
    list(JOIN violations "\n" violation_text)
    message(FATAL_ERROR "Unsafe shared_ptr::get() escapes found:\n${violation_text}")
endif()

message(STATUS "No shared_ptr::get() async/lambda escapes found")
