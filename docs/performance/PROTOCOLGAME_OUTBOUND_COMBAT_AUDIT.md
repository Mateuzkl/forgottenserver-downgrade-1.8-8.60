# ProtocolGame outbound and combat performance audit

Audit date: 2026-09-20

## Scope and exact base

- Base SHA: `d4f0f4e4dc3bde9f0ec94b66906b0493b4726b2a`
- Branch: `perf/protocolgame-outbound-combat-hotpath`
- PR #303 status during the audit: open and unmerged (`57e1fdcbcb880dad35696bc0b29253565cc1289c`); no PR #303 changes were stacked here.
- Repository: `Mateuzkl/forgottenserver-downgrade-1.8-8.60`
- StressBot: branch `tfs`, SHA `542df8d4b2185741419d35b2cd667f2c94f0311d`

This branch adds measurement only. It deliberately does not change combat timing, creature scheduling, packet contents, output batching delay, encryption, or connection ownership.

## Test machine and build

- CPU: Intel Core i5-10300H, 4 cores / 8 logical CPUs
- Environment: WSL2, Linux `6.18.33.2-microsoft-standard-WSL2`
- Compiler: GCC 13.3.0
- Build: Release, C++23
- Flags: `-O3 -fomit-frame-pointer -DNDEBUG -march=native -mtune=native -flto=auto -fno-fat-lto-objects`
- LTO: enabled (`-flto=auto`)
- Sanitizers: disabled for the measured build
- Allocator: project lock-free/free-list allocator; mimalloc disabled
- `networkThreads`: 4
- XTEA/checksum: enabled by the 8.60 protocol
- Map: local `test.otbm`
- Dense spawn fixture: 1,190 monster entries in `Untitled-1-spawn.xml`
- Performance report interval: 5 seconds
- Linux `perf`: unavailable for this WSL kernel, so no call graph or cycle attribution is claimed. Thread CPU was measured with `pidstat`; internal phase timers provide function-level evidence.

## StressBot configuration

`scripts/test123.json` was used with CLI bot-count and duration overrides:

- account prefix `teste123`, account width 3
- login pacing 650 ms, burst size 1
- `aiEnabled=true`, behavior `mixed`
- random walk 1,500 ms
- attack scan 2,000 ms
- `exori` 4,000 ms; `exori gran` and `exori hur` 6,000 ms
- healing enabled
- chat enabled
- reconnect enabled
- XTEA and Adler32 enabled

The unified `BrainLoopAsync` path was used; there is no evidence of duplicate legacy behavior loops.

## Baseline results

### Dense 100, uninstrumented outbound path

- Duration: 90 seconds
- Whole-process CPU average: 62.4% of one logical CPU
- Game/main thread CPU average: 49.4%
- Network workers: approximately 3.2%, 3.2%, 3.2%, and 3.4% (about 13% combined)
- `Game::checkCreatures`: commonly 10–17 ms average per call; maximum observed about 95.5 ms
- Reactor queue: commonly 170–260; maximum backlog about 303–340
- Area-combat recipients: about 1.14M–1.24M per 5-second window (logical recipients, not packets)

### Dense 300 configured, uninstrumented outbound path

- Duration: 300 seconds
- Highest observed connected/in-world range: approximately 264 before deaths and reconnect behavior reduced it
- Area-combat recipient peak: approximately 2.9M per 5-second window
- Representative 264-player window: approximately 2.28M recipients, `CombatSpell::castSpell` 3.29 s cumulative, `Combat::doAreaCombat` 1.70 s cumulative
- Reactor backlog reached approximately 2,257 in a heavy window
- `Game::checkCreatures` remained the repeatedly reported slowest callback, with observed maxima above 100 ms

### Dense 300 configured, outbound-instrumented

- Duration: 260 seconds
- Highest sustained active connection count in the selected peak window: 218
- Disconnects: 163; TCP connection failures: 0
- The run did not sustain 300 simultaneous living players because the combat fixture kills bots. It is therefore labelled “300 configured / 218 active peak,” not a completed steady-state 300-player result.

Selected 5-second peak window at 218 active connections:

| Metric | 5-second count | Rate |
|---|---:|---:|
| Logical ProtocolGame appends | 1,197,815 | 239,563/s |
| Logical bytes | 16,617,215 | 3.32 MB/s |
| Output buffers / async writes | 18,007 | 3,601/s |
| Bytes before XTEA/header | 16,502,055 | 3.30 MB/s |
| Bytes on wire | 16,706,842 | 3.34 MB/s |
| Area-combat recipients | 1,593,151 | 318,630/s |

Batching and pool behavior in that window:

