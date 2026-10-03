# Combat, fan-out, packet buffers and reactor latency audit

Date: 2026-10-01. Branch: `perf/realistic-load-latency`.
This is an isolated synthetic experiment, not a production capacity guarantee.
It continues [the preceding workload audit](STRESSBOT_WORKLOAD_AUDIT.md).

## 1. Executive summary

The current baseline already contains the owned attack-retry timer, follow
request lifecycle fixes, reactor preprocessing budget accounting, item registry
synchronization, and uninitialized **outgoing** pooled messages with initialized
crypto padding. None of those changes was repeated.

A subsequent focused [ItemRegistry lifetime correction](ITEM_REGISTRY_LIFETIME_AUDIT.md)
replaces membership checks with strong ownership pinning. Its validation and
unmeasured hot-path costs are separate from the CPU measurements below.

The next demonstrable amplification is a temporary `NetworkMessage` initialized
to 65,500 bytes for each recipient of very small combat packets. The current
release assembly explicitly called `memset(..., 65500)` while constructing an
eight-byte magic effect. The selected serializers now use small, bounded,
initialized, write-only plaintext buffers. They still copy the written prefix
into a recipient-owned `OutputMessage`; encryption, checksums, padding,
visibility gates, packet order and gameplay semantics remain unchanged.

New diagnostics distinguish combat scopes, area target collection, fan-out
candidate counts, payload bytes/writes, selected serializer initialization,
output append, crypto framing, connection enqueue, and bounded per-source
callback duration/queue-age histograms. Diagnostics remain disabled by default.

The complete matched matrix contains eight scenarios before and eight after,
all at full population, with no parser errors, unknown opcodes, disconnects or
failed server shutdowns. Eighteen focused test targets passed. Observed REALISTIC
CPU fell by 10.51%, 9.80% and 5.84% at 300, 600 and 1,000 bots, respectively.
These are single-run local observations, not statistical capacity guarantees.

TORTURE 1000 did **not** reduce total CPU: 122.29% became 140.25% (+14.69%).
Successful weapon executions increased from approximately 1,475.7 to 2,183.2/s,
while mean queue delay fell from 248.19 to 110.65 ms and maximum delay from
589.16 to 366.63 ms. Main-thread CPU fell, but network delivery consumed more
CPU. The optimization improves serialization cost and overload progress; it
does not satisfy a blanket lower-CPU requirement or eliminate gameplay lag.

There is no consistent RSS win, LOGIN_ONLY maxima worsened, and some individual
combat/callback maxima increased. Remaining clearing in other paths is still
dominant in the instruction profile. Full Windows/gameplay acceptance is not
complete; the branch needs that validation before a production merge.

## 2. Root causes and audit decisions

### Measured packet clearing amplification

Each selected outgoing serializer previously default-constructed a full
65,500-byte `NetworkMessage`. A successful magic effect only appended eight
bytes. Repeating that initialization for every observer amplified local combat
work before the existing output pipeline even started. The reference
instrumented TORTURE 1000 window counted 10,437,211 selected initializations:
683,637,320,500 logically initialized bytes, approximately 636.69 GiB.

`PacketBuffer<Capacity>` has no receive API, arbitrary seek or reserved network
headers. Only its written prefix is exposed. Overflow invalidates the payload;
the current senders have statically bounded capacity for every legal input.
String conversion retains the existing Latin-1 limit and empty-field fallback.
Both `sendTextMessage` overloads are covered.

| Sender | Previous initialized payload storage | New capacity | Wire payload |
| --- | ---: | ---: | ---: |
| Magic effect | 65,500 | 8 | 8 |
| Distance effect | 65,500 | 13 | 13 |
| Creature health | 65,500 | 6 | 6 |
| Creature turn | 65,500 | 14 | 14 |
| Combat text, both overloads | 65,500 | 8,196 | 4 + Latin-1 bytes |
| Animated text | 65,500 | 8,201 | 9 + Latin-1 bytes |

The current fork sends **16-bit** effect identifiers. The turn payload includes
the `0x63` marker, 32-bit creature ID and direction; it is fourteen bytes, not
thirteen. No stock-protocol size assumption was substituted for this code.

The new release assembly for `sendMagicEffect` has a small stack frame and
writes the eight-byte payload directly into its local storage, without the
65,500-byte `memset`. Logical initialization counters describe requested buffer
capacity; they are **not hardware memory-bandwidth measurements**, and compiler
dead-store elimination can reduce actual physical stores further.

### Attack eligibility and callback order: audited, unchanged

`Creature::onAttacking` calls the attacked hooks before its LOS test. Moving a
player cooldown rejection ahead of those hooks would change script-visible
in-fight and attack behavior. `Player::doAttacking` retains its existing
condition/cooldown/weapon/ammo checks, classic/nonclassic timing and the already
owned retry timer. Attempt/success counters were added, not new retries.

No evidence supported changing attack speed, weakening monster AI, suppressing
failed attempts, or caching mutable weapon/target state across invocations.

### Health, Lua hooks and spectator reuse: audited, unchanged

Health/mana callbacks can recurse and change positions, instances or health.
The existing damage path already reuses its local spectator vector between
mana-shield feedback and later damage components. Healing can use a same-floor
query, whereas damage/effects use multifloor queries. Merging these different
queries would not preserve visibility. No cross-frame recipient cache was
introduced, and no intermediate health/effect notification was coalesced.

A source-level trace of an ordinary area spell is:

1. Build the LOS-filtered dynamic tile list once from the existing immutable
   directional matrix.
2. Query the area-expanded, player-only spectator range once; reuse that vector
   for the tile effects.
3. For each tile, preserve tile effects/field callbacks before collecting that
   tile's targets into owned `shared_ptr`s.
4. Apply each target's copied damage in the existing order. Health feedback
   retains its target-centered query/range; shield feedback can reuse that
   transaction's vector. Lua recursion can introduce further transactions.
5. Distance effects retain the union of their two endpoint queries; that is not
   interchangeable with the area or health ranges.

The trace is a code-path analysis, **not an exact runtime per-event query
count**. Global query counts also include movement, items and other work.
The report does not divide all queries by combat events and call that an exact
combat-attributed measurement.

### Area geometry and allocation: audited, unchanged

`AreaCombat::getArea` already returns an immutable directional matrix by const
reference. Absolute tiles and LOS validity are dynamic and cannot be cached
across casts. `toDamageCreatures` already reserves 100 entries and owns target
references through application. Reordering target collection ahead of tile
callbacks or replacing owners with raw pointers would alter behavior/lifetime.

