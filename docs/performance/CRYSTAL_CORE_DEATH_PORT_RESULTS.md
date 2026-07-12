# Crystal core death port results

## Measured baseline

Source: OTS Statistics logs in `data/logs/stats`, 12 July 2026.

- `influenced_spawn.lua`: one 7,415 ms Lua call; same stall appeared as dispatcher `think()`.
- `custom_bestiary.lua`: 184 aggregate calls / 385 ms in `lua.log`; 18 slow calls / 104 ms and two very-slow calls totaling 86 ms, maximum 49 ms.
- `task_board/creature_events.lua`: nine slow calls totaling 59 ms before first fix.
- Bestiary SQL: per-kill inserts dominated kill-related SQL; maximum observed insert was 48 ms on database thread.
- Existing Callgrind output files were all zero bytes and cannot support performance claims.

## Phase-one retest

After incremental Bestiary cache update and removal of full Lua Forge scan:

- No new `influenced_spawn.lua` slow/very-slow record.
- No new `custom_bestiary.lua` slow/very-slow record.
- Bestiary normal callback: 30 calls / 27 ms aggregate.
- Remaining kill spike: `task_board/creature_events.lua` reached 16 ms.
- Matching synchronous `player_bounty_tasks` insert took 12 ms.

These numbers led directly to async Task Board persistence and C++ Bestiary/Bosstiary state.

## Implemented final changes

- Full Forge lifecycle moved from Lua global scan to C++ tracked pools/sets.
- Bestiary/Bosstiary counts moved to `Player` memory and transactional batched player save.
- Per-kill Bestiary INSERT and Bosstiary SELECT removed.
- Bounty, Weekly and Hunting Task periodic kill saves moved off dispatcher with `db.asyncQuery()`.
- Bestiary derived caches update one race instead of scanning all registered entries twice per kill.
- Callgrind capture now starts after scenario preparation, dumps, stops server, waits for Valgrind and rejects empty output.

## Verification completed

- Release `tfs` build: passed with GCC 13/C++23 and `-Werror`.
- Lua syntax: Forge, Bestiary, Bosstiary, Bounty, Weekly and Hunting Task scripts passed Lua 5.5 `loadfile`.
- Shell syntax: `capture_callgrind.sh` passed `bash -n`.
- Focused `test_storage_maps`: 3 tests passed, including Bestiary in-memory increment and overflow saturation.
- Runtime smoke test: server reached ONLINE, first scheduled Forge C++ cycle ran without an OTS slow entry/error, and SIGINT shutdown completed cleanly.

## Required runtime retest

No post-final runtime percentile is claimed yet. Run same kill scenario, then compare only newly appended OTS Stats entries. A valid Callgrind capture must have non-zero size.

```bash
# Run from the repository root.
./build-release/tfs
```

After killing same monster batch, stop with `Ctrl+C` and inspect:

```bash
tail -n 80 data/logs/stats/lua_slow.log
tail -n 80 data/logs/stats/dispatcher_slow.log
tail -n 80 data/logs/stats/sql_slow.log
```

Callgrind combat capture:

```bash
JOBS=2 bash tools/build-callgrind.sh
bash scripts/profiling/capture_callgrind.sh combat 60
FILE=$(find . -maxdepth 1 -name 'callgrind-combat.out.*' -size +0c -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)
callgrind_annotate --inclusive=yes --threshold=0.5 "$FILE" | less
kcachegrind "$FILE"
```
