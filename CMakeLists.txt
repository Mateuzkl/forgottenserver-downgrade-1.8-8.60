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

set(BOOST_REQUIRED_VERSION 1.71.0)

option(HTTP "Enable HTTP support" ON)
if (HTTP)
    list(APPEND VCPKG_MANIFEST_FEATURES "http")
    set(BOOST_REQUIRED_VERSION 1.75.0)
endif()

option(BUILD_TESTING "Build unit tests" OFF)
if (BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "unit-tests")
endif()

option(ENABLE_ASAN "Enable AddressSanitizer for memory diagnostics" OFF)
option(ENABLE_NATIVE_OPTIMIZATIONS "Enable -march=native/-mtune=native for local Release builds" ON)

option(USE_LUAJIT "Use LuaJIT" OFF)
if (USE_LUAJIT)
    list(APPEND VCPKG_MANIFEST_FEATURES "luajit")
else()
    list(APPEND VCPKG_MANIFEST_FEATURES "lua")
endif()

project(tfs CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Strict warnings only for our sources, not for dependencies
if (NOT WIN32)
    add_compile_options(-Wall -Wextra -Wnon-virtual-dtor -pedantic -Werror -pipe -fvisibility=hidden)
    add_compile_options(-Wno-error=old-style-cast)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fno-strict-aliasing)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-Wimplicit-fallthrough -Wmove)
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

# Find packages.
find_package(OpenSSL 3.0.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
find_package(absl CONFIG REQUIRED)

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

# Selects LuaJIT if user defines or auto-detected
if (USE_LUAJIT)
    find_package(LuaJIT REQUIRED)
else ()
    set(TFS_LUA_REQUIRED_VERSION "5.5")
    message(STATUS "LuaJIT disabled; requiring Lua ${TFS_LUA_REQUIRED_VERSION}. See README.md for the manual Lua 5.5 setup.")
    find_package(Lua "${TFS_LUA_REQUIRED_VERSION}" REQUIRED)
endif ()

if (APPLE)
    find_package(Iconv REQUIRED)
endif()

# Force UTF-8 support in MSVC (Visual Studio/VSCode on Windows)
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(BOOST_REQUIRED_COMPONENTS system iostreams locale)
if (HTTP)
    list(APPEND BOOST_REQUIRED_COMPONENTS json)
endif ()
if (BUILD_TESTING)
    list(APPEND BOOST_REQUIRED_COMPONENTS unit_test_framework)
endif ()
find_package(Boost ${BOOST_REQUIRED_VERSION} REQUIRED COMPONENTS ${BOOST_REQUIRED_COMPONENTS})

if (NOT DEFINED DISABLE_STATS OR NOT DISABLE_STATS)
    message(STATUS "OTS stats enabled. Run 'cmake -DDISABLE_STATS=1 ..' to disable")
    ADD_DEFINITIONS(-DSTATS_ENABLED)
else ()
    message(STATUS "OTS stats disabled. Run 'cmake -DDISABLE_STATS=0 ..' to enable")
    ADD_DEFINITIONS(-USTATS_ENABLED)
endif ()

include_directories(${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${LUA_INCLUDE_DIR} ${MYSQL_INCLUDE_DIR} ${PUGIXML_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)

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

if (BUILD_TESTING)
    message(STATUS "Building unit tests")
    enable_testing()
    add_test(
        NAME build_source_list_parity
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
endif()

# Option to disable unity builds
option(ENABLE_UNITY_BUILD "Enable unity build" ON)
if (ENABLE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
endif()

### INTERPROCEDURAL OPTIMIZATION ###
if (ENABLE_ASAN)
    message(STATUS "IPO/LTO disabled because ENABLE_ASAN is enabled")
else ()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT error)
    if (result)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    else ()
        message(STATUS "IPO is not supported: ${error}")
    endif ()
endif ()
### END INTERPROCEDURAL OPTIMIZATION ###

add_subdirectory(src)

add_executable(tfs ${tfs_MAIN})
target_link_libraries(tfs tfslib ZLIB::ZLIB OpenSSL::Crypto absl::flat_hash_map)

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

target_precompile_headers(tfs PUBLIC src/otpch.h)
