#!/usr/bin/env bash
# Runs the standalone (engine-free) Lua tests with a plain Lua 5.5 interpreter.
# Usage: bash tests/lua/run.sh [path-to-lua]
set -u

cd "$(dirname "$0")/../.."

LUA_BIN="${1:-${LUA_BIN:-lua}}"
if ! command -v "$LUA_BIN" >/dev/null 2>&1; then
	if [ -x /usr/local/bin/lua ]; then
		LUA_BIN=/usr/local/bin/lua
	else
		echo "error: no lua interpreter found (set LUA_BIN or pass one as argument)" >&2
		exit 2
	fi
fi

status=0
for test in tests/lua/test_*.lua; do
	if ! "$LUA_BIN" "$test"; then
		status=1
	fi
done
exit $status
