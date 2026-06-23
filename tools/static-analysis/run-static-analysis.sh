#!/usr/bin/env bash
# Generates static-analysis reports only. It never changes source files unless
# --fix-file explicitly names one source file for clang-tidy.

set -uo pipefail
IFS=$'\n\t'

usage() {
  cat <<'EOF'
Usage: bash tools/static-analysis/run-static-analysis.sh [options]

Without a tool option, all available tools run.

Options:
  --cppcheck              Run only Cppcheck.
  --clang-tidy            Run only clang-tidy.
  --lizard                Run only Lizard.
  --iwyu                  Run only Include What You Use.
  --deep                  Enable Cppcheck --inconclusive findings.
  --strict                Return a failure when Lizard thresholds are exceeded.
  --fix-file <path>       Run clang-tidy --fix on one explicit file below src/.
  -h, --help              Show this help.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(git -C "${SCRIPT_DIR}/../.." rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "${ROOT_DIR}" ]]; then
  echo "Unable to locate the Git repository root." >&2
  exit 1
fi

BUILD_DIR="${ROOT_DIR}/build-analysis"
REPORT_DIR="${ROOT_DIR}/analysis-reports"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"
RUN_CPPCHECK=true
RUN_CLANG_TIDY=true
RUN_LIZARD=true
RUN_IWYU=true
TOOL_SELECTED=false
DEEP=false
STRICT=false
FIX_FILE=""
ANALYSIS_FAILURES=0
BUILD_READY=false

