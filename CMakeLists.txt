cmake_minimum_required(VERSION 3.20)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

# Ensure to pick up the default triplet from the environment if any. This helps
# driving the vcpkg triplet in the same way either when starting vcpkg directly,
# or when letting CMake start vcpkg at configure/generate time.
# Note: this logic must happen before PROJECT command.
if (DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "The vcpkg triplet")
endif()

option(HTTP "Enable HTTP support" ON)
if (HTTP)
    list(APPEND VCPKG_MANIFEST_FEATURES "http")
endif()

option(BUILD_TESTING "Build unit tests" OFF)
option(ENABLE_TEST_LTO "Use production LTO library for unit tests" OFF)
if (BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "unit-tests")
endif()

option(BUILD_BENCHMARKING "Build benchmarks" OFF)
option(ENABLE_BENCHMARK_LTO "Use production LTO library for benchmarks" OFF)
if (BUILD_BENCHMARKING)
    list(APPEND VCPKG_MANIFEST_FEATURES "benchmarks")
endif()

option(ENABLE_ASAN "Enable AddressSanitizer for memory diagnostics" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for undefined behavior diagnostics" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer for data race diagnostics" OFF)
option(ENABLE_NATIVE_OPTIMIZATIONS "Enable -march=native/-mtune=native for local Release builds" ON)
option(ENABLE_PACKET_BACKLOG_DIAGNOSTICS "Enable ProtocolGame packet backlog diagnostics" OFF)
option(ENABLE_CCACHE "Enable ccache compiler launcher automatically when available" ON)
option(ENABLE_LTO "Enable Link-Time Optimization (IPO/LTO) for Release/RelWithDebInfo builds" ON)
option(ENABLE_PCH "Enable precompiled headers (otpch.h) for non-unity builds" ON)
option(ENABLE_LOW_MEMORY_LINKER "Reduce linker RAM peak on GNU ld (--no-keep-memory --reduce-memory-overheads)" OFF)

set(UNITY_BUILD_BATCH_SIZE "8" CACHE STRING "Number of source files per Unity Build batch")
set(TFS_COMPILE_JOBS "0" CACHE STRING "Limit concurrent compile jobs via Ninja job pool (0 = unlimited)")
set(TFS_LINK_JOBS "0" CACHE STRING "Limit concurrent link jobs via Ninja job pool (0 = unlimited)")
set(TFS_LTO_JOBS "0" CACHE STRING "Limit LTO parallel worker threads (0 = compiler default)")
set(TFS_LINKER "auto" CACHE STRING "Linker preference on Unix: auto, default, mold, lld, gold")

option(USE_LUAJIT "Use LuaJIT" OFF)
if (USE_LUAJIT)
    list(APPEND VCPKG_MANIFEST_FEATURES "luajit")
else()
    list(APPEND VCPKG_MANIFEST_FEATURES "lua")
endif()

