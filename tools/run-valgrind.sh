#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-valgrind-linux"
ROOT_BIN="${ROOT_DIR}/tfs"
BUILD_BIN="${BUILD_DIR}/tfs"
LOG_FILE="valgrind-definitive.log"
JOBS="${JOBS:-$(nproc)}"
VALGRIND_ERROR_EXITCODE="${TFS_VALGRIND_ERROR_EXITCODE:-99}"
VALGRIND_SHOW_LEAK_KINDS="${TFS_VALGRIND_SHOW_LEAK_KINDS:-definite}"

source "${ROOT_DIR}/tools/cmake-linux-env.sh"

cd "${ROOT_DIR}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake not found"
	exit 1
fi

if ! command -v valgrind >/dev/null 2>&1; then
	echo "valgrind not found"
	exit 1
fi

tfs_check_lua55_paths

cmake_args=(--preset valgrind-linux)
tfs_append_linux_cmake_cache_args cmake_args
cmake "${cmake_args[@]}"
cmake --build --preset valgrind-linux --parallel "${JOBS}"

if [[ ! -x "${BUILD_BIN}" ]]; then
	echo "Valgrind build binary not found: ${BUILD_BIN}"
	exit 1
fi

if [[ "${TFS_BUILD_ONLY:-0}" == "1" ]]; then
	echo "Valgrind build completed: ${BUILD_BIN}"
	exit 0
fi

# Keep the runtime cwd at the repository root because TFS expects config.lua and data/
# next to the executable. The definitive command below intentionally runs ./tfs.
cp "${BUILD_BIN}" "${ROOT_BIN}"
chmod +x "${ROOT_BIN}"

echo "Running definitive Valgrind memcheck. Log: ${ROOT_DIR}/${LOG_FILE}"
echo "Valgrind error exit code: ${VALGRIND_ERROR_EXITCODE}"
echo "Valgrind leak kinds: ${VALGRIND_SHOW_LEAK_KINDS}"
valgrind \
	--tool=memcheck \
	--leak-check=full \
	--show-leak-kinds="${VALGRIND_SHOW_LEAK_KINDS}" \
	--error-exitcode="${VALGRIND_ERROR_EXITCODE}" \
	--track-origins=yes \
	--num-callers=50 \
	--leak-resolution=high \
	--error-limit=no \
	--expensive-definedness-checks=yes \
	--partial-loads-ok=yes \
	--log-file="${LOG_FILE}" \
	./tfs
