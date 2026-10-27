file(GLOB tests_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

foreach(test_src ${tests_SRC})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE
        ${tfs_TEST_LIBRARY}
        ZLIB::ZLIB
        OpenSSL::Crypto
        absl::flat_hash_map
    )
    if (NOT ENABLE_TEST_LTO)
        set_target_properties(${test_name} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION FALSE
            INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "TFS_TEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    )
endforeach()

find_program(TFS_LUA_TEST_EXECUTABLE NAMES lua5.5 lua luajit)
if (TFS_LUA_TEST_EXECUTABLE)
    add_test(NAME test_step_tile_callbacks
        COMMAND ${TFS_LUA_TEST_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/test_step_tile_callbacks.lua
            ${PROJECT_SOURCE_DIR}/data/events/scripts/player.lua)
endif()
