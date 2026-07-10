#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  capture_flamegraph.sh --pid PID [options]
  capture_flamegraph.sh --process NAME [options]

Options:
  --pid PID              Target process id.
  --process NAME         Resolve PID with pidof.
  --duration SECONDS     Capture duration. Default: 60.
  --freq HZ              Sampling frequency. Default: 99.
  --title TITLE          FlameGraph title. Default: TFS flamegraph.
  --output PATH          Output SVG. Default: profiling-output/tfs.svg.
  --filter REGEX         Filter folded stacks before rendering.
  -h, --help             Show this help.
USAGE
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

pid=""
process_name=""
duration="60"
freq="99"
title="TFS flamegraph"
output="profiling-output/tfs.svg"
filter=""

while (($# > 0)); do
  case "$1" in
    --pid)
      [[ $# -ge 2 ]] || die "--pid requires a value"
      pid="$2"
      shift 2
      ;;
    --process)
      [[ $# -ge 2 ]] || die "--process requires a value"
      process_name="$2"
      shift 2
      ;;
    --duration)
      [[ $# -ge 2 ]] || die "--duration requires a value"
      duration="$2"
      shift 2
      ;;
    --freq)
      [[ $# -ge 2 ]] || die "--freq requires a value"
      freq="$2"
      shift 2
      ;;
    --title)
      [[ $# -ge 2 ]] || die "--title requires a value"
      title="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || die "--output requires a value"
      output="$2"
      shift 2
      ;;
    --filter)
      [[ $# -ge 2 ]] || die "--filter requires a value"
      filter="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "${pid}" || -n "${process_name}" ]] || die "use --pid or --process"
[[ -z "${pid}" || -z "${process_name}" ]] || die "use only one of --pid or --process"
[[ "${duration}" =~ ^[0-9]+$ && "${duration}" -ge 1 ]] || die "--duration must be a positive integer"
[[ "${freq}" =~ ^[0-9]+$ && "${freq}" -ge 1 ]] || die "--freq must be a positive integer"

if [[ -n "${process_name}" ]]; then
  command -v pidof >/dev/null 2>&1 || die "pidof not found"
  pid="$(pidof -s "${process_name}" || true)"
  [[ -n "${pid}" ]] || die "process not found: ${process_name}"
fi

[[ "${pid}" =~ ^[0-9]+$ ]] || die "invalid PID: ${pid}"

perf_bin="$(command -v "${PERF_BIN:-perf}" || true)"
[[ -n "${perf_bin}" ]] || die "perf not found. Run scripts/profiling/setup_flamegraph.sh first."
command -v perl >/dev/null 2>&1 || die "perl not found"

flamegraph_dir="${FLAMEGRAPH_DIR:-${HOME}/FlameGraph}"
stackcollapse="${flamegraph_dir}/stackcollapse-perf.pl"
flamegraph="${flamegraph_dir}/flamegraph.pl"

[[ -f "${stackcollapse}" ]] || die "missing ${stackcollapse}. Run setup_flamegraph.sh first."
[[ -f "${flamegraph}" ]] || die "missing ${flamegraph}. Run setup_flamegraph.sh first."

output_dir="$(dirname -- "${output}")"
output_file="$(basename -- "${output}")"
base_name="${output_file%.svg}"
intermediate_dir="${output_dir}"

mkdir -p "${output_dir}"

perf_data="${intermediate_dir}/${base_name}.perf.data"
perf_script="${intermediate_dir}/${base_name}.perf"
folded="${intermediate_dir}/${base_name}.folded"
filtered_folded="${intermediate_dir}/${base_name}.filtered.folded"

use_sudo=0
record_cmd=("${perf_bin}" record -F "${freq}" -p "${pid}" -g -o "${perf_data}" -- sleep "${duration}")
if [[ "${PERF_USE_SUDO:-1}" != "0" && "${EUID}" -ne 0 ]]; then
  command -v sudo >/dev/null 2>&1 || die "sudo not found. Set PERF_USE_SUDO=0 if perf is allowed without sudo."
  use_sudo=1
  record_cmd=(sudo "${record_cmd[@]}")
fi

printf 'Capturing PID %s for %ss at %s Hz...\n' "${pid}" "${duration}" "${freq}"
"${record_cmd[@]}"

printf 'Writing perf script: %s\n' "${perf_script}"
if [[ "${use_sudo}" -eq 1 ]]; then
  sudo "${perf_bin}" script -i "${perf_data}" > "${perf_script}"
else
  "${perf_bin}" script -i "${perf_data}" > "${perf_script}"
fi

printf 'Writing folded stacks: %s\n' "${folded}"
perl "${stackcollapse}" "${perf_script}" > "${folded}"

render_input="${folded}"
if [[ -n "${filter}" ]]; then
  printf 'Filtering folded stacks with regex: %s\n' "${filter}"
  if ! grep -Ei "${filter}" "${folded}" > "${filtered_folded}"; then
    die "no folded stacks matched filter: ${filter}"
  fi
  render_input="${filtered_folded}"
fi

printf 'Writing flamegraph SVG: %s\n' "${output}"
perl "${flamegraph}" --title="${title}" "${render_input}" > "${output}"

printf 'Done.\n'
printf 'Intermediate files:\n'
printf '  %s\n' "${perf_data}" "${perf_script}" "${folded}"
if [[ -n "${filter}" ]]; then
  printf '  %s\n' "${filtered_folded}"
fi
