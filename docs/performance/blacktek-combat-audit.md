# BlackTek combat reference audit — TFS 1.8 / 8.60

Date: 2026-10-15. Local baseline: `7bc92ba6cc0c026f2089a162909b904265f7c9d7`,
branch `perf/tfs18-1000-stressbots`; merge base with `origin/main`:
`a5068b48918254cbf2e13c5aec19cc691f2d2cb8`.
This follows, rather than replaces, the [400-bot audit](400bots/FINDINGS.md).
The human authorized studying BlackTek, then explicitly authorized applying and
validating a minimal adaptation. No new deployment or production load was run.

## Reference identity and provenance

Study-only shallow/sparse clone outside this repository:
`C:/Users/Mateus/AppData/Local/Temp/codex-blacktek-reference-20261015`.
Remote: <https://github.com/Black-Tek/BlackTek-Server>; branch `master`;
inspected SHA: `828b89395f28852fe2476655d30f1b90507c6066`.
No BlackTek setup, bootstrap, build, install or database script was executed.

Its README describes TFS 1.4.2 ancestry and protocol 10.98. Its
`licensing/Readme.md` distinguishes the original GPL codebase from MIT-licensed
contributions; `licensing/License` contains an MIT notice, whereas inspected
source headers still refer to GPL-2.0. This is recorded as provenance, not a
legal conclusion about every file. No BlackTek source was copied/cherry-picked:
the patch independently changes existing TFS routing and retains TFS notices.

## Fresh capture: observed bottleneck region, not exclusive CPU

Input: `data/logs/server_20261015_164644.log` (905,216 bytes), SHA-256:
`E7DD2B9225760F0474EC2CEBA07307D659EBD174687FEDBF9EE115222A26397E`.
The existing read-only parser finds 119 complete windows and excludes one
incomplete window. All durations below are **inclusive wall time**; nesting
overlaps, so rows must not be summed or reported as independent CPU shares.

| Metric | Calls | Total ms | Largest call ms |
| --- | ---: | ---: | ---: |
| Game::checkCreatures | 4,047 | 163,074.123 | 1,706.196 |
| Combat::doAreaCombat | 18,814 | 82,321.857 | 608.411 |
| Combat::area.applyTargets | 18,814 | 60,153.364 | 594.898 |
| Creature::executeConditions | 131,606 | 69,168.849 | 16.065 |
| Combat::area.processTiles+collectTargets | 18,814 | 21,910.411 | 79.057 |
| Map::getSpectators | 614,782 | 2,931.253 | 0.734 |
| Map::getPathMatching | 14,046 | 119.779 | 0.467 |

The detailed worst AoE sample is a native Stress Demon combat spell, 21 active
tiles and 268 targets: 608,403.5 us inside its phase timer, including 594,897.6 us
in target application and 13,471.7 us processing tiles/collecting targets.
The outer metric includes a little additional instrumentation time. At
16:54:24.755, the creature-check maximum is 1,706,195.6 us, queue waiting reaches
1,717,762.9 us and reactor backlog reaches 502. Waiting is not CPU execution.
The OTS dispatcher very-slow file independently names `checkCreatures`.

The accompanying OTS files also record a 60 ms Lua prey timer, a 126 ms KV-store
SQL write and a 181 ms full save. These are additional possible stalls, not
evidence that every combat stall is SQL/Lua. Their aggregate date-only windows
must not be equated to an exact timestamped Perf event. One dispatcher window
reports 415 online players, further illustrating why a connection peak cannot
establish the claimed controlled stress population.

This capture contains none of the newer `Game::combatChangeHealth`,
`ConditionDamage::doDamage`, `Creature::addCondition` or `monster_ai` diagnostics
already present in HEAD. Therefore it cannot identify exclusive cost within
target application or prove that the newest instrumentation was deployed.
The local Windows executable is 5,030,912 bytes, timestamp 12:24:05, SHA-256
`F3E8C5800A417A8F2667A6D91C80E7786C78B60D135C1FB3EE7E4F73363238A5`;
the log does not bind the running binary to a Git SHA. Connection peaks are
not a verified bot population. There is no controlled before/after stress run.

## Mechanism comparison

Line references refer to the exact BlackTek SHA above and the local audited
source. Evidence means source/capture evidence, not a server-capacity result.

