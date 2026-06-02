#!/usr/bin/env bash

# Shared Linux/WSL CMake cache arguments for helper builds.
# These match the manual Lua 5.5, simdutf and mio locations prepared by build.sh.

TFS_LUA_VERSION="${TFS_LUA_VERSION:-5.5.0}"
TFS_LUA_PREFIX="${TFS_LUA_PREFIX:-/usr/local}"
TFS_LUA_INCLUDE_DIR="${TFS_LUA_INCLUDE_DIR:-${TFS_LUA_PREFIX}/include}"
TFS_LUA_LIBRARY="${TFS_LUA_LIBRARY:-${TFS_LUA_PREFIX}/lib/liblua.a}"
TFS_SIMDUTF_PREFIX="${TFS_SIMDUTF_PREFIX:-${HOME}/.local}"

tfs_linux_cmake_prefix_path() {
	local -a prefixes=()

	prefixes+=("${TFS_LUA_PREFIX}" "${TFS_SIMDUTF_PREFIX}")

	if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
		prefixes+=("${CMAKE_PREFIX_PATH}")
	fi

	local IFS=';'
	printf '%s' "${prefixes[*]}"
}

tfs_append_linux_cmake_cache_args() {
	local -n cmake_args_ref="$1"
	local prefix_path
	prefix_path="$(tfs_linux_cmake_prefix_path)"

	cmake_args_ref+=(
		-DLUA_INCLUDE_DIR="${TFS_LUA_INCLUDE_DIR}"
		-DLUA_LIBRARY="${TFS_LUA_LIBRARY}"
		-DLUA_LIBRARIES="${TFS_LUA_LIBRARY};m;dl"
		-DLUA_VERSION_STRING="${TFS_LUA_VERSION}"
		-DCMAKE_PREFIX_PATH="${prefix_path}"
	)
}

tfs_check_lua55_paths() {
	if [[ ! -d "${TFS_LUA_INCLUDE_DIR}" || ! -f "${TFS_LUA_LIBRARY}" ]]; then
		cat >&2 <<EOF
Lua ${TFS_LUA_VERSION} was not found at:
  include: ${TFS_LUA_INCLUDE_DIR}
  library: ${TFS_LUA_LIBRARY}

Run ./build.sh once to prepare dependencies, or override:
  TFS_LUA_PREFIX=/custom/lua/prefix
  TFS_LUA_INCLUDE_DIR=/custom/include
  TFS_LUA_LIBRARY=/custom/lib/liblua.a
EOF
		return 1
	fi
}
