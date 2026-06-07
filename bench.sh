#!/usr/bin/env bash
set -Eeuo pipefail

BUILD_DIR="build-bench"
JOBS="${JOBS:-$(nproc 2>/dev/null || printf '2')}"
CLEAN_BUILD=0
FILTER=""

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
	cat <<'EOF'
Usage: ./bench.sh [options]

Options:
  --clean                Remove build-bench before configuring
  --filter PATTERN       Run only benchmarks matching PATTERN (passed to ctest -R)
  --jobs N               Parallel build jobs (default: nproc)
  -h, --help             Show this help

Examples:
  ./bench.sh                        # Build and run all benchmarks
  ./bench.sh --clean                # Clean rebuild
  ./bench.sh --filter bench_tools   # Run only bench_tools
EOF
}

parse_args() {
	while (($#)); do
		case "$1" in
			--clean)
				CLEAN_BUILD=1
				shift
				;;
			--filter)
				[[ $# -ge 2 ]] || { echo "--filter requires a value"; exit 1; }
				FILTER="$2"
				shift 2
				;;
			--jobs)
				[[ $# -ge 2 ]] || { echo "--jobs requires a value"; exit 1; }
				JOBS="$2"
				shift 2
				;;
			-h|--help)
				usage
				exit 0
				;;
			*)
				echo "unknown option: $1"
				usage
				exit 1
				;;
		esac
	done
}

main() {
	parse_args "$@"

	cd "${SCRIPT_DIR}"
	[[ -f "CMakeLists.txt" ]] || { echo "CMakeLists.txt not found in $(pwd)"; exit 1; }

	echo "=== Benchmark Build ==="

	if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
		echo "Cleaning build directory: ${BUILD_DIR}"
		rm -rf "${BUILD_DIR}"
	fi

	echo "Configuring CMake with BUILD_BENCHMARKING=ON..."
	cmake -S . -B "${BUILD_DIR}" \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_BENCHMARKING=ON \
		-DCMAKE_PREFIX_PATH="/usr/local;${HOME}/.local" \
		-DLUA_INCLUDE_DIR=/usr/local/include \
		-DLUA_LIBRARY=/usr/local/lib/liblua.a \
		-DLUA_LIBRARIES='/usr/local/lib/liblua.a;m;dl' \
		-DLUA_VERSION_STRING=5.5.0 \
		-DDISABLE_STATS=1 \
		-DENABLE_NATIVE_OPTIMIZATIONS=OFF \
		-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF \
		-Wno-dev

	echo "Building benchmarks..."
	cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

	echo ""
	echo "=== Running Benchmarks ==="

	local ctest_args=(--test-dir "${BUILD_DIR}" --output-on-failure)
	if [[ -n "${FILTER}" ]]; then
		ctest_args+=(-R "${FILTER}")
		echo "Filter: ${FILTER}"
	fi

	ctest "${ctest_args[@]}"

	echo ""
	echo "=== Done ==="
}

main "$@"