- 66.5 logical appends per output buffer on average
- 916 bytes per pre-XTEA output buffer on average
- wire/header/padding expansion: 204,787 bytes, about 1.24%
- pending write queue maximum: 1
- output-message pool: 16 fresh allocations, 17,991 reuses (99.91% reuse)

The output connection was keeping up. The overload was before the socket queue, on the dispatcher/game thread.

## Top outbound generators

Selected peak window:

| Category | Calls/s | Bytes/s | Share of logical bytes |
|---|---:|---:|---:|
| Magic effects | 161,932 | 1,295,458 | 39.0% |
| Other/uncategorized | 22,887 | 1,428,023 | 43.0% |
| Creature health | 21,013 | 126,077 | 3.8% |
| Animated text/effects | 13,705 | 143,396 | 4.3% |
| Creature movement | 4,351 | 57,669 | 1.7% |
| Tile update | 3,477 | 48,309 | 1.5% |
| Distance effects | 2,892 | 37,593 | 1.1% |
| Add creature | 1,495 | 23,136 | 0.7% |

The “other” bucket remains large because custom/Astra opcodes and compound packets are intentionally not decoded with a hot-path map or string lookup. Expanding this bucket should be a measurement-only follow-up using additional compile-time opcode cases.

Calls at `ProtocolGame::writeToOutputBuffer` are per-recipient logical appends. They are not TCP packets. Multiple appends are coalesced into one output buffer and one async write.

## Hotspots in the same peak window

1. `Game::checkCreatures`: 2.220 s cumulative across 28 calls; 79.3 ms average; 106.9 ms maximum. Histogram p95/p99 are reported as the 134.2 ms power-of-two bucket upper bound.
2. `CombatSpell::castSpell`: 3.129 s cumulative across 6,974 calls; 448.7 us average.
3. `Monster::doAttacking`: 1.990 s cumulative across 6,893 calls; 288.7 us average.
4. `Combat::doAreaCombat`: 1.639 s cumulative across 2,639 calls; 620.9 us average.
5. `Combat::area.processTiles+collectTargets` and `applyTargets`: dominant area-combat phases; the slowest sampled cast spent 2.781 ms and 2.480 ms in those phases respectively.

Control metrics were materially smaller:

- `Map::getSpectators`: 168.6 ms cumulative across 45,393 calls, 3.7 us average
- `Map::moveCreature`: 662.1 ms cumulative, 173.0 us average
- `Game::internalMoveCreature`: 673.7 ms cumulative, 128.0 us average

This confirms that ordinary walking and the spectator lookup itself are not the primary bottleneck in this workload.

## Reactor and checkCreatures

In the selected peak window:

- queue latency average: 24.9 ms
- queue latency p50: 16.8 ms
- queue latency p95/p99: 134.2 ms histogram upper bound
- queue latency maximum: 107.4 ms
- current queue: 2,140
- maximum backlog: 2,171
- deferred tasks: 440

`checkCreatures` iterates a complete bucket and synchronously runs `onThink`, `onAttacking`, and `executeConditions` for every live entry. Buckets are already balanced by entry count when creatures are added, but cost is heterogeneous: an attacking monster can trigger area combat and hundreds of thousands of per-viewer effect appends. There is no time budget or persistent cursor, so one callback can monopolize the dispatcher.

No scheduler slicing was implemented in this branch. Slicing is a separate gameplay-sensitive change because naive deferral can alter attack, movement, and condition timing.

## Outbound path and ownership

Observed call graph:

```text
Game/combat event
  -> Player::send*
  -> ProtocolGame::send* builds a NetworkMessage for one viewer
  -> ProtocolGame::writeToOutputBuffer
  -> Protocol::getOutputBuffer
  -> OutputMessagePool allocation/reuse + autosend registration
  -> OutputMessagePool::sendAll after the existing 10 ms autosend delay
  -> Protocol::send
  -> Connection::send queues an OutputMessage_ptr
  -> Connection::internalSend on the connection strand
  -> Protocol::onSendMessage
       -> inner length
       -> XTEA padding/encryption
       -> Adler32/crypto header and outer length
  -> asio::async_write
  -> Connection::onWriteOperation
```

Ownership remains:

- `Protocol` holds a weak `Connection` reference.
- The connection queue stores `OutputMessage_ptr`.
- `internalSend` intentionally takes the shared pointer by value.
- The async completion captures both `shared_from_this()` and `msg`, keeping the connection and inline message buffer alive until Asio completes.
- Queue timestamps are stored in a parallel connection-owned deque, not in `OutputMessage`, so sharing an output message cannot introduce a timestamp data race.
- Error and completion paths pop/clear the message and timestamp queues together.

No raw pointer replaced any shared ownership edge.

## Fan-out correctness audit

