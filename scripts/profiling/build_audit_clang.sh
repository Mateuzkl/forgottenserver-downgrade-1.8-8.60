#!/usr/bin/env bash
set -euo pipefail
audit_root=${1:?pass the isolated audit root}
build_dir="$audit_root/candidate-clang-diagnostic"
cmake -S "$audit_root/candidate-source" -B "$build_dir" -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g1 -DNDEBUG' -DBUILD_TESTING=ON \
  -DENABLE_TEST_LTO=OFF -DENABLE_NATIVE_OPTIMIZATIONS=OFF -DUSE_MIMALLOC=OFF \
  "-DLUA_INCLUDE_DIR=${TFS_AUDIT_LUA_INCLUDE:-/usr/local/include}" \
  "-DLUA_LIBRARY=${TFS_AUDIT_LUA_LIBRARY:-/usr/local/lib/liblua.a}" \
  "-DCMAKE_PREFIX_PATH=${TFS_AUDIT_PREFIX_PATH:-/usr/local;$HOME/.local}" \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > "$audit_root/clang-configure.log" 2>&1
targets=(test_combat_messages test_combat_reentry test_map_spectators
  test_party_lifetime test_lua_callback_contract test_networkmessage_wire
  test_protocolgame_pipeline test_connection_write_lifetime test_xtea
  test_regression_crash test_condition_shared_lifetime test_condition_damage_queue
  test_reactor test_pathfinding)
cmake --build "$build_dir" --parallel 2 --target "${targets[@]}" \
  > "$audit_root/clang-build.log" 2>&1 || { tail -70 "$audit_root/clang-build.log"; exit 1; }
pattern=$(IFS='|'; echo "${targets[*]}")
ctest --test-dir "$build_dir" --output-on-failure -j2 -R "^($pattern)$" \
  > "$audit_root/clang-ctest.log" 2>&1 || { tail -100 "$audit_root/clang-ctest.log"; exit 1; }
tail -20 "$audit_root/clang-ctest.log"
