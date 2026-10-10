#!/usr/bin/env bash
set -uo pipefail
name="$1"
shift
mkdir -p "${DIAGNOSTICS_DIR:-build/diagnostics}"
start=$SECONDS
"$@" 2>&1 | tee "${DIAGNOSTICS_DIR:-build/diagnostics}/$name.log"
code=${PIPESTATUS[0]}
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  printf '| %s | %s | %s |\n' "$name" "$code" "$((SECONDS-start))" >> "$GITHUB_STEP_SUMMARY"
fi
exit "$code"