select_only() {
  if [[ "${TOOL_SELECTED}" == false ]]; then
    RUN_CPPCHECK=false
    RUN_CLANG_TIDY=false
    RUN_LIZARD=false
    RUN_IWYU=false
    TOOL_SELECTED=true
  fi

  case "$1" in
    cppcheck) RUN_CPPCHECK=true ;;
    clang-tidy) RUN_CLANG_TIDY=true ;;
    lizard) RUN_LIZARD=true ;;
    iwyu) RUN_IWYU=true ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cppcheck) select_only cppcheck ;;
    --clang-tidy) select_only clang-tidy ;;
    --lizard) select_only lizard ;;
    --iwyu) select_only iwyu ;;
    --deep) DEEP=true ;;
    --strict) STRICT=true ;;
    --fix-file)
      shift
      if [[ $# -eq 0 ]]; then
        echo "--fix-file requires a source-file path." >&2
        exit 2
      fi
      FIX_FILE="$1"
      select_only clang-tidy
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

mkdir -p "${REPORT_DIR}"
cd "${ROOT_DIR}"

write_skipped_report() {
  local report="$1"
  local message="$2"
  printf '%s\n' "${message}" > "${report}"
  echo "${message}"
}

configure_analysis_build() {
  if ! command -v cmake >/dev/null 2>&1; then
    write_skipped_report "${REPORT_DIR}/configuration.txt" "CMake was not found; compilation-database tools were skipped."
    ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
    return
  fi

  echo "==> Configuring analysis build"
  if cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_UNITY_BUILD=OFF \
    -DENABLE_NATIVE_OPTIMIZATIONS=OFF \
    -DSKIP_GIT=ON \
    -DDISABLE_STATS=1 \
    -DENABLE_ASAN=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_BENCHMARKING=OFF \
    > "${REPORT_DIR}/configuration.txt" 2>&1; then
    if [[ -f "${COMPILE_COMMANDS}" ]]; then
      BUILD_READY=true
      return
    fi
    echo "CMake completed without compile_commands.json." >> "${REPORT_DIR}/configuration.txt"
  fi

  echo "CMake configuration failed; inspect analysis-reports/configuration.txt." >&2
  ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
}

run_cppcheck() {
  local text_report="${REPORT_DIR}/cppcheck.txt"
  local xml_report="${REPORT_DIR}/cppcheck.xml"
  local -a common_args=(
    "--project=${COMPILE_COMMANDS}"
    --enable=warning,style,performance,portability
    --inline-suppr
    --suppress=missingIncludeSystem
    "--suppressions-list=${ROOT_DIR}/.cppcheck-suppressions.txt"
    "-i${ROOT_DIR}/build-analysis"
    "-i${ROOT_DIR}/build"
    "-i${ROOT_DIR}/.git"
    "-i${ROOT_DIR}/data"
    "-i${ROOT_DIR}/modules"
    "-i${ROOT_DIR}/screenshots"
    "-i${ROOT_DIR}/docs"
  )

  if [[ "${DEEP}" == true ]]; then
    common_args+=(--inconclusive)
  fi

  if ! command -v cppcheck >/dev/null 2>&1; then
    write_skipped_report "${text_report}" "Cppcheck is not installed; install it to generate this report."
    write_skipped_report "${xml_report}" "Cppcheck is not installed; XML report was not generated."
    return
  fi
  if [[ "${BUILD_READY}" != true ]]; then
    write_skipped_report "${text_report}" "Cppcheck was skipped because compile_commands.json is unavailable."
    write_skipped_report "${xml_report}" "Cppcheck was skipped because compile_commands.json is unavailable."
    return
  fi

  echo "==> Running Cppcheck"
  local status=0
  cppcheck "${common_args[@]}" \
    '--template={file}:{line}:{column}: {severity}: {message} [{id}]' \
    "--output-file=${text_report}" || status=$?
  cppcheck "${common_args[@]}" --xml --xml-version=2 "--output-file=${xml_report}" || status=$?
  if [[ ${status} -ne 0 ]]; then
    echo "Cppcheck returned ${status}; inspect its reports." >&2
    ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
  fi
}

resolve_fix_file() {
  local candidate="$1"
  local candidate_dir
  candidate_dir="$(cd "$(dirname "${candidate}")" 2>/dev/null && pwd)" || return 1
  candidate="${candidate_dir}/$(basename "${candidate}")"
  [[ -f "${candidate}" ]] || return 1

  case "${candidate}" in
    "${ROOT_DIR}"/src/*.cpp|"${ROOT_DIR}"/src/*.cc|"${ROOT_DIR}"/src/*.cxx)
      printf '%s\n' "${candidate}"
      ;;
    *)
      return 1
      ;;
  esac
}

run_clang_tidy() {
  local report="${REPORT_DIR}/clang-tidy.txt"
  if [[ "${BUILD_READY}" != true ]]; then
    write_skipped_report "${report}" "clang-tidy was skipped because compile_commands.json is unavailable."
    return
  fi

  if [[ -n "${FIX_FILE}" ]]; then
    local source_file
    if ! source_file="$(resolve_fix_file "${FIX_FILE}")"; then
      echo "--fix-file must name an existing .cpp, .cc, or .cxx file below src/." >&2
      ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
      return
    fi
    if ! command -v clang-tidy >/dev/null 2>&1; then
      write_skipped_report "${report}" "clang-tidy is not installed; the requested fix was not applied."
      ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
      return
    fi
    echo "==> Running clang-tidy fix for ${source_file#"${ROOT_DIR}"/}"
    if ! clang-tidy -p "${BUILD_DIR}" --fix "${source_file}" > "${report}" 2>&1; then
      ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
    fi
    return
  fi

  echo "==> Running clang-tidy"
  if command -v run-clang-tidy >/dev/null 2>&1; then
    if ! run-clang-tidy -p "${BUILD_DIR}" > "${report}" 2>&1; then
      ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
    fi
    return
  fi
  if ! command -v clang-tidy >/dev/null 2>&1; then
    write_skipped_report "${report}" "clang-tidy is not installed; install clang-tidy to generate this report."
    return
  fi

  : > "${report}"
  local source_file
  local status=0
  while IFS= read -r -d '' source_file; do
    clang-tidy -p "${BUILD_DIR}" "${source_file}" >> "${report}" 2>&1 || status=$?
  done < <(find "${ROOT_DIR}/src" -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0)
  if [[ ${status} -ne 0 ]]; then
    ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
  fi
}

run_lizard() {
  local report="${REPORT_DIR}/lizard.txt"
  local raw_report="${REPORT_DIR}/lizard-raw.txt"
  if ! command -v lizard >/dev/null 2>&1; then
    write_skipped_report "${report}" "Lizard is not installed; install it to generate this report."
    return
  fi

  echo "==> Running Lizard"
  local status=0
  lizard -V -C 20 -L 180 -a 8 "${ROOT_DIR}/src" > "${raw_report}" 2>&1 || status=$?
  {
    cat "${raw_report}"
    printf '\nTop 30 functions by cyclomatic complexity:\n'
    awk 'NF >= 6 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ { print }' "${raw_report}" \
      | sort -k2,2nr -k1,1nr \
      | head -n 30
  } > "${report}"
  rm -f "${raw_report}"

  if [[ ${status} -ne 0 && "${STRICT}" == true ]]; then
    echo "Lizard thresholds were exceeded in strict mode." >&2
    ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
  fi
}

run_iwyu() {
  local report="${REPORT_DIR}/iwyu.txt"
  if [[ "${BUILD_READY}" != true ]]; then
    write_skipped_report "${report}" "IWYU was skipped because compile_commands.json is unavailable."
    return
  fi
  if command -v iwyu_tool.py >/dev/null 2>&1; then
    echo "==> Running IWYU"
    if ! iwyu_tool.py -p "${BUILD_DIR}" > "${report}" 2>&1; then
      ANALYSIS_FAILURES=$((ANALYSIS_FAILURES + 1))
    fi
    return
  fi
  if command -v include-what-you-use >/dev/null 2>&1; then
    write_skipped_report "${report}" "include-what-you-use is installed, but iwyu_tool.py was not found to drive compile_commands.json. Install the IWYU tools package."
    return
  fi
  write_skipped_report "${report}" "IWYU is not installed; this optional report was skipped."
}

if [[ "${RUN_CPPCHECK}" == true || "${RUN_CLANG_TIDY}" == true || "${RUN_IWYU}" == true ]]; then
  configure_analysis_build
fi

[[ "${RUN_CPPCHECK}" == true ]] && run_cppcheck
[[ "${RUN_CLANG_TIDY}" == true ]] && run_clang_tidy
[[ "${RUN_LIZARD}" == true ]] && run_lizard
[[ "${RUN_IWYU}" == true ]] && run_iwyu

echo
echo "Static-analysis reports: ${REPORT_DIR}"
echo "Configuration: ${REPORT_DIR}/configuration.txt"
echo "Cppcheck: ${REPORT_DIR}/cppcheck.txt and cppcheck.xml"
echo "clang-tidy: ${REPORT_DIR}/clang-tidy.txt"
echo "Lizard: ${REPORT_DIR}/lizard.txt"
echo "IWYU: ${REPORT_DIR}/iwyu.txt"

if [[ ${ANALYSIS_FAILURES} -ne 0 ]]; then
  echo "Static analysis completed with ${ANALYSIS_FAILURES} tool/configuration failure(s)." >&2
  exit 1
fi