Only the diagnostic timing was split: target collection is now measured
separately and subtracted from the tile-processing phase. Those two phases can
be interpreted separately; most higher-level combat scopes remain inclusive.
No new area representation, sorting, deduplication or allocation policy was
introduced without allocation evidence.

### Recipient policy and sharing: audited, unchanged

Effect, health, animated-text and distance recipient queries already use the
player-only mode where appropriate. Gameplay creature callbacks are separate
and were not narrowed. Per-recipient viewport, tile/ground, stack position,
instance, hidden-health, known-creature, cast and spy rules remain in their
original layers. A spy can have a different viewport from its own player.

The optimization reduces the local serialization buffer; it does **not** share
mutable `OutputMessage`s or encrypted bytes between connections. Health
percentage division/rounding was deliberately left unchanged.

### Long callbacks and reactor overload

The reactor cannot preempt an executing Lua or combat callback. Per-source
diagnostics now retain 128 bounded source slots plus one overflow aggregate,
duration and queued-age histograms, and counts above 5/10/25/50/100 ms. Full
description/origin hashes identify slots; bounded labels are for display.

The private item fixture touches 1,000 positions every two seconds. Reference
REALISTIC 600 queue max was 117.24 ms with this event and 58.64 ms without it;
the grouped global-event callback reached 94.94 ms versus 6.96 ms. TORTURE 1000
still exceeded 500 ms without the event. Thus the synthetic event contributes
spikes, but is not the sole overload source. These are attribution runs, not
code A/B comparisons or a reason to disable production scripts.

No production callback was chunked: yielding within combat, damage or an
arbitrary Lua loop changes observable atomic ordering. Reactor fairness,
budgets, queue limits and cancellation semantics were not changed in this pass.

## 3. Measurement method and provenance

The pre-edit release candidate was first reproduced for REALISTIC 300/600/1000
and TORTURE 1000, with CPU respectively 50.82/100.13/166.21/124.60 percent of one
core. Those observations precede fine instrumentation and are a reproduction
check, **not the matched comparison against the new instrumented binary**.

Matched reference/optimized runs use the same fine instrumentation, frozen bot,
seed 180252, offered rates, map, 1,000 monsters, two network threads, and private
MariaDB fixture. Each run requires full population, twenty seconds of warm-up,
then at least sixty seconds of measurement. Compilation and Callgrind profiling
do not run concurrently with CPU windows. Successful shutdown is checked
separately from completion of the measurement window.

Frozen offered rates use 20 ms login spacing and a bot packet cap of 18.
REALISTIC walk/scan intervals are 1,500/2,000 ms; TORTURE uses 300/500 ms.
The knight's spell/chat intervals are 6,000/30,000 ms. No rate was reduced
between the matched binaries.

CPU is Linux process tick time divided by elapsed monotonic time; **100% means
one logical core**, not the whole machine. RSS is process resident memory.
Thread CPU separates main reactor, network workers and the remaining worker
threads. Tests use GCC 13.3 Release `-O3`, LTO and unity under WSL on the local
i5-10300H host. This is not a Windows/MSVC capacity or latency result.

The normal ignored `config.lua` remains at `maxConnectionsPerIP = 100`. The
isolated fixture permits its synthetic connections on private ports 17711/17712
and database `atlas_perf_20261001`; no production database is modified.

| Artifact | SHA-256 |
| --- | --- |
| Pre-instrumentation release reference | `307a8e5baade75f7419eb3b6690fb0cdaee08ed7ead24e861e83dc2041f2f33a` |
| Fine-instrumentation reference, commit `ec144a66` | `2a5068e823ad2551ff3d6059b5e9a1059a150efdd8d5118eda454a22915a5b39` |
| Bounded-buffer candidate, commit `40032c21` | `90059975b55338307b2d144b1f45ff81d16472a5cf902400e131190bfeb738f6` |
| Frozen StressBot assembly, unchanged in this audit | `7187abee663f5aa1fe93eb1ce21da0414a809d5960f23aefec59bf74968e19df` |

Raw JSON, logs, launchers, analysis scripts and Callgrind dumps remain outside
Git in `atlas-performance-results-20261001` and the private WSL profile folder.
No benchmark XML, private world, account credentials, config, binary or raw log
is staged. The bot source was not altered to obtain an apparent server gain.

### Interpretation limits

The fixed histogram p50/p95/p99 values shown below are **the worst five-second
reporting-interval upper bounds**, not exact pooled whole-run percentiles.
Scope totals are elapsed wall time, include child scopes unless noted, and can
include descheduling. They are not additive CPU percentages. Complete report
blocks within the measurement window are validated against the captured generic
telemetry; post-window bot runtime and shutdown are excluded. Reporting phases
can straddle the CPU window, and atomic network counters can cross report
boundaries, so rates are diagnostic approximations.

Depending on reporting phase, a window contains twelve or thirteen reports;
their first/last intervals are not aligned with its exact sixty-second CPU
boundary. Scope means and candidate/event ratios use the actual recorded call
counts, while counter-derived rates should not be mistaken for exact transport
rates. Bot start/end deltas are the independently sampled transport reference.

Combat event scopes count outer synchronous entries, including failed/no-output
attack attempts. Payload writes are plaintext chunks, **not necessarily one
opcode or one wire message**. Fan-out candidates are players considered by a
broadcast, before later per-protocol rejection; they are not always delivered
recipients. Wire counters include all asynchronously framed messages and cannot
be assigned back to one combat transaction from this instrumentation.

Actions/bot/s are bot send-side walk/attack/spell/heal/potion/chat/turn/outfit
attempts, excluding pings; they are not successful server hits. Successful attack
counts and received packets/bytes are reported separately. The frozen TORTURE
bot's display of activity-state categories is not used as a progress metric.

The seed freezes bot choices, not server RNG or scheduling. Accounts reset
position, resources and conditions, but persisted skills/inventory are not
fully reset between every run. Single-run differences below five percent are
not advertised as CPU gains; any such claim requires at least three matched
repetitions with median and min/max.

## 4. Combat breakdown

TORTURE 1000, event ON; values are reference → bounded-buffer candidate.
The measured windows are 60.64 and 60.82 seconds. `*` denotes the worst
five-second histogram upper bound. Mean change is observed per-call elapsed
cost, not an additive CPU percentage or a confidence interval. In particular,
small changes and unchanged code paths are not independent optimization claims.

