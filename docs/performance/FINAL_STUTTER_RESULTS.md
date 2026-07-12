# Final stutter results

## Completed engineering checks

- Full Release server build: passed.
- C++23 warnings-as-errors build: passed.
- All 13 CTest targets: passed.
- Lua server and Astra scripts: syntax validation passed with Lua 5.5.
- Server startup smoke test: scripts, monsters, NPC backend, map and spawn initialization reached; no new script-load error was observed.
- Duplicate death scheduling: guarded and unit-tested.
- Lua timer identity: file/function/line/timer id/delay retained.
- Death stage p50/p95/p99: emitted by the existing five-second performance report.
- Lua/SQL/KV and analyzer packets: correlated with `deathId`.
- Analyzer packet/UI duplication: reduced without changing 8.60 field order.

## Required live acceptance run

Set `performanceMetricsEnabled = true` in `config.lua`, start the Release server, connect Astra and reproduce movement plus monster kills with all systems enabled. Stop with `Ctrl+C` after at least 60 seconds.

```bash
cd '/mnt/c/Users/Mateus/Desktop/forge/carpet system/forgottenserver-downgrade-1.8-8.60'
./build-release/tfs 2>&1 | tee "data/logs/stutter-final-$(date +%Y%m%d-%H%M%S).log"
```

Then extract correlated evidence:

```bash
LOG=$(ls -t data/logs/stutter-final-*.log | head -1)
grep -E '\[ServerDeathTrace\]|\[Perf\] (Death::|Game::checkCreatureWalk|Game::internalMoveCreature|Map::moveCreature|Map::getSpectators|reactor queue)' "$LOG"
grep -E '\[ClientDeathTrace\]' '/mnt/c/Users/Mateus/Desktop/forge/bazar/AstraClient/otclientv8.log'
tail -n 200 data/logs/stats/lua_slow.log
tail -n 200 data/logs/stats/sql_slow.log
tail -n 200 data/logs/stats/special.log
```

Lua syntax-only validation:

```bash
/usr/local/bin/lua -e "assert(loadfile('data/scripts/network/hunt_analyzer/huntanalyzer.lua')); assert(loadfile('data/scripts/network/party_analyzer/partytracker.lua')); assert(loadfile('data/scripts/network/boss_cooldown/bosscooldown.lua')); assert(loadfile('data/scripts/lib/boss_cooldown.lua'))"
```

No post-change p95/p99 is reported until this live scenario is executed. Old Callgrind files are empty and must not be used. If a fresh Callgrind capture is required, use the existing profiling scripts and verify the output is non-zero before opening it.
