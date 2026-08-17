#!/usr/bin/env bash
# Baseline measurement runner for perf/profile-baseline.
#
#   ./benchmarks/perf_baseline/run.sh            # baseline flag set only
#   ./benchmarks/perf_baseline/run.sh --sweep    # all four flag sets
#
# The baseline is the one the Release build actually uses. Reporting a speedup
# against -O0/-O1/-O2 is how this plan got its priorities wrong once already, so
# --sweep exists to make that visible rather than to be quoted on its own.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/tfs_perf_baseline"
mkdir -p "$OUT"

BASELINE="-O3 -march=native -mtune=native -fomit-frame-pointer -DNDEBUG"

# src/xtea.cpp pulls in otpch.h, which needs the vcpkg include tree. Reuse the
# include list CMake already resolved instead of guessing paths. Point
# TFS_BUILD_DIR at another build tree if build-release is not the one you use.
BUILD_DIR="${TFS_BUILD_DIR:-$ROOT/build-release}"
FLAGS_MAKE="$(find "$BUILD_DIR" -name flags.make -path '*tfslib*' 2>/dev/null | head -1)"
if [ -n "$FLAGS_MAKE" ]; then
	PROJECT_INCLUDES="$(sed -n 's/^CXX_INCLUDES = //p' "$FLAGS_MAKE")"
else
	PROJECT_INCLUDES=""
	echo "WARNING: no configured build tree found under $BUILD_DIR."
	echo "         Configure one first:  cmake -B build-release -DCMAKE_BUILD_TYPE=Release"
	echo "         The XTEA benchmark needs it to locate the vcpkg headers."
	echo
fi

if [ "${1:-}" = "--sweep" ]; then
	FLAG_SETS=("-O2" "-O3" "-O3 -mavx2" "$BASELINE")
else
	FLAG_SETS=("$BASELINE")
fi

echo "########## environment ##########"
if command -v lscpu > /dev/null; then
	lscpu | grep -E '^Model name|^CPU\(s\):' | sed 's/  */ /g'
	echo -n "isa: "
	lscpu | grep -o -E 'avx512f|avx2|sse4_2|sse2' | sort -u | tr '\n' ' '
	echo
fi
"${CXX:-g++}" --version | head -1
echo "commit: $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "branch: $(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
echo

status=0

for flags in "${FLAG_SETS[@]}"; do
	echo "##################################################################"
	if [ "$flags" = "$BASELINE" ]; then
		echo "# FLAGS: $flags   <-- BASELINE (what Release actually builds with)"
	else
		echo "# FLAGS: $flags   (context only, not a valid baseline)"
	fi
	echo "##################################################################"

	# shellcheck disable=SC2086
	# FMT_HEADER_ONLY: otpch.h drags spdlog/fmt in. The benchmark only needs the
	# XTEA loop, so make fmt header-only rather than linking the engine's deps.
	"${CXX:-g++}" $flags -std=c++23 -DFMT_HEADER_ONLY -I"$ROOT/src" $PROJECT_INCLUDES \
		-o "$OUT/bench_xtea" \
		"$ROOT/benchmarks/perf_baseline/bench_xtea.cpp" "$ROOT/src/xtea.cpp" 2>&1 | head -20
	if [ ! -x "$OUT/bench_xtea" ]; then
		echo "xtea build FAILED"; status=1; continue
	fi
	"$OUT/bench_xtea" || status=1
	echo

	# shellcheck disable=SC2086
	if ! "${CXX:-g++}" $flags -std=c++23 \
		-o "$OUT/bench_adler" \
		"$ROOT/benchmarks/perf_baseline/bench_adler.cpp" -lz 2>&1 | head -20; then
		echo "adler build FAILED"; status=1; continue
	fi
	"$OUT/bench_adler" || status=1
	echo
done

exit $status
