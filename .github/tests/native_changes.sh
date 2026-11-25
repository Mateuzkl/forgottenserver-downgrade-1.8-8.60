#!/usr/bin/env bash
set -euo pipefail
script=$(cd "$(dirname "$0")/../scripts" && pwd)/native-changes.sh
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
cd "$fixture"
git init -q -b main
git config user.name 'CI fixture'
git config user.email 'ci-fixture@example.invalid'
git config commit.gpgsign false

commit_file() {
  mkdir -p "$(dirname "$1")"
  printf '%s\n' "$2" > "$1"
  git add -- "$1"
  git commit -qm fixture
}
check() {
  local expected=$1 event=$2 base=$3 head=$4
  : > output
  EVENT_NAME=$event BASE_SHA=$base HEAD_SHA=$head DEFAULT_BRANCH=main GITHUB_OUTPUT="$PWD/output" bash "$script" > /dev/null
  grep -qx "build=$expected" output || { echo "Unexpected build decision: $event $base..$head"; exit 1; }
}

commit_file notes.txt base
base=$(git rev-parse HEAD)
git update-ref refs/remotes/origin/main "$base"
commit_file mods/tracker.lua 'return true'
lua=$(git rev-parse HEAD)
check false pull_request "$base" "$lua"
check false push "$base" "$lua"
check false push 0000000000000000000000000000000000000000 "$lua"
check true workflow_dispatch "$base" "$lua"
check true push unavailable "$lua"
commit_file .github/workflows/example.yml 'name: fixture'
workflow=$(git rev-parse HEAD)
check false pull_request "$base" "$workflow"
commit_file 'src/path with spaces.cpp' '// native'
native=$(git rev-parse HEAD)
check true pull_request "$base" "$native"
commit_file mods/tracker.lua 'return false'
mixed=$(git rev-parse HEAD)
check true pull_request "$base" "$mixed"
check false push "$native" "$mixed"
git mv -- 'src/path with spaces.cpp' native-backup.txt
git commit -qm rename
check true push "$mixed" "$(git rev-parse HEAD)"
before=$(git rev-parse HEAD)
git rm -q -- native-backup.txt
git commit -qm delete
check false push "$before" "$(git rev-parse HEAD)"
before=$(git rev-parse HEAD)
commit_file CMakeLists.txt '# build input'
check true push "$before" "$(git rev-parse HEAD)"
before=$(git rev-parse HEAD)
commit_file vcpkg.json '{}'
check true push "$before" "$(git rev-parse HEAD)"
echo 'PASS: Lua-only, mixed PR, push ranges, native renames, build inputs, manual runs and missing history'
