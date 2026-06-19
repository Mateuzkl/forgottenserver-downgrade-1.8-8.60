#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-asan-linux"
LOG_DIR="${ROOT_DIR}/asan-logs"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
LOG_PREFIX="${LOG_DIR}/asan_${RUN_ID}"
UBSAN_LOG_PREFIX="${LOG_DIR}/ubsan_${RUN_ID}"
DEFINITIVE_LOG="${ROOT_DIR}/asan-definitive.log"
RUN_LOG="${LOG_DIR}/run_${RUN_ID}.log"
JOBS="${JOBS:-$(nproc)}"

source "${ROOT_DIR}/tools/cmake-linux-env.sh"

cd "${ROOT_DIR}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake not found"
	exit 1
fi

tfs_check_lua55_paths

cmake_args=(--preset asan-linux)
tfs_append_linux_cmake_cache_args cmake_args
cmake "${cmake_args[@]}"
cmake --build --preset asan-linux --parallel "${JOBS}"

ASAN_BIN="${BUILD_DIR}/tfs"
if [[ ! -x "${ASAN_BIN}" ]]; then
	echo "ASan build binary not found: ${ASAN_BIN}"
	exit 1
fi

if [[ "${TFS_BUILD_ONLY:-0}" == "1" ]]; then
	echo "ASan build completed: ${ASAN_BIN}"
	exit 0
fi

mkdir -p "${LOG_DIR}"

# Keep string interceptors in the default/compatible mode. libmysqlclient can
# trip strict string checks during mysql_real_connect before the server starts.
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:strict_string_checks=0:log_path=${LOG_PREFIX}"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:log_path=${UBSAN_LOG_PREFIX}"

{
	echo "Running AddressSanitizer without mimalloc."
	echo "Combined ASan run log: ${DEFINITIVE_LOG}"
	echo "This run log copy: ${RUN_LOG}"
	echo "ASan crash logs will be written as ${LOG_PREFIX}.<pid> if an error is detected."
	echo "UBSan crash logs will be written as ${UBSAN_LOG_PREFIX}.<pid> if an error is detected."
} | tee "${DEFINITIVE_LOG}" "${RUN_LOG}"

set +e
"${ASAN_BIN}" 2>&1 | tee -a "${DEFINITIVE_LOG}" "${RUN_LOG}"
status=${PIPESTATUS[0]}
set -e

shopt -s nullglob
sanitizer_logs=("${LOG_PREFIX}".* "${UBSAN_LOG_PREFIX}".*)

if ((${#sanitizer_logs[@]})); then
	for log_file in "${sanitizer_logs[@]}"; do
		{
			echo
			echo "===== ${log_file} ====="
			cat "${log_file}"
		} | tee -a "${DEFINITIVE_LOG}" "${RUN_LOG}"
	done
else
	{
		echo
		echo "No ASan/UBSan crash log was generated for this run."
		echo "That usually means the sanitizer did not detect an error before the server stopped."
	} | tee -a "${DEFINITIVE_LOG}" "${RUN_LOG}"
fi

exit "${status}"