| Scope | Calls | Total ms | Mean us | P95 us* | P99 us* | Max us | Mean change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Creature::onAttacking | 129,214 → 194,584 | 41629.4 → 43949.3 | 322.174 → 225.863 | 1048.6 → 524.3 | 2097.2 → 2097.2 | 30485.9 → 28326.8 | -29.9% |
| Player::doAttacking | 107,275 → 161,838 | 40432.9 → 42849.0 | 376.909 → 264.765 | 1048.6 → 524.3 | 4194.3 → 2097.2 | 30484.2 → 28325.3 | -29.8% |
| Monster::doAttacking | 20,394 → 32,452 | 1001.5 → 840.7 | 49.109 → 25.906 | 131.1 → 65.5 | 524.3 → 524.3 | 28846.3 → 26824.7 | -47.2% |
| Game::combatChangeHealth | 240,184 → 332,727 | 44052.2 → 32647.6 | 183.410 → 98.121 | 524.3 → 262.1 | 1048.6 → 524.3 | 17903.4 → 31787.8 | -46.5% |
| Combat::doCombat | 25,901 → 29,985 | 8882.8 → 4967.2 | 342.952 → 165.656 | 2097.2 → 1048.6 | 4194.3 → 2097.2 | 28845.3 → 32123.5 | -51.7% |
| Combat::doAreaCombat | 9,507 → 9,745 | 7868.0 → 4101.2 | 827.601 → 420.854 | 2097.2 → 2097.2 | 8388.6 → 4194.3 | 26896.6 → 32113.7 | -49.1% |
| Combat::area.buildTiles | 9,507 → 9,745 | 19.2 → 21.2 | 2.020 → 2.177 | 8.2 → 8.2 | 8.2 → 8.2 | 331.0 → 1941.8 | +7.8% |
| Combat::area.prepareDamage | 9,507 → 9,745 | 1.6 → 1.6 | 0.167 → 0.160 | 0.5 → 0.5 | 0.5 → 0.5 | 70.0 → 20.0 | -4.2% |
| Combat::area.collectSpectators | 9,507 → 9,745 | 62.0 → 71.9 | 6.520 → 7.382 | 16.4 → 16.4 | 65.5 → 65.5 | 714.4 → 4336.4 | +13.2% |
| Combat::area.processTiles | 9,507 → 9,745 | 2917.5 → 1205.0 | 306.875 → 123.657 | 1048.6 → 524.3 | 2097.2 → 2097.2 | 26543.6 → 8090.6 | -59.7% |
| Combat::area.collectTargets | 85,452 → 87,519 | 256.6 → 219.7 | 3.002 → 2.510 | 8.2 → 8.2 | 32.8 → 32.8 | 25355.9 → 9596.5 | -16.4% |
| Combat::area.applyTargets | 9,507 → 9,745 | 4601.1 → 2573.5 | 483.973 → 264.083 | 2097.2 → 1048.6 | 4194.3 → 2097.2 | 11091.1 → 32003.2 | -45.4% |
| Combat::healthCallbacks | 111,826 → 156,124 | 1167.2 → 1588.3 | 10.438 → 10.173 | 32.8 → 16.4 | 131.1 → 131.1 | 13624.3 → 31681.8 | -2.5% |
| Combat::applyHealth | 111,826 → 156,124 | 74.6 → 91.0 | 0.667 → 0.583 | 2.0 → 1.0 | 4.1 → 2.0 | 3761.5 → 1214.4 | -12.6% |
| Combat::broadcastHealth | 111,826 → 156,124 | 3778.3 → 706.1 | 33.788 → 4.522 | 65.5 → 8.2 | 131.1 → 32.8 | 9570.6 → 8892.8 | -86.6% |
| Combat::broadcastEffect | 303,412 → 397,092 | 8492.0 → 3286.4 | 27.988 → 8.276 | 65.5 → 32.8 | 131.1 → 65.5 | 11582.7 → 18218.9 | -70.4% |
| Combat::broadcastText | 111,826 → 156,124 | 2634.4 → 1002.3 | 23.558 → 6.420 | 65.5 → 16.4 | 131.1 → 32.8 | 9002.0 → 10299.0 | -72.7% |
| ProtocolGame::sendMagicEffect | 6,655,417 → 8,524,997 | 7168.9 → 1812.2 | 1.077 → 0.213 | 2.0 → 0.5 | 4.1 → 1.0 | 11529.0 → 18189.5 | -80.3% |
| ProtocolGame::sendCreatureHealth | 2,366,139 → 3,253,285 | 3553.2 → 495.1 | 1.502 → 0.152 | 2.0 → 0.3 | 2.0 → 0.5 | 9513.8 → 8881.3 | -89.9% |
| ProtocolGame::sendAnimatedText | 2,366,139 → 3,253,285 | 2414.2 → 750.0 | 1.020 → 0.231 | 2.0 → 0.5 | 4.1 → 1.0 | 8966.6 → 10260.9 | -77.4% |
| ProtocolGame::sendTextMessage | 2,370,890 → 3,255,238 | 3886.7 → 1289.2 | 1.639 → 0.396 | 2.0 → 1.0 | 4.1 → 1.0 | 8941.0 → 7813.5 | -75.8% |
| ProtocolGame::sendCreatureTurn | 40,733 → 52,585 | 46.9 → 13.7 | 1.150 → 0.260 | 4.1 → 0.5 | 4.1 → 1.0 | 838.3 → 164.3 | -77.4% |
| ProtocolGame::sendAddCreature | 4,387 → 4,993 | 24.3 → 21.0 | 5.550 → 4.199 | 8.2 → 8.2 | 32.8 → 32.8 | 8058.3 → 1078.3 | -24.3% |
| ProtocolGame::sendMoveCreature | 143,367 → 176,877 | 266.2 → 297.0 | 1.857 → 1.679 | 4.1 → 4.1 | 32.8 → 32.8 | 8060.2 → 2571.1 | -9.6% |
| ProtocolGame::outputAppend | 15,277,002 → 20,213,184 | 1779.7 → 2335.1 | 0.116 → 0.116 | 0.5 → 0.5 | 0.5 → 0.5 | 7892.5 → 18188.8 | -0.8% |
| Protocol::cryptoFrame | 263,163 → 574,231 | 1808.6 → 2344.1 | 6.873 → 4.082 | 32.8 → 16.4 | 65.5 → 32.8 | 9684.1 → 8442.4 | -40.6% |
| Connection::enqueue | 263,163 → 574,132 | 837.0 → 1845.8 | 3.180 → 3.215 | 4.1 → 4.1 | 32.8 → 32.8 | 9441.3 → 9705.7 | +1.1% |

