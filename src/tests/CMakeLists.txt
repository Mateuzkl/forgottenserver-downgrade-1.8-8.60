file(GLOB tests_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

foreach(test_src ${tests_SRC})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE
        tfslib
        ZLIB::ZLIB
        OpenSSL::Crypto
        absl::flat_hash_map
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
