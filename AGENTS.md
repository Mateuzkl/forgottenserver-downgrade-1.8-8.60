# AGENTS.md — TFS 1.8 (8.60 Downgrade) Build, Test & AI Development Policy

This file defines mandatory rules for all AI assistants and automated tooling operating on this repository in Ubuntu 22.04/24.04 and WSL2.

## 1. Golden Rules for Builds & Resource Safety (Ubuntu / WSL2)

- **NEVER saturate the host CPU or RAM.** Do NOT run raw `cmake --build ... -j$(nproc)`, `make -j$(nproc)`, or unbounded parallel links/LTO on WSL2 or developer workstations.
- **ALWAYS prefer `./build.sh`** (with `--non-interactive --skip-deps` when dependencies are already installed) or the documented `linux-*` presets in `CMakePresets.json`.
- **Keep Windows and the IDE responsive on WSL2:**
  - `./build.sh` automatically detects WSL2, reserves CPU threads and RAM headroom for Windows + IDE, lowers process priority (`nice` + `ionice`), caps Ninja `compile_pool` / `link_pool` and GCC/Clang LTO workers (`TFS_LTO_JOBS`), and stores build artifacts + `ccache` on native Linux ext4 (`~/.cache/tfs-build/` and `~/.cache/ccache/`) when the repository resides on `/mnt/*` (NTFS/9P).
  - Never create heavy build directories under `/mnt/c/...` when working from WSL2 unless explicitly requested by the user.
- **Preserve maximum production runtime performance:**
  - Production engine binaries MUST keep `-O3 -fomit-frame-pointer -DNDEBUG`, LTO (`ENABLE_LTO=ON`), and `USE_MIMALLOC=ON` (and `-march=native -mtune=native` unless `--portable` is requested).
  - Use `--profile full-performance` for production binaries; use `--profile balanced` or `--profile low-memory` for incremental development, debugging, sanitizers, and unit tests.
- **Always use incremental compilation:**
  - Do NOT pass `--clean` unless switching incompatible options/compilers or investigating a cache bug.
  - When iterating on a single test or subsystem, pass `--target <target>` (e.g. `--target test_combat_hotpath` or `--target tfs`) instead of rebuilding all 70+ test binaries.

## 2. Build Profiles

| Profile | Command | LTO | Unity Build | PCH | Linker / Pools | CPU & RAM Policy (WSL2) | Best For |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Low Memory** | `./build.sh --non-interactive --skip-deps --profile low-memory` | `OFF` | `OFF` *(override: `--unity on`)* | `ON` | `compile<=2`, `link=1`, GNU ld low-mem flags, `nice -n 15`, `ionice -c 3` | Reserves >=75% CPU & >=2 GiB RAM | <=8 GB RAM, heavy IDE load, sanitizers |
| **Balanced** *(default on WSL2)* | `./build.sh --non-interactive --skip-deps --profile balanced` | `OFF` | `ON` (batch 8; *override: `--unity off`*) | `OFF` *(auto `ON` if `--unity off`)* | Adaptive `compile` (<=50% CPU on WSL2), `link=1`, `ctest<=2`, `nice -n 10` | Reserves >=40-50% CPU & >=2 GiB RAM | Daily dev, fast rebuilds, unit tests |
| **Full Performance** *(production)* | `./build.sh --non-interactive --skip-deps --profile full-performance` | `ON` | `ON` (batch 8) | `OFF` | `-O3`, `-march=native`, `USE_MIMALLOC=ON`, bounded `TFS_LTO_JOBS`, `link<=2` | Still caps RAM (~1.45 GiB/job) & reserves >=25% CPU on WSL2 | Production `tfs` binary & release verification |

To inspect the exact adaptive plan (detected CPUs, RAM, WSL2 status, build dir, job limits, and flags) without building:

```bash
./build.sh --non-interactive --skip-deps --print-plan --profile balanced
```

## 3. Standard Workflows for AI Agents

### Incremental Engine Build (WSL2 / Dev)
```bash
./build.sh --non-interactive --skip-deps --profile balanced --target tfs
```

### Fast Targeted Unit Test Iteration
```bash
./build.sh --non-interactive --skip-deps --profile balanced --tests --target test_combat_hotpath --test-filter '^test_combat_hotpath$'
```