No live distance-effect samples occurred in these knight workloads; its
production encoder is covered by the byte/framing tests, not a fabricated timing
row. Mana and prepare-death hooks were not exercised by this high-health fixture.

The smaller buffers reduce the mean cost of health/effect/text broadcasts and
their inclusive parents. They do not remove the owning output copy: append mean
cost is essentially unchanged. Longest observed health/Lua/area samples can
still increase despite lower means, and the remaining tail is not solved.

## 5. Fan-out

Values below are reference → candidate. Targets and candidates are per **area
cast**, whereas writes/bytes are per outer synchronous combat event. They are
different denominators and must not be interpreted as one universal recipient
count. The optimization changes buffer construction, not recipient selection.

| Scenario | Combat events | Targets/AoE | Candidates/AoE | Payload writes/event | Payload bytes/event |
| --- | --- | --- | --- | --- | --- |
| REALISTIC 300 ON | 68,210 → 68,965 | 2.26 → 2.28 | 19.07 → 18.22 | 28.92 → 27.88 | 627.5 → 609.5 |
| REALISTIC 600 ON | 124,667 → 125,564 | 2.08 → 2.00 | 21.40 → 21.46 | 33.08 → 32.12 | 737.2 → 717.4 |
| REALISTIC 1000 ON | 177,074 → 181,106 | 1.88 → 1.81 | 23.92 → 23.18 | 35.67 → 34.91 | 812.3 → 795.3 |
| TORTURE 1000 ON | 138,721 → 204,329 | 2.36 → 2.40 | 23.96 → 23.72 | 98.15 → 90.33 | 2096.7 → 1944.1 |
| REALISTIC 600 OFF | 127,058 → 136,751 | 2.13 → 2.00 | 21.75 → 21.48 | 32.63 → 31.92 | 731.0 → 719.8 |
| TORTURE 1000 OFF | 152,692 → 210,300 | 2.41 → 2.45 | 23.50 → 25.17 | 98.44 → 98.03 | 2074.0 → 2098.9 |

TORTURE 1000 ON fixed distributions, with worst-interval histogram upper
bounds marked `*`:

| Distribution | Samples | Mean | P95* | P99* | Max |
| --- | --- | --- | --- | --- | --- |
| Health candidates/broadcast | 111,826 → 156,124 | 21.16 → 20.84 | 64 → 64 | 64 → 64 | 42 → 42 |
| Effect candidates/broadcast | 303,412 → 397,092 | 21.94 → 21.47 | 64 → 64 | 64 → 64 | 48 → 46 |
| Text candidates/broadcast | 111,826 → 156,124 | 21.16 → 20.84 | 64 → 64 | 64 → 64 | 42 → 42 |
| Bytes/outer event | 138,721 → 204,329 | 2096.74 → 1944.13 | 8192 → 8192 | 16384 → 16384 | 24963 → 28969 |
| Writes/outer event | 138,721 → 204,329 | 98.15 → 90.33 | 512 → 256 | 1024 → 1024 | 1233 → 1496 |

Power-of-two bin upper bounds can exceed the actual sample maximum. The same
recipient can appear in multiple broadcasts within an event; these are not
unique players/event. No reduction in recipients is claimed as an optimization.
Exact combat-attributed spectator queries, unique notified players and wire
messages per transaction are **not available** from this instrumentation.
Dividing all global queries by combat events would mix unrelated movement/item
work into that number, so that proxy is deliberately omitted.

## 6. Memory operations and send pipeline

The separate reference Callgrind runtime-reset capture used 100 TORTURE bots,
1,000 monsters and approximately ten instrumented seconds. It recorded
543,279,765 instructions, with library `memset` accounting for 471,627,519
self instructions (86.81%) and `memcpy` 14,732,427 (2.71%). These instruction
shares are **not CPU percentages**; profiling changes progress. LTO/unity and
unresolved call sites also limit exact source attribution.

The corresponding candidate capture lasted 10.218 instrumented seconds, versus
10.217 for the reference, and recorded 523,929,518 instructions. Selected
self-instruction counts, not inclusive overlapping call-tree totals:

| Function | Reference instructions | Candidate instructions | Reference share | Candidate share |
| --- | ---: | ---: | ---: | ---: |
| Library `memset` | 471,627,519 | 426,580,984 | 86.81% | 81.42% |
| Library `memcpy` | 14,732,427 | 14,565,782 | 2.71% | 2.78% |
| `Protocol::onSendMessage` | 5,191,997 | 5,122,517 | 0.96% | 0.98% |
| `_int_malloc` | 1,408,332 | 1,773,772 | 0.26% | 0.34% |

Both profile runs had zero parser/unknown-opcode/disconnection counters and
exit code zero. Offered attack attempts were similar (1,774 versus 1,759), but
delivered packets were 100 versus 1,089 and bytes 144,896 versus 246,062 during
the capture. Thus these profiles are qualitative source evidence, not equal
completed-work CPU benchmarks; the raw instruction-total delta is not a speedup.

In the non-profiled TORTURE ON windows, selected logically initialized volume
fell from 636.69 to 41.17 GiB even as selected serializer calls increased from
10,437,211 to 14,066,336. Average requested initialized capacity became roughly
3,143 bytes instead of 65,500. Approximately 95.2% less capacity per selected
call does **not** mean 95.2% less process CPU or measured hardware bandwidth.
Total appended plaintext grew from 321,214,215 to 428,917,987 bytes; removing
that required ownership copy was not part of the optimization.

The assembly and selected-initialization counters independently establish the
outgoing temporary-buffer source. Other memory operations remain:

| Source | Size/lifetime | Decision |
| --- | --- | --- |
| Selected small outgoing serializers | Capacities above; invocation-local | Replace full receive-capable buffer with bounded initialized writer |
| Other outgoing `NetworkMessage` serializers | Up to 65,500; invocation-local | Unchanged; require separate size/behavior audit |
| Incoming/Lua `NetworkMessage` | 65,500; parser/Lua lifetime | Keep initialized storage and parser state |
| Received message ownership copy | Full buffer and parser state; dispatcher handoff | Keep direct copy construction; do not trim arbitrary Lua/parser seek state |
| Pooled `OutputMessage` | Recipient-owned, retained through async completion | Earlier optimization already avoids blanket zeroing; do not repeat |
| Append into output | Only written payload prefix | Keep synchronous copy; required ownership boundary |
| Crypto headers/checksum/XTEA padding | Exact framed ranges and final block | Keep initialized headers and `0x33` padding |
| Area target snapshot | Existing reserved vector of owning references | Keep ownership/order; no speculative raw-pointer conversion |
| Spectator vectors and merge/dedup | Existing reserve/partition policy | No allocator rewrite without per-site allocation evidence |