project(tfs CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Automatic ccache integration (honors explicit CMAKE_CXX_COMPILER_LAUNCHER overrides)
set(TFS_CCACHE_ACTIVE OFF)
if (ENABLE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND NOT DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    find_program(TFS_CCACHE_PROGRAM ccache)
    if (TFS_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${TFS_CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${TFS_CCACHE_PROGRAM}")
        set(TFS_CCACHE_ACTIVE ON)
        message(STATUS "ccache enabled: ${TFS_CCACHE_PROGRAM}")
    endif()
elseif (CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
    set(TFS_CCACHE_ACTIVE ON)
endif()

# Ninja job pools to bound parallel compile/link CPU and RAM consumption
if (CMAKE_GENERATOR MATCHES "Ninja")
    set(_tfs_job_pools)
    if (TFS_COMPILE_JOBS MATCHES "^[1-9][0-9]*$")
        list(APPEND _tfs_job_pools "compile_pool=${TFS_COMPILE_JOBS}")
        set(CMAKE_JOB_POOL_COMPILE compile_pool)
    endif()
    if (TFS_LINK_JOBS MATCHES "^[1-9][0-9]*$")
        list(APPEND _tfs_job_pools "link_pool=${TFS_LINK_JOBS}")
        set(CMAKE_JOB_POOL_LINK link_pool)
    endif()
    if (_tfs_job_pools)
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS ${_tfs_job_pools})
        message(STATUS "Ninja job pools configured: ${_tfs_job_pools}")
    endif()
endif()

# Strict warnings only for our sources, not for dependencies
if (NOT WIN32)
    add_compile_options(-Wall -Wextra -Wnon-virtual-dtor -pedantic -Werror -pipe -fvisibility=hidden)
    add_compile_options(-Wno-error=old-style-cast)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # GCC 12/13 -O3 emits false-positive free-nonheap-object inside libstdc++ std::variant/std::string
    # in non-unity translation units (GCC PR 104336 / 105562).
    add_compile_options(-fno-strict-aliasing -Wno-error=free-nonheap-object)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-Wimplicit-fallthrough -Wmove -Wno-date-time)
endif ()

if (ENABLE_ASAN)
    if (MSVC)
        message(WARNING "AddressSanitizer is experimental on Windows/MSVC")
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else ()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif ()
endif ()

if (ENABLE_UBSAN)
    if (MSVC)
        message(WARNING "UndefinedBehaviorSanitizer is not available on MSVC; ignoring ENABLE_UBSAN")
    else ()
        add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=undefined)
    endif ()
endif ()

if (ENABLE_TSAN)
    if (ENABLE_ASAN)
        message(FATAL_ERROR "ENABLE_TSAN and ENABLE_ASAN cannot be combined; the runtimes conflict")
    endif ()
    if (MSVC)
        message(FATAL_ERROR "ThreadSanitizer is not available on MSVC")
    else ()
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=thread)
    endif ()
endif ()

# Find packages.
find_package(OpenSSL 3.0.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(absl CONFIG REQUIRED)
# Find asio (header-only, can be in system path or vcpkg)
find_package(asio CONFIG QUIET)
if (NOT asio_FOUND)
    find_path(ASIO_INCLUDE_DIR NAMES asio.hpp)
    if (ASIO_INCLUDE_DIR)
        message(STATUS "Found asio headers at ${ASIO_INCLUDE_DIR}")
        add_library(asio INTERFACE)
        target_include_directories(asio INTERFACE ${ASIO_INCLUDE_DIR})
        add_library(asio::asio ALIAS asio)
        set(asio_FOUND TRUE)
    else()
        message(FATAL_ERROR "asio was not found. Install libasio-dev via apt or use vcpkg.")
    endif()
endif()

# Find mio (header-only, can be in system path or manually installed or vcpkg)
find_package(mio CONFIG QUIET)
if (NOT mio_FOUND)
    find_path(MIO_INCLUDE_DIR NAMES mio/mmap.hpp)
    if (MIO_INCLUDE_DIR)
        message(STATUS "Found mio headers at ${MIO_INCLUDE_DIR}")
        add_library(mio INTERFACE)
        target_include_directories(mio INTERFACE ${MIO_INCLUDE_DIR})
        add_library(mio::mio ALIAS mio)
        set(mio_FOUND TRUE)
    else()
        if (WIN32)
            message(FATAL_ERROR "mio was not found. Install it via vcpkg.")
        else()
            message(FATAL_ERROR "mio was not found. Install it manually using build.sh or install it in $HOME/.local")
        endif()
    endif()
endif()

set(SIMDUTF_HINTS)
if (UNIX AND DEFINED ENV{HOME})
    list(APPEND SIMDUTF_HINTS "$ENV{HOME}/.local")
endif()

find_package(simdutf CONFIG QUIET HINTS ${SIMDUTF_HINTS})
if (NOT simdutf_FOUND)
    if (WIN32)
        message(FATAL_ERROR "simdutf was not found. It should be automatically installed via vcpkg (see vcpkg.json). Make sure vcpkg is properly configured.")
    else()
        message(FATAL_ERROR "simdutf was not found. Install it manually:\n"
                "  cd ~\n"
                "  git clone https://github.com/simdutf/simdutf.git\n"
                "  cd simdutf\n"
                "  cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local\n"
                "  cmake --build build -- -j$(nproc)\n"
                "  cmake --install build\n"
                "CMake will automatically detect it in $HOME/.local\n"
                "If installed elsewhere, use: -Dsimdutf_DIR=<prefix>/lib/cmake/simdutf")
    endif()
endif()

find_package(fmt 8.1.1 CONFIG)
find_package(spdlog CONFIG REQUIRED)
if (NOT fmt_FOUND)
    find_package(fmt 8.1.1 REQUIRED)
endif()

# Look for vcpkg-provided libmariadb first
# If we link to the file directly, we might miss its dependencies from vcpkg
find_package(unofficial-libmariadb CONFIG)
if (unofficial-libmariadb_FOUND)
    set(MYSQL_CLIENT_LIBS "unofficial::libmariadb")
else ()
    find_package(MySQL REQUIRED)
endif ()

find_package(Threads REQUIRED)
find_package(PugiXML CONFIG REQUIRED)

if (BUILD_BENCHMARKING)
    find_package(benchmark REQUIRED)
endif()

# Selects LuaJIT if user defines or auto-detected
if (USE_LUAJIT)
    find_package(LuaJIT REQUIRED)
else ()
    set(TFS_LUA_REQUIRED_VERSION "5.5")
    message(STATUS "LuaJIT disabled; requiring Lua ${TFS_LUA_REQUIRED_VERSION}. See README.md for the manual Lua 5.5 setup.")

    # Lua 5.5 changed its version defines to numeric *_N macros. CMake's
    # FindLua module does not recognize those macros yet, so honor and verify
    # the explicit paths supplied by build.sh before falling back to FindLua.
    if (LUA_INCLUDE_DIR AND LUA_LIBRARY AND EXISTS "${LUA_INCLUDE_DIR}/lua.h" AND EXISTS "${LUA_LIBRARY}")
        file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" TFS_LUA_VERSION_DEFINES
            REGEX "^#define[ \t]+LUA_VERSION_(MAJOR|MINOR)(_N)?[ \t]+")

        string(REGEX MATCH "LUA_VERSION_MAJOR(_N)?[ \t]+\"?([0-9]+)" TFS_LUA_MAJOR_MATCH "${TFS_LUA_VERSION_DEFINES}")
        set(TFS_LUA_VERSION_MAJOR "${CMAKE_MATCH_2}")
        string(REGEX MATCH "LUA_VERSION_MINOR(_N)?[ \t]+\"?([0-9]+)" TFS_LUA_MINOR_MATCH "${TFS_LUA_VERSION_DEFINES}")
        set(TFS_LUA_VERSION_MINOR "${CMAKE_MATCH_2}")

        if (NOT TFS_LUA_VERSION_MAJOR STREQUAL "5" OR NOT TFS_LUA_VERSION_MINOR STREQUAL "5")
            message(FATAL_ERROR "The explicit Lua headers at ${LUA_INCLUDE_DIR} are not Lua ${TFS_LUA_REQUIRED_VERSION}.")
        endif()

        if (NOT LUA_LIBRARIES)
            set(LUA_LIBRARIES "${LUA_LIBRARY};m;${CMAKE_DL_LIBS}")
        endif()
        set(Lua_FOUND TRUE)
        set(Lua_VERSION "${TFS_LUA_VERSION_MAJOR}.${TFS_LUA_VERSION_MINOR}")
        message(STATUS "Found explicit Lua ${Lua_VERSION}: ${LUA_LIBRARY}")
    else()
        find_package(Lua "${TFS_LUA_REQUIRED_VERSION}" REQUIRED)
    endif()
endif ()

if (APPLE)
    find_package(Iconv REQUIRED)
endif()

# Force UTF-8 support in MSVC (Visual Studio/VSCode on Windows)
if(MSVC)
  add_compile_options(/utf-8)
endif()

if (NOT DEFINED DISABLE_STATS OR NOT DISABLE_STATS)
    message(STATUS "OTS stats enabled. Run 'cmake -DDISABLE_STATS=1 ..' to disable")
    ADD_DEFINITIONS(-DSTATS_ENABLED)
else ()
    message(STATUS "OTS stats disabled. Run 'cmake -DDISABLE_STATS=0 ..' to enable")
    ADD_DEFINITIONS(-USTATS_ENABLED)
endif ()

include_directories(${OPENSSL_INCLUDE_DIR} ${LUA_INCLUDE_DIR} ${MYSQL_INCLUDE_DIR} ${PUGIXML_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

### LINUX RELEASE OPTIMIZATIONS ###
if (NOT WIN32)
    set(TFS_RELEASE_FLAGS "-O3 -fomit-frame-pointer -DNDEBUG")
    if (ENABLE_NATIVE_OPTIMIZATIONS)
        string(APPEND TFS_RELEASE_FLAGS " -march=native -mtune=native")
    endif()

    set(CMAKE_CXX_FLAGS_RELEASE "${TFS_RELEASE_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "${TFS_RELEASE_FLAGS}")

    # Profile-Guided Optimization (2 steps)
    # Step 1: cmake -DCMAKE_BUILD_TYPE=Release -DPGO_GENERATE=ON ..
    # Step 2: run server under load, then: cmake -DCMAKE_BUILD_TYPE=Release -DPGO_USE=ON ..
    option(PGO_GENERATE "Generate PGO profile data" OFF)
    option(PGO_USE "Use PGO profile data" OFF)
    if (PGO_GENERATE)
        add_compile_options(-fprofile-generate)
        add_link_options(-fprofile-generate)
    elseif (PGO_USE)
        add_compile_options(-fprofile-use -fprofile-correction)
        add_link_options(-fprofile-use)
    endif()
endif()
### END LINUX RELEASE OPTIMIZATIONS ###

### MIMALLOC ALLOCATOR ###
option(USE_MIMALLOC "Use mimalloc allocator for better performance on Linux" OFF)
if (USE_MIMALLOC AND (ENABLE_ASAN OR ENABLE_TSAN))
    message(STATUS "mimalloc disabled automatically because a memory/thread sanitizer is enabled")
    set(USE_MIMALLOC OFF)
endif()
if (USE_MIMALLOC AND NOT WIN32)
    find_package(mimalloc CONFIG)
    if (mimalloc_FOUND)
        message(STATUS "mimalloc found - using as memory allocator")
    else()
        find_library(MIMALLOC_LIB mimalloc)
        if (MIMALLOC_LIB)
            message(STATUS "mimalloc found at ${MIMALLOC_LIB}")
        else()
            message(WARNING "mimalloc not found. Install with: sudo apt install libmimalloc-dev")
            set(USE_MIMALLOC OFF)
        endif()
    endif()
endif()
### END MIMALLOC ###

### SLOW TASK DETECTION ###
option(ENABLE_SLOW_TASK_DETECTION "Enable slow task detection logging (adds overhead)" OFF)
if (ENABLE_SLOW_TASK_DETECTION)
    ADD_DEFINITIONS(-DSLOW_TASK_DETECTION)
    message(STATUS "Slow task detection enabled. Disable for production: -DENABLE_SLOW_TASK_DETECTION=OFF")
endif()
### END SLOW TASK DETECTION ###

if (ENABLE_PACKET_BACKLOG_DIAGNOSTICS)
    ADD_DEFINITIONS(-DPROTOCOLGAME_BACKLOG_DIAGNOSTICS)
    message(STATUS "ProtocolGame packet backlog diagnostics enabled")
endif()

if (BUILD_TESTING OR BUILD_BENCHMARKING)
    enable_testing()
endif()

if (BUILD_TESTING)
    message(STATUS "Building unit tests")
    add_test(
        NAME check_source_list_parity
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tools/check-source-list-parity.cmake
    )
    add_test(
        NAME check_manual_delete
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tools/check-manual-delete.cmake
    )
    add_test(
        NAME check_shared_get_escapes
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tools/check-shared-get-escapes.cmake
    )
    add_test(
        NAME check_protocolgame_handshake
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tools/check-protocolgame-handshake.cmake
    )
    add_test(
        NAME check_shop_podium_protocol
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tools/check-shop-podium-protocol.cmake
    )
endif()

# Option to disable unity builds
option(ENABLE_UNITY_BUILD "Enable unity build" ON)
if (ENABLE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    if (UNITY_BUILD_BATCH_SIZE MATCHES "^[1-9][0-9]*$")
        set(CMAKE_UNITY_BUILD_BATCH_SIZE "${UNITY_BUILD_BATCH_SIZE}")
    endif()
endif()

if (ENABLE_PCH AND NOT ENABLE_UNITY_BUILD AND TFS_CCACHE_ACTIVE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Allow ccache to cache GCC translation units that consume precompiled headers.
    add_compile_options(-fpch-preprocess)
endif()

### INTERPROCEDURAL OPTIMIZATION ###
set(TFS_IPO_ACTIVE OFF)
if (ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    message(STATUS "IPO/LTO disabled because a sanitizer is enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
elseif (NOT ENABLE_LTO)
    message(STATUS "IPO/LTO disabled via ENABLE_LTO=OFF")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
else ()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT error)
    if (result)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        set(TFS_IPO_ACTIVE ON)
    else ()
        message(STATUS "IPO is not supported: ${error}")
    endif ()
endif ()
### END INTERPROCEDURAL OPTIMIZATION ###

### LINKER SELECTION & MEMORY TUNING ###
set(TFS_SELECTED_LINKER "default")
if (UNIX AND NOT APPLE)
    include(CheckLinkerFlag)
    set(_tfs_linker_candidates)
    if (TFS_LINKER STREQUAL "auto")
        if (NOT TFS_IPO_ACTIVE)
            list(APPEND _tfs_linker_candidates mold)
        endif()
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            list(APPEND _tfs_linker_candidates lld)
        endif()
    elseif (NOT TFS_LINKER STREQUAL "default")
        list(APPEND _tfs_linker_candidates "${TFS_LINKER}")
    endif()

    foreach(_linker IN LISTS _tfs_linker_candidates)
        set(_flag "-fuse-ld=${_linker}")
        string(TOUPPER "${_linker}" _linker_upper)
        check_linker_flag(CXX "${_flag}" "TFS_LINKER_SUPPORTS_${_linker_upper}")
        if (TFS_LINKER_SUPPORTS_${_linker_upper})
            add_link_options("${_flag}")
            set(TFS_SELECTED_LINKER "${_linker}")
            message(STATUS "Using linker: ${_linker}")
            break()
        endif()
    endforeach()

    set(TFS_LTO_LINK_OPTIONS)
    if (TFS_IPO_ACTIVE AND TFS_LTO_JOBS MATCHES "^[1-9][0-9]*$")
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND TFS_LTO_LINK_OPTIONS $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto=${TFS_LTO_JOBS}>)
            message(STATUS "GCC LTO worker threads capped at ${TFS_LTO_JOBS}")
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND TFS_SELECTED_LINKER STREQUAL "lld")
            list(APPEND TFS_LTO_LINK_OPTIONS $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--thinlto-jobs=${TFS_LTO_JOBS}>)
            message(STATUS "Clang ThinLTO worker threads capped at ${TFS_LTO_JOBS}")
        endif()
    endif()

    if (ENABLE_LOW_MEMORY_LINKER AND (TFS_SELECTED_LINKER STREQUAL "default" OR TFS_SELECTED_LINKER STREQUAL "bfd"))
        check_linker_flag(CXX "-Wl,--no-keep-memory" TFS_LD_SUPPORTS_NO_KEEP_MEMORY)
        if (TFS_LD_SUPPORTS_NO_KEEP_MEMORY)
            add_link_options(-Wl,--no-keep-memory)
        endif()
        check_linker_flag(CXX "-Wl,--reduce-memory-overheads" TFS_LD_SUPPORTS_REDUCE_MEM_OVERHEADS)
        if (TFS_LD_SUPPORTS_REDUCE_MEM_OVERHEADS)
            add_link_options(-Wl,--reduce-memory-overheads)
        endif()
    endif()
endif()
### END LINKER SELECTION & MEMORY TUNING ###

message(STATUS "Build summary: type=${CMAKE_BUILD_TYPE} generator=${CMAKE_GENERATOR} compiler=${CMAKE_CXX_COMPILER_ID} "
               "ccache=${TFS_CCACHE_ACTIVE} unity=${ENABLE_UNITY_BUILD}(batch=${UNITY_BUILD_BATCH_SIZE}) "
               "pch=${ENABLE_PCH} lto=${TFS_IPO_ACTIVE}(jobs=${TFS_LTO_JOBS}) linker=${TFS_SELECTED_LINKER} "
               "mimalloc=${USE_MIMALLOC} tests=${BUILD_TESTING} bench=${BUILD_BENCHMARKING}")

add_subdirectory(src)

add_executable(tfs ${tfs_MAIN})
target_link_libraries(tfs tfslib ZLIB::ZLIB OpenSSL::Crypto absl::flat_hash_map)
if (TFS_LTO_LINK_OPTIONS)
    target_link_options(tfs PRIVATE ${TFS_LTO_LINK_OPTIONS})
endif()

# Embed the application icon on Windows (rc.exe compiles the .rc -> MAINICON)
if (WIN32)
    target_sources(tfs PRIVATE ${CMAKE_SOURCE_DIR}/vc18/theforgottenserver.rc)
endif()

# Link mimalloc if enabled
if (USE_MIMALLOC AND NOT WIN32)
    if (mimalloc_FOUND)
        target_link_libraries(tfs mimalloc)
    elseif (MIMALLOC_LIB)
        target_link_libraries(tfs ${MIMALLOC_LIB})
    endif()
endif()

target_include_directories(tfs PUBLIC)

### Git Version ###
# Define the two required variables before including
# the source code for watching a git repository.
option(SKIP_GIT "Skip checking for git updates" OFF)
if(NOT SKIP_GIT)
    set(PRE_CONFIGURE_FILE "cmake/gitmetadata.h.in")
    set(POST_CONFIGURE_FILE "${CMAKE_CURRENT_BINARY_DIR}/gitmetadata.h")
    include(git_watcher)
    if(Git_FOUND)
        add_dependencies(tfs check_git)
        target_include_directories(tfs PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()
### END Git Version ###

if (ENABLE_PCH AND WIN32)
    target_precompile_headers(tfs PUBLIC src/otpch.h)
endif()
