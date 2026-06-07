set(benchs_SRC
    ${CMAKE_CURRENT_LIST_DIR}/bench_tools.cpp
    )

foreach(bench_src ${benchs_SRC})
    get_filename_component(bench_name ${bench_src} NAME_WE)
    add_executable(${bench_name} ${bench_src})
    target_link_libraries(${bench_name} PRIVATE tfslib ZLIB::ZLIB OpenSSL::Crypto absl::flat_hash_map benchmark::benchmark)

    add_test(NAME ${bench_name} COMMAND ${bench_name})
endforeach()