`Connection::internalSend` retains the owned output message for `async_write`.
No separate connection payload buffer or additional mandatory whole-message copy
was found in that path. Producer/consumer locking, strand behavior and framing
were not moved or changed. The retained-free pool remains **2,048**, which is not
a maximum on live allocations.

There is an additional diagnostic caveat: the allocator's raw free lists are
keyed by **block size and capacity**, not by message type. The GCC binary has
one `LockfreeFreeList<65528, 2048>` for the shared-control-block allocation
layout used here. Incoming and outgoing allocations can therefore compete for
the same retained blocks, although only outgoing allocations increment the
output diagnostics. A tracked output peak below 2,048 does not by itself prove
that this shared free list cannot miss. No incoming initialization or ownership
policy was changed, and misses remain allocation fallbacks, not packet drops.

## 7. Reactor tails and controlled before/after matrix

### Matched CPU, memory, progress and backlog

Every arrow is reference → candidate, with the same offered workload. ON/OFF
only refers to the private synthetic item event, not production scripts.
RSS is the sampled mean; CPU percentages use one core as 100%.

| Scenario | CPU % | CPU change | RSS MiB | Actions/bot/s | Main % | Network % | Backlog max | Queue max ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| LOGIN_ONLY 600 ON | 23.11 → 22.86 | -1.10% | 302.69 → 297.96 | 0.000 → 0.000 | 7.75 → 7.44 | 15.38 → 15.42 | 276 → 273 | 69.68 → 77.12 |
| LOGIN_ONLY 1000 ON | 37.67 → 36.74 | -2.46% | 387.05 → 392.64 | 0.000 → 0.000 | 12.11 → 11.43 | 25.57 → 25.29 | 429 → 434 | 105.84 → 121.44 |
| REALISTIC 300 ON | 52.78 → 47.24 | -10.51% | 257.69 → 257.01 | 0.162 → 0.160 | 22.86 → 18.45 | 29.94 → 28.79 | 306 → 299 | 64.35 → 61.57 |
| REALISTIC 600 ON | 105.50 → 95.16 | -9.80% | 324.58 → 338.77 | 0.170 → 0.171 | 45.24 → 34.82 | 60.26 → 60.34 | 537 → 546 | 117.24 → 78.06 |
| REALISTIC 1000 ON | 171.63 → 161.61 | -5.84% | 420.90 → 428.09 | 0.174 → 0.169 | 71.78 → 58.11 | 99.88 → 103.50 | 806 → 838 | 155.73 → 151.87 |
| TORTURE 1000 ON | 122.29 → 140.25 | +14.69% | 488.05 → 476.95 | 1.878 → 1.864 | 84.69 → 78.20 | 37.58 → 62.05 | 2542 → 2119 | 589.16 → 366.63 |
| REALISTIC 600 OFF | 100.96 → 92.32 | -8.56% | 316.74 → 319.49 | 0.171 → 0.171 | 40.69 → 32.15 | 60.26 → 60.21 | 541 → 553 | 58.64 → 21.49 |
| TORTURE 1000 OFF | 123.69 → 140.75 | +13.80% | 485.41 → 474.64 | 1.899 → 1.882 | 83.24 → 77.54 | 40.45 → 63.21 | 2428 → 1908 | 542.13 → 270.35 |

LOGIN_ONLY has idle connected bots, not an empty world: monsters and the item
event remain active. Its small CPU differences are not established gains and
its queue/callback maxima increased. REALISTIC 1000 also offered about 2.8%
fewer send-side actions in this run; normalization below limits that conclusion.

RSS peak is the maximum sampled resident memory, not allocator high-water
capacity. Measurement durations are included to expose the small window-length
differences and the twelve/thirteen-report boundary caveat:

| Scenario | Sampled peak RSS MiB | Window seconds |
| --- | --- | --- |
| LOGIN_ONLY 600 ON | 310.25 → 305.42 | 60.40 → 60.37 |
| LOGIN_ONLY 1000 ON | 398.48 → 406.50 | 60.55 → 60.39 |
| REALISTIC 300 ON | 262.02 → 260.53 | 60.49 → 60.44 |
| REALISTIC 600 ON | 327.59 → 341.29 | 60.64 → 60.57 |
| REALISTIC 1000 ON | 423.29 → 430.15 | 60.67 → 60.80 |
| TORTURE 1000 ON | 489.81 → 486.48 | 60.64 → 60.82 |
| REALISTIC 600 OFF | 320.84 → 323.22 | 60.65 → 60.59 |
| TORTURE 1000 OFF | 489.21 → 477.94 | 60.55 → 60.72 |

### Transport and successful server work

Bot incoming rates use exact sampled start/end deltas. Wire, success and
initialization rates use complete reporting blocks and are approximate because
their boundaries do not exactly match the CPU window. A successful attack here
means weapon execution returned true, not necessarily an unblocked damaging hit.

| Scenario | Bot incoming packets/s | Bot incoming MiB/s | Wire messages/s | Successful attacks/s | Selected initialized MiB/s |
| --- | --- | --- | --- | --- | --- |
| LOGIN_ONLY 600 ON | 3023 → 3106 | 0.353 → 0.358 | 3238 → 3057 | 0.0 → 0.0 | 194.649 → 0.024 |
| LOGIN_ONLY 1000 ON | 4810 → 4728 | 0.600 → 0.593 | 4728 → 4758 | 0.0 → 0.0 | 282.278 → 0.039 |
| REALISTIC 300 ON | 6791 → 6605 | 0.900 → 0.885 | 6716 → 6525 | 322.4 → 327.3 | 1582.610 → 71.975 |
| REALISTIC 600 ON | 14039 → 13980 | 1.922 → 1.884 | 13846 → 13798 | 623.9 → 623.3 | 3321.352 → 153.462 |
| REALISTIC 1000 ON | 20627 → 20916 | 3.007 → 2.988 | 20358 → 20824 | 898.4 → 933.8 | 5123.335 → 243.079 |
| TORTURE 1000 ON | 4485 → 9641 | 5.130 → 6.950 | 4340 → 9441 | 1475.7 → 2183.2 | 10750.823 → 693.223 |
| REALISTIC 600 OFF | 14413 → 14340 | 1.715 → 1.675 | 14194 → 15325 | 624.6 → 677.6 | 3343.300 → 166.576 |
| TORTURE 1000 OFF | 5355 → 10073 | 5.267 → 7.308 | 5266 → 9939 | 1658.7 → 2250.5 | 11827.524 → 774.508 |