### Full Unit Test Suite (CTest)
```bash
# Without DB (DB tests cleanly skipped with code 77):
./build.sh --non-interactive --skip-deps --profile balanced --tests

# Full suite including MariaDB integration tests (test_account_manager_db, test_market_delivery_db):
./build.sh --non-interactive --skip-deps --profile balanced --db-tests
```

### Sanitizers (ASan + UBSan)
Sanitizers automatically disable LTO and `USE_MIMALLOC`, default to `ENABLE_UNITY_BUILD=OFF` (manually overrideable via `--unity on`), and cap parallelism to avoid memory exhaustion:
```bash
./build.sh --non-interactive --skip-deps --profile low-memory --sanitizer asan-ubsan --tests --test-filter 'test_party_lifetime|test_combat'
```

### Benchmarks (Google Benchmark)
```bash
./bench.sh                                     # Adaptive balanced build + all benchmarks
./bench.sh --filter bench_networkmessage       # Single benchmark
./bench.sh --profile low-memory                # Low-RAM benchmark run
```

### Production Binary Build (`-O3` + LTO + mimalloc)
```bash
./build.sh --non-interactive --skip-deps --profile full-performance
# Portable binary (no -march=native, e.g. for deploying to a different CPU):
./build.sh --non-interactive --skip-deps --profile full-performance --portable
```

## 4. Direct CMake / Preset Rules (When Not Using `build.sh`)

If invoking `cmake` directly, AI agents MUST follow these rules:
1. Always use `-G Ninja` (when `ninja` is available).
2. Keep `ccache` enabled (`-DENABLE_CCACHE=ON`, default) with:
   ```bash
   export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
   export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-5G}"
   export CCACHE_COMPRESS=1
   export CCACHE_SLOPPINESS="pch_defines,time_macros,include_file_mtime,include_file_ctime"
   export CCACHE_NOHASHDIR=1
   unset CCACHE_BASEDIR # Keep __FILE__ absolute for unit tests locating data/ from __FILE__
   ```
3. Always pass Lua 5.5 and local prefix paths on Ubuntu/WSL2:
   ```bash
   -DCMAKE_PREFIX_PATH="/usr/local;$HOME/.local" \
   -DLUA_INCLUDE_DIR=/usr/local/include \
   -DLUA_LIBRARY=/usr/local/lib/liblua.a \
   -DLUA_LIBRARIES="/usr/local/lib/liblua.a;m;dl"
   ```
4. Always cap Ninja pools and LTO workers on WSL2 (e.g., `-DTFS_COMPILE_JOBS=4 -DTFS_LINK_JOBS=1 -DTFS_LTO_JOBS=4`) and run under `nice -n 10`.
5. Use `-DENABLE_LTO=OFF` with Unity Build (`-DENABLE_UNITY_BUILD=ON`, default) for fast dev/test builds, or `-DENABLE_UNITY_BUILD=OFF` for `low-memory`, sanitizers, and single-TU debugging; keep `-DENABLE_LTO=ON -DCMAKE_BUILD_TYPE=Release` for production verification.
6. Run timing-sensitive CTest suites with bounded concurrency (`ctest -j 2` or `-j 1` under sanitizers).
7. Prefer `CMakePresets.json` profiles (`linux-low-memory`, `linux-balanced`, `linux-full-performance`, `linux-asan-ubsan`, `linux-benchmarks`).

## 5. Optional Host WSL2 `.wslconfig` Recommendation

On 16 GB Windows hosts running an IDE alongside WSL2, a `~/.wslconfig` on Windows (`%USERPROFILE%\.wslconfig`) further guarantees host stability:
```ini
[wsl2]
memory=8GB
processors=6
swap=4GB
```

## 6. Repository Invariants to Preserve

- **Lua 5.5 is required** (patched 5.5.0 via `tools/patches/lua-5.5.0-write-barrier.patch`); LuaJIT is disabled by default.
- **Source list parity:** Any `.cpp`/`.h` added to or removed from `src/CMakeLists.txt` must be kept in sync with `vc18/theforgottenserver.vcxproj` (verified by `check_source_list_parity`).
- **Memory safety:** No manual `delete` (`check_manual_delete`) and no unsafe `.get()` lifetime escapes (`check_shared_get_escapes`).
- **Protocol & Lua checks:** Keep `check_protocolgame_handshake`, `check_shop_podium_protocol`, `tools/test_step_tile_callbacks.lua`, `src/tests/test_sound_port.lua`, and `.github/tests/*.lua` green.
