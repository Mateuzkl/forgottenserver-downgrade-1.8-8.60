#!/usr/bin/env bash
# Run CTest against a private MariaDB instance. Usage: bash tools/test-disposable-mariadb.sh BUILD_DIR [ctest options]
set -euo pipefail
build=$(realpath "${1:?supply the CMake build directory}")
shift
root=$(cd "$(dirname "$0")/.." && pwd)
fixture=$(mktemp -d /tmp/tfs-test-db.XXXXXXXX)
socket="$fixture/mysql.sock"
pid=
cleanup() {
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    if [[ "$fixture" =~ ^/tmp/tfs-test-db\.[[:alnum:]]{8}$ ]]; then
        rm -rf -- "$fixture"
    fi
}
trap cleanup EXIT
mariadb-install-db --no-defaults --datadir="$fixture/data" --auth-root-authentication-method=normal >"$fixture/install.log" 2>&1 || { cat "$fixture/install.log"; exit 1; }
mariadbd --no-defaults --user="$(id -un)" --datadir="$fixture/data" --socket="$socket" --skip-networking \
    --pid-file="$fixture/server.pid" --log-error="$fixture/server.log" &
pid=$!
client=(mariadb --no-defaults --protocol=SOCKET --socket="$socket" --user=root)
for ((i=0; i<100; i++)); do
    if "${client[@]}" -e 'SELECT 1' >/dev/null 2>&1; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then cat "$fixture/server.log"; exit 1; fi
    sleep 0.1
done
export TFS_TEST_DB_HOST=localhost TFS_TEST_DB_USER=root TFS_TEST_DB_PASS='' TFS_TEST_DB_SOCKET="$socket"
export TFS_TEST_DB_NAME=tfs_market_test_local
"${client[@]}" -e 'CREATE DATABASE tfs_market_test_local'
"${client[@]}" "$TFS_TEST_DB_NAME" < "$root/schema.sql"
ctest --test-dir "$build" --output-on-failure --no-tests=error "$@"