All sixteen runs maintained full population with zero parser errors, unknown
opcodes, failures and disconnects, and server exit code zero. More incoming
packets do not by themselves prove more gameplay: faster queue advancement
also lets the existing auto-send callback run more often and flush smaller
batches. No batching policy was changed. In TORTURE ON, auto-send calls rose
264 → 590, framed messages 263,163 → 574,233, and successful weapon executions
89,491 → 132,784. This is observed higher processed/delivered progress, not a
change to attack formulas, visibility or StressBot offered rates.

| Scenario | Full-process CPU ms/bot action | Full-process CPU ms/successful weapon execution |
| --- | --- | --- |
| REALISTIC 300 ON | 10.838 → 9.818 | 1.637 → 1.443 |
| REALISTIC 600 ON | 10.356 → 9.277 | 1.691 → 1.527 |
| REALISTIC 1000 ON | 9.874 → 9.537 | 1.910 → 1.731 |
| TORTURE 1000 ON | 0.651 → 0.752 | 0.829 → 0.642 |
| REALISTIC 600 OFF | 9.860 → 8.986 | Not used for a separate gain claim |
| TORTURE 1000 OFF | 0.651 → 0.748 | Not used for a separate gain claim |

These ratios include all server CPU, not isolated action cost. LOGIN_ONLY has
zero actions, so CPU/action is undefined. REALISTIC 1000's 3.41% CPU/action
difference requires repeated runs before calling it a meaningful normalized
gain. TORTURE ON CPU/bot attempt worsened, whereas CPU/successful execution
fell by approximately 22.5%; both are retained to expose the tradeoff.

### Pool pressure, path requests and overload

| Scenario | Tracked output peak | Window misses | Overflow frees | Deferrals | Path requests | Task drops |
| --- | --- | --- | --- | --- | --- | --- |
| LOGIN_ONLY 600 ON | 919 → 839 | 0 → 49 | 0 → 0 | 0 → 1 | 21865 → 19996 | 0 → 0 |
| LOGIN_ONLY 1000 ON | 1646 → 1811 | 41 → 0 | 0 → 0 | 0 → 0 | 30330 → 30441 | 0 → 0 |
| REALISTIC 300 ON | 505 → 492 | 0 → 0 | 0 → 0 | 0 → 0 | 21699 → 21979 | 0 → 0 |
| REALISTIC 600 ON | 927 → 1066 | 0 → 0 | 0 → 0 | 36 → 9 | 38643 → 38859 | 0 → 0 |
| REALISTIC 1000 ON | 1646 → 1763 | 12 → 0 | 0 → 0 | 3683 → 1114 | 53249 → 53284 | 0 → 0 |
| TORTURE 1000 ON | 1823 → 1888 | 0 → 336 | 57 → 0 | 1050330 → 389877 | 24853 → 38439 | 0 → 0 |
| REALISTIC 600 OFF | 818 → 715 | 0 → 0 | 0 → 0 | 3 → 0 | 39478 → 42646 | 0 → 0 |
| TORTURE 1000 OFF | 1825 → 1864 | 145 → 256 | 31 → 0 | 886672 → 366963 | 26606 → 40441 | 0 → 0 |

Deferrals count repeated postponements, not unique tasks. TORTURE ON deferrals
fell about 62.9%, but 389,877 remain; no queue-limit enlargement or deliberate
packet dropping is behind the result. Output peak is not total shared-free-list
occupancy (section 6), and misses are allocator fallbacks rather than drops.
The pool capacity was not enlarged.

Output-diagnostic pool hits, sampled as exact start/end window deltas:

| Scenario | Window hits |
| --- | --- |
| LOGIN_ONLY 600 ON | 195771 → 184420 |
| LOGIN_ONLY 1000 ON | 286203 → 288322 |
| REALISTIC 300 ON | 406282 → 394350 |
| REALISTIC 600 ON | 839118 → 835980 |
| REALISTIC 1000 ON | 1235015 → 1266133 |
| TORTURE 1000 ON | 263567 → 574255 |
| REALISTIC 600 OFF | 860915 → 928566 |
| TORTURE 1000 OFF | 318492 → 603406 |

### Queue age versus callback execution

`*` denotes the worst five-second histogram upper bound; maxima are observed
samples. A bin upper bound can exceed the actual maximum. These are not exact
whole-window quantiles and cannot be pooled from logged percentiles.

| Scenario | Type | Mean us | P50 ms* | P95 ms* | P99 ms* | Max ms |
| --- | --- | --- | --- | --- | --- | --- |
| LOGIN_ONLY 600 ON | Queue | 696.72 → 545.62 | 0.524 → 0.524 | 4.194 → 4.194 | 16.777 → 16.777 | 69.677 → 77.121 |
| LOGIN_ONLY 600 ON | Callback | 53.91 → 53.19 | 0.002 → 0.002 | 0.131 → 0.131 | 1.049 → 1.049 | 65.821 → 75.532 |
| LOGIN_ONLY 1000 ON | Queue | 1913.74 → 1453.05 | 2.097 → 2.097 | 16.777 → 8.389 | 33.554 → 33.554 | 105.835 → 121.442 |
| LOGIN_ONLY 1000 ON | Callback | 56.78 → 54.65 | 0.002 → 0.002 | 0.131 → 0.131 | 1.049 → 0.524 | 102.544 → 113.157 |
| REALISTIC 300 ON | Queue | 647.14 → 488.82 | 0.262 → 0.262 | 4.194 → 2.097 | 33.554 → 67.109 | 64.350 → 61.567 |
| REALISTIC 300 ON | Callback | 164.28 → 137.16 | 0.008 → 0.008 | 1.049 → 1.049 | 2.097 → 1.049 | 65.892 → 42.307 |
| REALISTIC 600 ON | Queue | 2548.63 → 1460.42 | 2.097 → 1.049 | 33.554 → 8.389 | 67.109 → 33.554 | 117.239 → 78.061 |
| REALISTIC 600 ON | Callback | 179.59 → 143.07 | 0.008 → 0.008 | 1.049 → 1.049 | 2.097 → 2.097 | 94.940 → 72.530 |
| REALISTIC 1000 ON | Queue | 11858.07 → 7705.21 | 8.389 → 8.389 | 67.109 → 67.109 | 268.435 → 134.218 | 155.732 → 151.873 |
| REALISTIC 1000 ON | Callback | 198.71 → 171.14 | 0.008 → 0.008 | 1.049 → 1.049 | 4.194 → 4.194 | 134.953 → 115.275 |
| TORTURE 1000 ON | Queue | 248185.72 → 110645.64 | 536.871 → 134.218 | 536.871 → 536.871 | 1073.742 → 536.871 | 589.159 → 366.631 |
| TORTURE 1000 ON | Callback | 201.64 → 164.13 | 0.016 → 0.016 | 1.049 → 0.524 | 2.097 → 2.097 | 154.717 → 121.700 |
| REALISTIC 600 OFF | Queue | 1431.52 → 811.27 | 1.049 → 0.524 | 16.777 → 8.389 | 67.109 → 16.777 | 58.640 → 21.493 |
| REALISTIC 600 OFF | Callback | 161.94 → 134.77 | 0.008 → 0.008 | 1.049 → 1.049 | 2.097 → 1.049 | 22.842 → 19.712 |
| TORTURE 1000 OFF | Queue | 202979.31 → 100765.68 | 536.871 → 134.218 | 536.871 → 268.435 | 536.871 → 268.435 | 542.135 → 270.353 |
| TORTURE 1000 OFF | Callback | 194.89 → 158.54 | 0.016 → 0.016 | 1.049 → 0.524 | 2.097 → 2.097 | 89.376 → 65.176 |

