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
endforeach()
