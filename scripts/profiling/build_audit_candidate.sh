#!/usr/bin/env bash
set -euo pipefail
audit_root=${1:?pass the isolated audit root}
mode=${2:-release}
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source_dir="$audit_root/candidate-source"
if [[ ! -d "$source_dir" ]]; then cp -a "$audit_root/baseline-source" "$source_dir"; fi
# Copy code and its test inputs, never the developer's live map/config/logs.
rsync -a "$repo_root/src/" "$source_dir/src/"
rsync -a "$repo_root/data/scripts/eventcallbacks/creature/default_onTargetCombat.lua" \
  "$source_dir/data/scripts/eventcallbacks/creature/default_onTargetCombat.lua"
mkdir -p "$source_dir/tools" "$source_dir/data/events/scripts"
rsync -a "$repo_root/tools/test_step_tile_callbacks.lua" \
  "$source_dir/tools/test_step_tile_callbacks.lua"
rsync -a "$repo_root/data/events/scripts/player.lua" \
  "$source_dir/data/events/scripts/player.lua"
build_dir="$audit_root/candidate-$mode"
printf 'Candidate mode=%s source=%s build=%s\n' "$mode" "$source_dir" "$build_dir"
flags=(-DCMAKE_BUILD_TYPE=Release -DENABLE_TEST_LTO=OFF)
launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=)
if command -v ccache >/dev/null 2>&1; then launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache); fi
parallel=2
if [[ "$mode" == asan ]]; then
  flags=(-DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON '-DCMAKE_CXX_FLAGS=-fsanitize=undefined' '-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g1')
  parallel=1
  export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
  export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DBUILD_TESTING=ON \
  -DENABLE_NATIVE_OPTIMIZATIONS=OFF -DUSE_MIMALLOC=OFF \
  "-DLUA_INCLUDE_DIR=${TFS_AUDIT_LUA_INCLUDE:-/usr/local/include}" \
  "-DLUA_LIBRARY=${TFS_AUDIT_LUA_LIBRARY:-/usr/local/lib/liblua.a}" \
  "-DCMAKE_PREFIX_PATH=${TFS_AUDIT_PREFIX_PATH:-/usr/local;$HOME/.local}" \
  "${launcher[@]}" "${flags[@]}" \
  > "$audit_root/candidate-$mode-configure.log" 2>&1
targets=()
for test_path in "$source_dir"/src/tests/test_*.cpp; do targets+=("$(basename "$test_path" .cpp)"); done
printf 'Building %s test executables\n' "${#targets[@]}"
cmake --build "$build_dir" --parallel "$parallel" --target "${targets[@]}" \
  > "$audit_root/candidate-$mode-build.log" 2>&1 || {
    tail -70 "$audit_root/candidate-$mode-build.log"; exit 1;
  }
printf 'Build finished; starting CTest\n'
# Keep timing-sensitive regression tests serial by default, independently of
# compilation parallelism; do not loosen their production assertions.
ctest --test-dir "$build_dir" --output-on-failure -j "${TFS_AUDIT_TEST_JOBS:-1}" \
  > "$audit_root/candidate-$mode-ctest.log" 2>&1 || {
    tail -120 "$audit_root/candidate-$mode-ctest.log"; exit 1;
  }
tail -20 "$audit_root/candidate-$mode-ctest.log"
