#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAPTURE_SCRIPT="${ROOT_DIR}/scripts/profiling/capture_flamegraph.sh"
PROCESS_NAME="${TFS_FLAMEGRAPH_PROCESS:-tfs}"
SERVER_BIN="${TFS_FLAMEGRAPH_BIN:-${ROOT_DIR}/build-release/tfs}"
SERVER_LOG="${ROOT_DIR}/profiling-output/tfs-flamegraph-server.log"
START_TIMEOUT="${TFS_FLAMEGRAPH_START_TIMEOUT:-120}"
server_pid=""

die() {
	echo "$*" >&2
	exit 1
}

cleanup() {
	local pid="${server_pid}"
	server_pid=""

	if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
		printf 'Stopping FlameGraph TFS process %s...\n' "${pid}"
		kill -INT "${pid}" 2>/dev/null || true
		wait "${pid}" 2>/dev/null || true
	fi
}

start_server() {
	[[ -x "${SERVER_BIN}" ]] || die "TFS binary not found: ${SERVER_BIN}. Build build-release first."
	[[ "${START_TIMEOUT}" =~ ^[0-9]+$ && "${START_TIMEOUT}" -ge 1 ]] || die "TFS_FLAMEGRAPH_START_TIMEOUT must be a positive integer"

	mkdir -p "$(dirname "${SERVER_LOG}")"
	: > "${SERVER_LOG}"

	printf 'Starting TFS: %s\n' "${SERVER_BIN}"
	"${SERVER_BIN}" > "${SERVER_LOG}" 2>&1 &
	server_pid=$!
	printf 'Waiting for TFS startup (PID %s)...\n' "${server_pid}"

	local elapsed=0
	while ((elapsed < START_TIMEOUT)); do
		if grep -Fq "Forgotten Server Online!" "${SERVER_LOG}"; then
			printf 'TFS is online. Server log: %s\n' "${SERVER_LOG}"
			return 0
		fi

		if ! kill -0 "${server_pid}" 2>/dev/null; then
			wait "${server_pid}" 2>/dev/null || true
			server_pid=""
			tail -n 50 "${SERVER_LOG}" >&2 || true
			die "TFS stopped before becoming online"
		fi

		sleep 1
		((++elapsed))
	done

	tail -n 50 "${SERVER_LOG}" >&2 || true
	die "TFS did not become online within ${START_TIMEOUT}s"
}

trap cleanup EXIT

find_perf() {
	local requested_perf="${PERF_BIN:-}"
	local candidate=""
	local index=0

	if [[ -n "${requested_perf}" ]]; then
		candidate="$(command -v "${requested_perf}" || true)"
		[[ -n "${candidate}" ]] || return 1
		printf '%s\n' "${candidate}"
		return 0
	fi

	candidate="$(command -v perf || true)"
	if [[ -n "${candidate}" ]] && "${candidate}" --version >/dev/null 2>&1; then
		printf '%s\n' "${candidate}"
		return 0
	fi

	local candidates=()
	shopt -s nullglob
	candidates=(/usr/lib/linux-tools-*/perf)
	shopt -u nullglob

	for ((index = ${#candidates[@]} - 1; index >= 0; --index)); do
		candidate="${candidates[index]}"
		if [[ -x "${candidate}" ]] && "${candidate}" --version >/dev/null 2>&1; then
			printf '%s\n' "${candidate}"
			return 0
		fi
	done

	return 1
}

[[ -f "${CAPTURE_SCRIPT}" ]] || {
	echo "FlameGraph capture script not found: ${CAPTURE_SCRIPT}" >&2
	exit 1
}

perf_bin="$(find_perf)" || {
	echo "Compatible perf not found. Run: bash scripts/profiling/setup_flamegraph.sh" >&2
	exit 1
}
export PERF_BIN="${perf_bin}"

capture_args=("$@")
has_target=0
for arg in "$@"; do
	case "${arg}" in
		--pid|--process|-h|--help)
			has_target=1
			break
			;;
	esac
done

cd "${ROOT_DIR}"

if [[ "${has_target}" -eq 0 ]]; then
	existing_pid="$(pidof -s "${PROCESS_NAME}" 2>/dev/null || true)"
	if [[ -n "${existing_pid}" ]]; then
		printf 'Using running TFS process: %s\n' "${existing_pid}"
		capture_args=(--pid "${existing_pid}" "${capture_args[@]}")
	else
		start_server
		capture_args=(--pid "${server_pid}" "${capture_args[@]}")
	fi
fi

printf 'Using perf: %s\n' "${PERF_BIN}"
printf 'Starting TFS FlameGraph capture...\n'
bash "${CAPTURE_SCRIPT}" "${capture_args[@]}"
