file(GLOB benchs_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/bench_*.cpp)

foreach(bench_src ${benchs_SRC})
    get_filename_component(bench_name ${bench_src} NAME_WE)
    add_executable(${bench_name} ${bench_src})
    set(bench_library ${tfs_BENCH_LIBRARY})
    if (NOT bench_library)
        set(bench_library tfslib)
    endif()
    if (bench_name STREQUAL "bench_reactor_heap" AND TARGET tfs_testlib)
        set(bench_library tfs_testlib)
    endif()
    if (NOT ENABLE_BENCHMARK_LTO)
        set_target_properties(${bench_name} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION FALSE
            INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
    elseif (TFS_LTO_LINK_OPTIONS)
        target_link_options(${bench_name} PRIVATE ${TFS_LTO_LINK_OPTIONS})
    endif()
    target_link_libraries(${bench_name} PRIVATE ${bench_library} ZLIB::ZLIB OpenSSL::Crypto absl::flat_hash_map absl::flat_hash_set benchmark::benchmark)

    if (NOT bench_name STREQUAL "bench_reactor_heap")
        add_test(NAME ${bench_name} COMMAND ${bench_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
endforeach()