TORTURE ON mean/max queue age improved by approximately 55.4%/37.8%, with more
successful executions, but its p95 upper bound did not improve. REALISTIC 300
queue p99 upper bound worsened despite lower mean/max; LOGIN maxima worsened.
No assertion that all latency metrics improved is supported by these runs.

### Top callback sources by execution tail

TORTURE 1000 ON. Source names below abbreviate their recorded description and
origin. Global events refer to the grouped `think()` callback at
`src/globalevent.cpp:157`, not one independently timed Lua event. Creature checks
refer to `Game::checkCreatures` scheduled from `src/game.cpp:6491`.
`Q95/Q99` are histogram upper bounds; `Qmax` is the observed queued age.
Threshold counts are execution durations, not queue ages.

Reference:

| Source | Calls | Total ms | Mean ms | P95 ms* | P99 ms* | Max ms | Q95/Q99/Qmax ms | >5/10/25/50/100 ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| GlobalEvents::think | 72 | 3246.3 | 45.087 | 268.435 | 268.435 | 154.717 | 536.871/536.871/473.605 | 39/32/28/28/21 |
| Game::checkCreatures | 170 | 4929.1 | 28.995 | 134.218 | 134.218 | 72.704 | 536.871/536.871/526.734 | 170/159/82/26/0 |
| Player::setAttackedCreature | 1164 | 542.6 | 0.466 | 1.049 | 33.554 | 30.488 | 536.871/536.871/525.378 | 3/2/1/0/0 |
| Player::attackCheck | 86265 | 35925.9 | 0.416 | 1.049 | 4.194 | 27.922 | 536.871/1073.742/589.130 | 158/37/1/0/0 |
| ProtocolGame::parsePacket | 126553 | 11187.6 | 0.088 | 1.049 | 2.097 | 27.552 | 1073.742/1073.742/588.246 | 99/24/5/0/0 |
| OutputMessagePool::sendAll | 264 | 1064.5 | 4.032 | 16.777 | 33.554 | 17.188 | 536.871/536.871/503.400 | 75/12/0/0/0 |
| Game::checkCreatureWalk | 38230 | 1274.5 | 0.033 | 0.262 | 0.524 | 11.412 | 536.871/1073.742/587.209 | 5/3/0/0/0 |
| Creature::requestFollowPathUpdate | 25793 | 131.3 | 0.005 | 0.016 | 0.033 | 9.027 | 536.871/1073.742/588.989 | 1/0/0/0/0 |
| Decay::checkDecay | 213 | 326.8 | 1.534 | 8.389 | 8.389 | 7.996 | 1073.742/1073.742/568.099 | 3/0/0/0/0 |

Candidate:

| Source | Calls | Total ms | Mean ms | P95 ms* | P99 ms* | Max ms | Q95/Q99/Qmax ms | >5/10/25/50/100 ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| GlobalEvents::think | 82 | 3246.5 | 39.591 | 134.218 | 134.218 | 121.700 | 268.435/268.435/240.174 | 44/33/29/29/19 |
| Game::checkCreatures | 282 | 4786.3 | 16.973 | 67.109 | 67.109 | 60.527 | 268.435/536.871/330.971 | 239/165/75/6/0 |
| ProtocolGame::parsePacket | 124290 | 7257.4 | 0.058 | 0.524 | 1.049 | 43.261 | 536.871/536.871/366.631 | 75/11/4/0/0 |
| Player::attackCheck | 128781 | 39006.8 | 0.303 | 1.049 | 2.097 | 28.329 | 536.871/536.871/366.512 | 305/67/4/0/0 |
| OutputMessagePool::sendAll | 590 | 2315.2 | 3.924 | 16.777 | 16.777 | 13.898 | 268.435/536.871/320.186 | 173/33/0/0/0 |
| Game::checkCreatureWalk | 53701 | 1453.6 | 0.027 | 0.262 | 0.524 | 12.001 | 268.435/536.871/362.292 | 11/2/0/0/0 |
| Game::checkCreatureAttack | 8219 | 711.4 | 0.087 | 0.524 | 1.049 | 9.288 | 268.435/536.871/362.604 | 7/0/0/0/0 |
| Decay::checkDecay | 394 | 383.7 | 0.974 | 8.389 | 16.777 | 8.956 | 268.435/536.871/358.647 | 6/0/0/0/0 |
| Creature::requestFollowPathUpdate | 38785 | 170.0 | 0.004 | 0.016 | 0.066 | 4.448 | 268.435/536.871/362.599 | 0/0/0/0/0 |

The event OFF runs lower candidate TORTURE callback max from 121.70 to 65.18 ms
and queue max from 366.63 to 270.35 ms, but remain overloaded. Grouped callbacks
can contain multiple events, so their entire cost is not assigned to the item
fixture. Removing that private event is attribution, not a production fix.

### Thread attribution

The two network workers are reported separately below, as percent of one core.
Other three startup/login/DB worker threads were effectively idle in measured
steady state; their startup work is outside these windows. The main-thread
column is in the controlled matrix above. Tick rounding can produce small
differences between summed thread and process CPU.