- Magic and distance effects are sent by iterating visible players and invoking each player’s protocol. The payload is often identical after instance/visibility filtering, but it is currently constructed/appended per recipient.
- Add/remove/move creature packets are viewer-dependent because of visibility, known-creature state, stack position, floor/map slices, and client state.
- Map descriptions and floor changes are position- and viewer-dependent.
- Player stats, skills, and inventory are player-specific.
- Creature outfit, speed, and health may be shareable only for a proven homogeneous viewer set; current call sites and client capability/visibility rules must be retained.
- Text/chat can be recipient- or channel-dependent.

The audit does not propose sharing a mutable `OutputMessage` across connections. If identical effect payload reuse is prototyped later, the safe unit is an immutable serialized payload or `NetworkMessage` body appended into each viewer’s own output buffer after all viewer filters pass.

## Network, XTEA, checksum, and write queue

In the selected peak window:

- `Protocol::onSendMessage` (length + XTEA + checksum/header) consumed 62.9 ms total for 18,007 buffers, 3.49 us average
- outbound queue latency averaged 403.9 us; p95 was within the 2.10 ms histogram bucket
- async write completion latency averaged 452.8 us; p95 was within the 2.10 ms bucket
- pending write queue maximum was 1
- all 18,007 writes started and completed in the reporting window

At a representative active-load sample, the game thread used about 67% of one logical CPU while the four network workers used roughly 2.7–3.0% each. Earlier 100-player baseline showed the same shape: game thread dominant, network workers only a few percent each.

XTEA, Adler32, and header construction are combined in the measured `onSendMessage` scope, so separate XTEA/Adler cycle shares are not claimed. Their combined cost is already too small to justify SIMD or checksum work in this branch. Existing XTEA known-vector tests pass.

## Incoming load and login

- Per-connection packet backlog summaries commonly peaked at 1–2 with zero rejections.
- No evidence shows incoming StressBot packets saturating the per-connection admission queue.
- Login was paced at 650 ms and overlapped the ramp. Initial state/map description traffic is visible, but a separate login-only 100/300/1000 matrix was not completed.
- Database worker threads were effectively idle in the steady samples.

Login cost must not be inferred from the dense steady-state window. A dedicated login-only benchmark remains required before optimizing RSA, authentication, DB load, or initial map serialization.

## Changes implemented

- Added 5-second aggregate counters for logical ProtocolGame appends and bytes.
- Added output buffer flush/queue counters and pre-XTEA versus wire bytes.
- Added async write start/completion counts, pending depth, queue latency, and completion latency.
- Added compile-time outbound opcode categories without hot-path maps or strings.
- Added tagged allocator accounting for fresh output-message allocations versus pool reuse.
- Kept the highest-frequency logical/category counters dispatcher-local and non-atomic; cross-thread counters remain relaxed atomics.
- Added regression coverage for message/timestamp queue alignment and allocator fresh/reuse accounting.

## A/B status

No throughput optimization was implemented, so there is no “faster” claim and no optimization A/B. Baseline and instrumented runs had different surviving active-player counts and must not be treated as a performance comparison. This branch supplies the measurements needed for later isolated A/B work.

## Validation

- Release server target builds successfully.
- `test_connection_limits`: pass
- `test_connection_write_lifetime`: pass
- `test_outputmessage_pool_metrics`: pass
- `test_xtea`: pass
- Final 10-bot smoke: 25 seconds, 10/10 in world, zero disconnects, zero TCP failures, metrics emitted with queue max 1 and matching write start/completion counts.

ASan, UBSan, and TSan were not run. The measured Release binary was not sanitizer-instrumented.

## Remaining limitations and risks

- Mandatory same-count spread versus dense comparison is incomplete. The local fixture places test accounts into the dense combat area; changing persistent DB positions was intentionally not done as part of this audit.
- A stable 300-active-player window was not achieved because bots died/reconnected under the heavy combat fixture.
- Linux `perf` was unavailable for the installed WSL kernel, so no hardware-sampled game-thread or whole-process call graph is included.
- “Other” outbound bytes need further compile-time category expansion.
- The report does not contain a standalone XTEA or Adler microbenchmark because combined send-finalization cost was not significant.
- Login-only 100/300/1000, `networkThreads` 1/2/4/8, 500 spread, and sanitizer matrices remain outstanding.

## Ranked next PR candidates

1. Add a gameplay-preserving, persistent-cursor experiment for `checkCreatures`, with attack/condition timing tests and a dense A/B benchmark.
2. Reduce verified duplicate magic-effect packet construction for homogeneous recipients while keeping per-viewer output buffers and visibility checks.
3. Expand the compile-time “other” opcode classification and add incoming opcode/task-class metrics.
4. Add a reproducible spread-position fixture and a dedicated login-only fixture.
5. Consider XTEA SIMD, Adler32 changes, or lower-level Asio allocator work only if a future profile shows them becoming significant.
