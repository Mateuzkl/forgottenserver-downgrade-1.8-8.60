#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

PROFILE="${TFS_BUILD_PROFILE:-balanced}"
BUILD_DIR="${TFS_BENCH_BUILD_DIR:-}"
JOBS="${JOBS:-}"
CLEAN_BUILD=0
FILTER=""
LTO_MODE="off"
EXTRA_ARGS=()

usage() {
	cat <<'EOF'
Usage: ./bench.sh [options]

Options:
  --profile PROFILE      Build profile: low-memory|balanced|full-performance (default: balanced)
  --clean                Remove benchmark build directory before configuring
  --filter PATTERN       Run only benchmarks matching PATTERN (passed to ctest -R)
  --jobs N               Override adaptive parallel build jobs
  --build-dir PATH       Override benchmark build directory
  --lto on|off           Enable/disable LTO for benchmarks (default: off)
  --reconfigure          Force CMake reconfigure
  -h, --help             Show this help

Examples:
  ./bench.sh                              # Build and run all benchmarks (adaptive WSL2-safe)
  ./bench.sh --profile low-memory         # Minimal RAM usage
  ./bench.sh --clean                      # Clean rebuild
  ./bench.sh --filter bench_networkmessage
EOF
}

parse_args() {
	while (($#)); do
		case "$1" in
			--profile)
				[[ $# -ge 2 ]] || { echo "--profile requires a value"; exit 1; }
				PROFILE="$2"
				shift 2
				;;
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
			--build-dir)
				[[ $# -ge 2 ]] || { echo "--build-dir requires a value"; exit 1; }
				BUILD_DIR="$2"
				shift 2
				;;
			--lto)
				[[ $# -ge 2 ]] || { echo "--lto requires a value"; exit 1; }
				LTO_MODE="${2,,}"
				shift 2
				;;
			--reconfigure)
				EXTRA_ARGS+=(--reconfigure)
				shift
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

	local -a args=(
		--non-interactive
		--skip-deps
		--profile "${PROFILE}"
		--benchmarks
		--stats off
		--portable
		--lto "${LTO_MODE}"
	)

	if [[ -n "${BUILD_DIR}" ]]; then
		args+=(--build-dir "${BUILD_DIR}")
	fi
	if [[ -n "${JOBS}" ]]; then
		args+=(--jobs "${JOBS}")
	fi
	if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
		args+=(--clean)
	fi
	if [[ -n "${FILTER}" ]]; then
		args+=(--test-filter "${FILTER}")
	fi
	if ((${#EXTRA_ARGS[@]} > 0)); then
		args+=("${EXTRA_ARGS[@]}")
	fi

	exec "${SCRIPT_DIR}/build.sh" "${args[@]}"
}

main "$@"
