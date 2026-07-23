cmake_minimum_required(VERSION 3.31)

option(COMPILE_AS_CPP "Enable the lua C++ library")
option(INSTALL_TOOLS "Install compiler and interpreter")

project(lua)

set(CMAKE_C_STANDARD 99)

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(UNIX)
    add_compile_definitions(LUA_USE_POSIX LUA_USE_DLOPEN)
    find_library(HAVE_LIBM NAMES m)
endif()

set(SRC_LUAI "${PROJECT_SOURCE_DIR}/src/lua.c")
set(SRC_LUAC "${PROJECT_SOURCE_DIR}/src/luac.c")
file(GLOB SRC_LIBLUA "${PROJECT_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM SRC_LIBLUA "${SRC_LUAI}" "${SRC_LUAC}")

add_library(lua ${SRC_LIBLUA})
target_include_directories(lua PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
    $<INSTALL_INTERFACE:include>
)

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(lua PUBLIC LUA_BUILD_AS_DLL)
endif()

if(UNIX)
    target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS})
    if(HAVE_LIBM)
        target_link_libraries(lua PRIVATE m)
    endif()
endif()

install(FILES src/lualib.h src/lua.h src/luaconf.h src/lauxlib.h src/lua.hpp
    DESTINATION include
)
install(TARGETS lua
    EXPORT unofficial-lua-config
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(COMPILE_AS_CPP)
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/cpp")
    file(COPY ${SRC_LIBLUA} DESTINATION "${PROJECT_BINARY_DIR}/cpp")
    file(GLOB SRC_LIBLUACPP "${PROJECT_BINARY_DIR}/cpp/*.c")
    set_source_files_properties(${SRC_LIBLUACPP} PROPERTIES LANGUAGE CXX)
    add_library(lua-cpp ${SRC_LIBLUACPP})
    set_target_properties(lua-cpp PROPERTIES OUTPUT_NAME "lua-c++")
    target_include_directories(lua-cpp PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
        $<INSTALL_INTERFACE:include>
    )

    if(WIN32 AND BUILD_SHARED_LIBS)
        target_compile_definitions(lua-cpp PUBLIC LUA_BUILD_AS_DLL)
    endif()

    if(UNIX)
        target_compile_definitions(lua-cpp PUBLIC LUA_USE_DLOPEN)
        target_link_libraries(lua-cpp PRIVATE ${CMAKE_DL_LIBS})
        if(HAVE_LIBM)
            target_link_libraries(lua-cpp PRIVATE m)
        endif()
    endif()

    install(TARGETS lua-cpp
        EXPORT unofficial-lua-config
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()

if(INSTALL_TOOLS)
    add_executable(luac "${SRC_LUAC}" $<TARGET_OBJECTS:lua>)
    target_include_directories(luac PRIVATE "${PROJECT_SOURCE_DIR}/src")
    if(UNIX AND HAVE_LIBM)
        target_link_libraries(luac PRIVATE m)
    endif()

    add_executable(luai "${SRC_LUAI}")
    set_target_properties(luai PROPERTIES OUTPUT_NAME "lua" PDB_NAME "luai")
    target_link_libraries(luai PRIVATE lua)
    if(UNIX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PC_READLINE readline REQUIRED)
        target_compile_definitions(luai PRIVATE LUA_USE_READLINE)
        target_include_directories(luai PRIVATE ${PC_READLINE_INCLUDE_DIRS})
        target_link_libraries(luai PRIVATE ${PC_READLINE_LINK_LIBRARIES})
    endif()

    install(TARGETS luai luac DESTINATION bin)
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/unofficial-lua-config-version.cmake"
    VERSION "${VERSION}"
    COMPATIBILITY "SameMajorVersion"
)
install(FILES "${PROJECT_BINARY_DIR}/unofficial-lua-config-version.cmake"
    DESTINATION share/unofficial-lua
)
install(EXPORT unofficial-lua-config
    NAMESPACE unofficial::lua::
    DESTINATION share/unofficial-lua
)
