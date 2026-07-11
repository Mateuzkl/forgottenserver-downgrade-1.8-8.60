#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: capture_callgrind.sh SCENARIO [DURATION]

SCENARIO: startup | idle | pathfinding | combat | movement | stress
DURATION: capture duration in seconds after collection is enabled (default: 60)

The script starts ./tfs-valgrind with collection disabled. After the server is
online, press Enter to reset counters and start the selected runtime scenario.
Press Enter again after preparing the scenario; collection then runs for the
fixed duration and writes callgrind-SCENARIO.out.<pid>.
USAGE
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

scenario="${1:-}"
duration="${2:-60}"
case "${scenario}" in
  startup|idle|pathfinding|combat|movement|stress) ;;
  -h|--help) usage; exit 0 ;;
  *) usage; die "invalid scenario: ${scenario:-<empty>}" ;;
esac
[[ "${duration}" =~ ^[0-9]+$ && "${duration}" -gt 0 ]] || die "duration must be a positive integer"

command -v valgrind >/dev/null 2>&1 || die "valgrind not found"
command -v callgrind_control >/dev/null 2>&1 || die "callgrind_control not found"
[[ -x ./tfs-valgrind ]] || die "missing ./tfs-valgrind; run tools/build-callgrind.sh"

output="callgrind-${scenario}.out.%p"
valgrind --tool=callgrind \
  --collect-atstart=no \
  --callgrind-out-file="${output}" \
  ./tfs-valgrind &
server_pid=$!

cleanup() {
  if kill -0 "${server_pid}" 2>/dev/null; then
    kill -INT "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" || true
  fi
}
trap cleanup EXIT INT TERM

printf 'Wait for server online, then press Enter.\n'
read -r
callgrind_control -p "${server_pid}" -i on
callgrind_control -p "${server_pid}" -z

printf 'Prepare scenario "%s", then press Enter to start %ss capture.\n' "${scenario}" "${duration}"
read -r
sleep "${duration}"
callgrind_control -p "${server_pid}" -d
callgrind_control -p "${server_pid}" -i off

printf 'Captured %s for PID %s.\n' "${scenario}" "${server_pid}"
