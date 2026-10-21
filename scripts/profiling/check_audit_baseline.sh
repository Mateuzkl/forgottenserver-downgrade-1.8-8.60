#!/usr/bin/env bash
# Link new regressions against the immutable, already built baseline library.
set -euo pipefail
audit_root=${1:?pass the isolated audit root}
shift
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
regression_source="$audit_root/regression-source"
if [[ ! -d "$regression_source" ]]; then cp -a "$audit_root/baseline-source" "$regression_source"; fi
# Never compile current CombatDamage headers against the old library: its
# layout changed. Copy only the new tests/probe into a baseline source copy.
for relative in src/tests/test_combat_messages.cpp src/tests/test_map_spectators.cpp \
  src/tests/test_combat_reentry.cpp src/tests/test_party_lifetime.cpp \
  src/tests/test_lua_callback_contract.cpp src/tests/test_networkmessage_wire.cpp \
  src/tests/protocolgame_audit_support.h src/benchs/bench_networkmessage.cpp \
  src/benchs/small_packet_probe.h; do
  cp "$repo_root/$relative" "$regression_source/$relative"
done
mkdir -p "$audit_root/red-tests"
cd "$audit_root/baseline-build"
read -r -a original <<< "$(ninja -t commands test_combat | tail -1)"
if [[ $# == 0 ]]; then set -- test_combat_messages test_map_spectators test_combat_reentry; fi
for test_name in "$@"; do
  source_path="$regression_source/src/tests/$test_name.cpp"
  if [[ "$test_name" == bench_* ]]; then source_path="$regression_source/src/benchs/$test_name.cpp"; fi
  args=()
  for arg in "${original[@]}"; do
    case "$arg" in
      :|'&&') ;;
      src/tests/CMakeFiles/test_combat.dir/Unity/unity_0_cxx.cxx.o)
        args+=("$source_path");;
      src/tests/test_combat) args+=("$audit_root/red-tests/$test_name");;
      *) args+=("$arg");;
    esac
  done
  args+=(-std=c++23 -Wall -Wextra -Werror -DSTATS_ENABLED -DASIO_STANDALONE
    -DFMT_SHARED -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DSPDLOG_SHARED_LIB
    "-I${TFS_AUDIT_LUA_INCLUDE:-/usr/local/include}" -I/usr/include/mysql -isystem "$HOME/.local/include"
    "-I$regression_source/src" -fstack-clash-protection
    # Test-only access aliases reuse friends that already exist on baseline;
    # no production field, class layout, or baseline header is modified.
    -DProtocolGameAuditTestAccess=ProtocolGameSpellCooldownTestAccess
    -DPartyLifetimeTestAccess=LootHighlightTestAccess)
  if [[ "$test_name" == bench_* ]]; then args+=(-lbenchmark); fi
  "${args[@]}" > "$audit_root/red-tests/$test_name-build.log" 2>&1 || {
    tail -50 "$audit_root/red-tests/$test_name-build.log"; exit 1;
  }
  status=0
  run_args=()
  if [[ "$test_name" == bench_* ]]; then
    run_args=(--benchmark_min_time=0.1s --benchmark_repetitions=5
      "--benchmark_out=$audit_root/red-tests/$test_name.json" --benchmark_out_format=json)
  fi
  "$audit_root/red-tests/$test_name" "${run_args[@]}" > "$audit_root/red-tests/$test_name.log" 2>&1 || status=$?
  printf '%s exit=%s\n' "$test_name" "$status"
  tail -15 "$audit_root/red-tests/$test_name.log"
done