| Scenario | Network worker 1 % | Network worker 2 % |
| --- | --- | --- |
| LOGIN_ONLY 600 ON | 7.682 → 7.702 | 7.699 → 7.719 |
| LOGIN_ONLY 1000 ON | 12.783 → 12.668 | 12.783 → 12.618 |
| REALISTIC 300 ON | 14.977 → 14.394 | 14.960 → 14.394 |
| REALISTIC 600 ON | 30.147 → 30.178 | 30.114 → 30.162 |
| REALISTIC 1000 ON | 49.958 → 51.760 | 49.925 → 51.743 |
| TORTURE 1000 ON | 18.831 → 30.993 | 18.749 → 31.059 |
| REALISTIC 600 OFF | 30.121 → 30.119 | 30.138 → 30.086 |
| TORTURE 1000 OFF | 20.249 → 31.541 | 20.199 → 31.672 |

TORTURE ON main CPU fell 84.69 → 78.20 while network CPU rose 37.58 → 62.05.
Delivered MiB/s increased about 35.5% and message rate more than doubled. This
supports increased delivery/framing work as an explanation for the total CPU
increase, but does not isolate socket, kernel, encryption and allocator CPU
individually. Crypto mean per message fell with smaller batches while its total
scope time increased 1808.6 → 2344.1 ms; connection enqueue also did more work.

## 8. Correctness and regression coverage

The metrics-only reference passed seven focused CTest targets. After the buffer
change, eighteen targets were rebuilt and passed:

- `test_combat_packets`, `test_performance_metrics`, `test_outputmessage`,
  `test_protocolgame_pipeline`, `test_xtea`;
- `test_combat`, `test_equipment_combat_bonus`, `test_creature_walk`,
  `test_reactor`, `test_game_creature_checks`;
- `test_map_spectators`, `test_spectators`, `test_monster_target_state`,
  `test_monster_idle_events`;
- `test_condition_damage_queue`, `test_condition_shared_lifetime`,
  `test_connection_write_lifetime`, `test_script_env_bounds`.

The eight new packet cases exercise production senders and compare their bytes
against the original `NetworkMessage` encoding. Coverage includes 16-bit
effects, distance, health rounding and hidden health, fourteen-byte turns,
combat text through both overloads, animated text, valid Latin-1, malformed UTF-8,
non-Latin-1 fallback, empty strings, 8,192/8,193 encoded characters and 16,384
UTF-8 bytes that legitimately encode to 8,192 Latin-1 characters. Capacity
overflow exposes no partial packet. Every representative payload is separately
framed with XTEA/checksum enabled and disabled in zeroed versus dirty storage.

Dense/sparse 128-observer tests preserve viewport and instance rejection, and
exercise the owner/cast/spy production delegation. Repeated health broadcasts
copy the correct payload to all 128 observers. The fixture initially omitted
the spectator type-partition precondition; that test setup was corrected, then
the final rebuilt packet test passed. No production visibility rule was changed
to satisfy a faulty fixture.

Existing tests cover target block-state independence for area conditions,
equipment reduction before mana-shield absorption, condition ticks and removal,
target switching/removal/teleport/floor/protection-zone transitions, owned
retry cancellation/rejection, reactor ordering/budget/fairness, and async-write
buffer lifetime. These are focused regressions, **not the entire gameplay
matrix requested for a production release**.

Movement/add/remove and death packets were not converted to the new writer.
Their representative live traffic is checked by the frozen bot parser, but no
new full byte-for-byte death/movement golden trace is claimed here. Manual
Windows client/server play, ranged/PvP/summon combinations, death/overkill and
every classic/nonclassic/exhaustion combination remain to be verified.
ASan/UBSan were not run, as requested by the user; TSan/MSan were not run either.

## 9. Remaining bottlenecks

- TORTURE queue maxima of 366.63 ms ON and 270.35 ms OFF remain too high for
  responsive combat. Serialization savings do not solve sustained overload.
- Non-preemptible grouped global callbacks still exceed 100 ms. Splitting
  arbitrary combat/Lua work would require an ordering/lifetime design, not just
  a reactor setting; no such gameplay change was made.
- Other receive, Lua and outgoing buffers still clear memory. Library `memset`
  remains 81.42% of the candidate Callgrind self instructions. Exact per-caller
  clearing/allocation counts were not recovered from every unresolved LTO site.
- Network worker cost dominates REALISTIC 1000 and explains much of the extra
  TORTURE CPU. Socket/kernel/encryption cost was not individually optimized.
- Individual health/Lua/area/parse maxima can increase despite better means.
  RSS is not consistently lower, and pool misses are not uniformly reduced.
- Exact per-event query/unique-recipient/wire attribution, spectator allocation
  counts, opcode splitting within combined writes and a timed standalone AoE
  microbenchmark remain measurement gaps. No container rewrite is justified by
  the current evidence alone.
- Mana/death hooks have no live samples in this high-health synthetic fixture.
  That does not show those production callbacks are cheap or safe to skip.
- There is only one sixty-second run per side/scenario. Small differences need
  at least three matched repetitions with median/min/max. No separate
  lower-monster-population matrix or all-default-diagnostics-off A/B was run.
- Full Windows/MSVC, ranged/PvP/summon/death/overkill and attack-mode gameplay
  acceptance remains incomplete. No sanitizer or race-freedom guarantee is made.

This is a focused, measured buffer-cost improvement with an explicit throughput
versus total CPU tradeoff, not a claim that the entire server is 100% optimized,
stable or ready for an arbitrary production population.

## 10. Merge recommendation

| Commit | Classification | Basis / release condition |
| --- | --- | --- |
| `ec144a66` — combat/fan-out/source-tail diagnostics | **SAFE** for opt-in diagnostic use | Disabled by default, bounded storage, focused counter/source/concurrency tests passed; enabling diagnostics adds overhead and histograms retain the documented limitations. |
| `40032c21` — bounded combat plaintext buffers | **NEEDS MORE TESTING** before production merge | Production encoders match legacy bytes/framing in focused tests and reduce measured serializer cost; complete Windows/gameplay acceptance and assessment of the TORTURE CPU tradeoff remain necessary. |
| This engineering report commit | **SAFE** as documentation | Records frozen artifacts, observed improvements, regressions and missing acceptance checks; does not assert a capacity guarantee. |

The branch as a production release is **NEEDS MORE TESTING**, not an automatic
merge recommendation. The serialization change is separate from metrics so it
can be reviewed independently. No speculative attack rejection, health
coalescing, geometry rewrite, reactor scheduling, pool enlargement or parallel
world-state mutation patch was added. None of the submitted changes is presented
as an experimental gameplay rewrite; proposals without evidence were left out.