| Mechanism | BlackTek file:line | Local TFS file:line | Contract difference | Opportunity | Risk / evidence | Decision |
| --- | --- | --- | --- | --- | --- | --- |
| Serialize an effect once | game.cpp:6417; protocolgame.cpp:2868 | game.cpp:7901; protocolgame.cpp:4297 | BlackTek uses an 8-bit effect; this TFS uses 16 bits and requires ground | Reuse identical bytes per synchronous event | TFS capture has substantial fan-out; direct copying would truncate IDs | Adapt locally, preserve checks and 16-bit wire format |
| Recipient routing | player.h:817; game.cpp:6432 | protocolspectator.h:174,405; player.h:1375 | Local generic output forwards only to owner; effect route also reaches cast and spies | Carry event-local preparation through the existing effect route | Calling generic output instead loses cast/spies; bypassing per-protocol checks changes visibility | Never replace the route with generic output |
| Shared spectator view | combat.cpp:3558 | combat.cpp:1618; game.cpp:7850 | Local target-specific queries filter instances and may follow world-changing callbacks | Borrow strong snapshots only while valid | An area-wide recipient set need not equal a target's current observers | Defer cross-target reuse; no persistent cache |
| Chunk/mask target collection | combat.cpp:3475; chunk.h | combat.cpp:1630; map.cpp:164 | BlackTek maintains chunk creature slots/coordinates; TFS uses its existing quadtree/tile ownership | Potential separate spatial-index study | Local tile building totals 178 ms versus 60,153 ms target application; index maintenance and target order differ | Do not transplant a new world index for this patch |
| Batch mitigation / SIMD | combat.cpp:3570,4287; simd_kernels.h | combat.cpp:1673; game.cpp:7113 | BlackTek has different formulas, modifier hooks, TLS scratch/context and batch state | Numerical kernels only after equivalent contracts are established | Local RNG, shields, events, reflected/nested damage and mutation order must remain exact | No batch/SIMD transplant |
| Condition iteration | creature.cpp:1398; condition.h:67 | creature.cpp:1663; creature.h:29 | BlackTek uses list/intrusive handles; local conditions already use vector/shared ownership | Profile actual condition damage, merge and output work | Both snapshot owners for mutation; deleting the local snapshot risks callback invalidation | Preserve ownership, clone independence and iteration semantics |
| Output lifetime | protocolgame.cpp:577 | protocolgame.cpp:1361; outputmessage.h:37; connection.cpp:332 | Existing TFS append copies immediately; async writes retain owned OutputMessages | Borrow one event packet synchronously | Packet/span must not escape; pending write ownership/backpressure are already guarded | Reuse existing copy/enqueue path; no new async state |

## Trace and ownership boundaries

`doAreaCombat` builds/validates tiles, fetches a strong spectator snapshot,
applies tile effects and collects strong creature owners, then processes targets
in their existing order. Each target receives its own damage copy, blocking,
health/mana processing, independently cloned conditions and target callback.
Condition execution can itself cause damage and invoke scripts. A broad raw
pointer cache or precomputed batch across those callbacks changes the contract.
In particular, `getCombatArea` returns raw tile borrows and
`combatTileEffects` invokes a tile callback before later tile accesses. This
audit does not certify arbitrary map-removing scripts at that boundary; a
dedicated reentrant removal reproducer and ownership/revalidation audit remain
separate work. The supplied worst AoE sample has no tile/target callbacks.

For the selected optimization, `Game::addMagicEffect` and the instance-specific
helper each own a stack-local `MagicEffectPacket` (optional eight-byte array) for
**one** position and effect ID. The existing `Player -> ProtocolSpectator -> ProtocolGame` route
borrows it by reference synchronously. Owner weak-lock, cast/spies traversal,
per-protocol position/spy viewport/floor checks and the ground check remain in
place and in order. The first eligible protocol prepares the payload; later
eligible protocols copy it into their independent output messages.
Framing, checksums, encryption keys and pending output ownership remain separate
per connection; only the unencrypted eight-byte event payload is reused.

There are no Lua callbacks or newly scheduled jobs in that synchronous packet
preparation/copy path. The optional is never retained by a protocol or async
operation and is not reused for another event. `Connection` still owns pending
OutputMessages, applies the existing queue limit and retains the in-flight write
buffer until completion. No shared world object was weakened to a raw owner,
no no-op deleter was added and no cache was extended across a cast/tick.

## Scope and rejected changes

The minimal production patch only reuses serialization of magic-effect events,
including the instance helper actually used by AoE impacts/block effects. It
does not remove recipients, ground/visibility checks, casts, spells, targets,
conditions, cooldowns or updates. It does not change health/text/speed packet
formats or combat formulas. The OTS Statistics implementation is reused.

The previous primitive-direct-output experiment remains rejected (see the prior
audit); this is a different hypothesis and is measured on the real in-memory
routing path, not inferred from the size of NetworkMessage's array. No
thread pool, off-thread map access, reactor-budget increase, global smart-pointer
conversion, condition pooling or speculative dead-code cleanup is included.

See [validation and limits](blacktek-combat-validation.md) for reproducible
tests, benchmark results and remaining live-load gates. This patch cannot by
itself establish that the 600-bot stall is fixed, much less guarantee 1,000 bots.
