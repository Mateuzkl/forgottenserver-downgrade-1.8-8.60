# Isolate CPU-specific kernels from Unity, PCH and MSVC cross-module inlining.
function(tfs_configure_xtea target)
    set(xtea_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    set_source_files_properties(
        "${xtea_root}/xtea.cpp" "${xtea_root}/xtea_sse2.cpp" "${xtea_root}/xtea_avx2.cpp"
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON)
    if(TFS_XTEA_FORCE_SCALAR)
        set_property(SOURCE "${xtea_root}/xtea.cpp" APPEND PROPERTY COMPILE_DEFINITIONS TFS_XTEA_FORCE_SCALAR=1)
    endif()
    if(MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|i[3-6]86|x86)$")
        set_source_files_properties("${xtea_root}/xtea_avx2.cpp" PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/GL-")
        set_source_files_properties("${xtea_root}/xtea_sse2.cpp" PROPERTIES COMPILE_OPTIONS "/GL-")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set_property(SOURCE "${xtea_root}/xtea_sse2.cpp" APPEND PROPERTY COMPILE_OPTIONS /arch:SSE2)
        endif()
    endif()
endfunction()
