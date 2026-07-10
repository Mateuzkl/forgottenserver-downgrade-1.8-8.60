#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAPTURE_SCRIPT="${ROOT_DIR}/scripts/profiling/capture_flamegraph.sh"
PROCESS_NAME="${TFS_FLAMEGRAPH_PROCESS:-tfs}"

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
		--pid|--process)
			has_target=1
			break
			;;
	esac
done

if [[ "${has_target}" -eq 0 ]]; then
	capture_args=(--process "${PROCESS_NAME}" "${capture_args[@]}")
fi

cd "${ROOT_DIR}"
printf 'Using perf: %s\n' "${PERF_BIN}"
printf 'Starting TFS FlameGraph capture...\n'
bash "${CAPTURE_SCRIPT}" "${capture_args[@]}"
