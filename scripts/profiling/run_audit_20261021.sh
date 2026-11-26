#!/usr/bin/env bash
# Isolated baseline: never loads the developer's edited world or starts a server.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
baseline=$(git -C "$repo_root" rev-parse origin/main)
audit_root=$(mktemp -d "${TFS_AUDIT_PARENT:-${TMPDIR:-/tmp}}/tfs-audit-20261021-XXXXXX")
printf 'AUDIT_ROOT=%s\nBASELINE=%s\n' "$audit_root" "$baseline"
mkdir "$audit_root/baseline-source"
git -C "$repo_root" archive "$baseline" | tar -x -C "$audit_root/baseline-source"
launcher=()
if command -v ccache >/dev/null; then launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache); fi
cmake -S "$audit_root/baseline-source" -B "$audit_root/baseline-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_TEST_LTO=OFF \
  -DENABLE_NATIVE_OPTIMIZATIONS=OFF -DUSE_MIMALLOC=OFF \
  "-DLUA_INCLUDE_DIR=${TFS_AUDIT_LUA_INCLUDE:-/usr/local/include}" \
  "-DLUA_LIBRARY=${TFS_AUDIT_LUA_LIBRARY:-/usr/local/lib/liblua.a}" \
  "-DCMAKE_PREFIX_PATH=${TFS_AUDIT_PREFIX_PATH:-/usr/local;$HOME/.local}" "${launcher[@]}" \
  > "$audit_root/baseline-configure.log" 2>&1 || {
    tail -80 "$audit_root/baseline-configure.log"; exit 1;
  }
cmake --build "$audit_root/baseline-build" --parallel 2 \
  > "$audit_root/baseline-build.log" 2>&1 || {
    tail -80 "$audit_root/baseline-build.log"; exit 1;
  }
ctest --test-dir "$audit_root/baseline-build" --output-on-failure -j2 \
  > "$audit_root/baseline-ctest.log" 2>&1 || {
    tail -120 "$audit_root/baseline-ctest.log"; exit 1;
  }
tail -20 "$audit_root/baseline-ctest.log"
