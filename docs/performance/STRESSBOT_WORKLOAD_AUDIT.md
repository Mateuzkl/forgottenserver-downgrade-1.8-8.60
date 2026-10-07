# TFS / StressBot workload and latency audit

Date: 2026-10-01. This report describes a synthetic, isolated local experiment,
not a production capacity guarantee. The server and bot changes are on local
branches `perf/realistic-load-latency` and `perf/realistic-workloads` respectively.

## Executive summary

The largest demonstrated defect was **attack retry amplification**. Failed
out-of-range swings did not advance `lastAttack`, but each independent check
could schedule another retry. Repeated target selection and normal creature
checks consequently started overlapping timer lineages. Under 1000 continuous
bots, an overloaded main thread accumulated tens of thousands of tasks.

The release candidate owns one independent attack retry per player. In the
1000-bot TORTURE experiment, maximum reactor backlog fell from **63,890 to
2,505**, mean process RSS from **1278.21 to 483.21 MiB**, and maximum observed
queue delay from **12,728.31 to 513.29 ms**. These are reductions of **96.08%,
62.20%, and 95.97%** respectively. CPU rose from **100.63% to 123.63%** of one
core: this is a latency/memory improvement, **not a CPU reduction**. Bot send
rates were close, while bytes delivered to bots increased. More useful progress
rather than suppressed output is a supported interpretation, not a measured
human-client latency result.

The follow and reactor-budget changes alone did not solve that overload. Their
intermediate TORTURE run still accumulated 91,340 tasks and required forced
termination after the shutdown timeout. REALISTIC results do not demonstrate
a general CPU reduction. Lowering artificial bot activity reduces offered load,
but must not be advertised as an optimization of the same server workload.

There were intermittent shutdown failures in both the baseline and attack-only
candidate. A baseline 1000-bot LOGIN_ONLY shutdown captured a stack through
`Item::~Item` erasing the global registry's `unordered_set`. Threaded login
constructs inventory items on workers while main-thread work also uses that
unprotected registry. A separate safety commit synchronizes all registry
operations; its benchmark matrix is listed separately below. This is a concrete
data-race fix, not proof that every historical heap failure has the same cause.
**Do not describe this branch as 100% stable or fully production-validated.**

## Measured root causes, in order of demonstrated impact

1. **Overlapping attack retry lineages (server):** queue and RSS growth under
   sustained, frequently unsuccessful melee checks. The one-owned-timer fix
   materially changes the overload result without changing damage formulas or
   nominal attack-speed thresholds.
2. **Continuous artificial action generation (bot):** independent loops and
   aggressive refresh intervals are a torture workload, not a mostly-idle player
   population. New explicit modes prevent an idle test from silently enabling
   combat/walking/cosmetics. REALISTIC alternates activities and resting periods.
3. **Combat and notification fan-out (server):** health/effect serialization,
   packet copying/clearing and recipient work remain hot after timer deduplication.
   Final Callgrind attribution includes `Game::combatChangeHealth`,
   `Creature::onAttacking`, `ProtocolGame::sendMagicEffect`, and memory operations.
   These overlapping inclusive costs are not additive CPU percentages.
4. **Unreliable bot world parsing (measurement):** incorrect skip handling,
   stack positions, item metadata and outfit counts invalidated earlier runs.
   Zero parser errors/unknown opcodes is now a prerequisite for accepting the
   measured packet stream. It is not proof of complete protocol coverage.
5. **Follow lifecycle / reactor budget accounting (server):** rejected or stale
   follow jobs could leave pending state inconsistent; preprocessing was outside
   the cycle budget. Corrected, but neither A* nor ready-task sorting was the
   dominant measured runtime cost in the final realistic arena.

An independent safety defect was also confirmed: global item registry reads,
inserts, erases and retirement lacked synchronization despite threaded inventory
loading. Shutdown crash location and code ownership analysis support fixing it;
they do not quantify a CPU saving or establish every crash's causal history.

The old `CALLGRIND_RUNTIME_HOTSPOTS.md` records a different supplied capture,
including startup. Its instruction counts must not be combined with this
runtime-reset experiment or the earlier exploratory CPU HTML.

## StressBot changes

| Change | Before | After | Reason / realism / benchmark effect |
| --- | --- | --- | --- |
| Protocol and world parsing (`39bd43a`) | Incorrect skip scope, incomplete metadata and mismatched message widths could desynchronize a payload or misclassify tiles. | Carry skips across floor/strip boundaries, track ordered tile things, load local OTB flags, consume the fork's short turn/ping/effect/outfit layouts, stop malformed/unknown payloads. | Reliable current workload observations; do not scan bytes to hide desynchronization. This changes the workload model, so old parser-broken runs are not A/B controls. |
| Workload modes (`afc6c17`) | Idle/active intent could depend on independent loops; aggressive defaults represented continuous activity. | Explicit LOGIN_ONLY, REALISTIC and TORTURE. REALISTIC always uses one serial brain; legacy independent loops are limited to TORTURE. | Separates connection capacity from active gameplay pressure. The new bot is used unchanged on both server revisions in the controlled matrix. |
| Seeded activity and pacing (`afc6c17`) | Cross-process schedule reproducibility was not assured. | Stable FNV-based identity seeds, independent RNG streams, configurable timings, seeded jitter, resting periods and opt-in cosmetics. | Reproducible offered schedules, not bit-identical network timing or combat outcomes. Weights select states rather than exact population percentages. |
| Send admission and session ownership (`af87937`) | Send backlog/attempt counts could obscure actual completed work; ping session tasks needed explicit ownership. | Bound replaceable requests, serialize writes, cap actual writes, drop stale replaceable requests before writing, await read-loop pongs in the session lifetime, use finite I/O timeouts. | No accumulating movement/target-refresh flood or reordered/partial encrypted frames. Critical cancellation, ping, text and inventory actions are not age-dropped. |
| Actual workload telemetry (`af87937`) | Attempted actions or combined text counters could misrepresent traffic. | Count completed socket writes by action, packet/byte rates, bot CPU/RSS, errors and fixed queue/send histograms. | Enables comparisons by offered load, not only bot count. Fixed histograms do not grow with traffic. |
| Tail and state labels (`268980c`) | Age-dropped waits were omitted from queue samples; TORTURE could display an unused REALISTIC schedule's initial states. | Include stale waits, distinguish stale/full rejections, expose means/maxima and packets/s/bot, label continuous activity explicitly. | Avoids flattering tail metrics or fictitious population distributions. These telemetry-only changes are not in the frozen bot used for the matrix. |

### Activity and send-path audit

The default REALISTIC brain ticks every 500-1000 ms, and TORTURE every
175-225 ms. REALISTIC chooses idle (5-60 s), walking (3-30 s), combat (5-45 s),
or social (5-15 s), with 2-15 s idle after combat. Healing remains possible
during rest. The defaults are selection weights 35/30/28/7, not simultaneous
population quotas. Production monster behavior can remain active even when
the bot sends no actions.

| Client action | Opcode | Timing / policy |
| --- | --- | --- |
| Autowalk / step / stop | `0x64`, `0x65..0x6D`, `0x69` | Brain movement uses `walkIntervalMs` and jitter. Walk counter counts a command, not each server step. Stop is critical, not replaceable. |
| Attack / cancel target | `0xA1` with target / target zero; `0xBE` helper | REALISTIC selects changed targets; refresh uses `attackScanIntervalMs` when needed. TORTURE intentionally refreshes continuously. Cancellation is not age-dropped. |
| Follow / fight modes | `0xA2`, `0xA0` | Follow helper exists; this matrix uses chase mode, not a continuous explicit follow loop. |
| Spell / heal / chat text | `0x96` | Respect configured slot cooldowns, target/range/mana checks, action exhaustion and `chatIntervalMs`; separate successful-write counters. Text is not age-dropped. |
| Potion / item use | `0x84`, `0x82` | Brain consumable checks; inventory actions are critical. Massive fixture health prevented potion/heal activation, so those paths are not end-to-end validated here. |
| Outfit request / update | `0xD2`, `0xD3` | Cosmetics opt-in and social-gated. Disabled in this matrix. |
| Turn | `0x6F..0x72` | Optional `idleTurnIntervalMs`; explicitly zero in this matrix. |
| Ping / logout | `0x1E`, `0x14` | Server-requested pong is awaited and not suppressed by optional heartbeat throttling. LOGIN_ONLY does not start periodic heartbeat/turn unless explicitly enabled. |

`queueSize=32` limits pending replaceable requests; `maxSendLagMsToDrop=1200`
rejects them before a write. Default cap is 18 packets/s/bot, with validated
maximum 20. A partially written frame is never intentionally discarded.
Critical sends still serialize and obey the cap; a blocked stream has a
5-second timeout. No packet coalescing or general priority system is claimed.

The world parser loads the server's external `items.otb` once (54,652 entries
in this fixture), preserving stack/fluid/blocking/top metadata. It does not
copy that asset into Git. Without OTB metadata, heuristics are explicitly
non-authoritative. Remaining gaps include viewport tile pruning, known-creature
removal bookkeeping, container layout branches and floor-transition edge cases.
Zero errors in this matrix do not make those gaps disappear.

## TFS changes and correctness reasoning

### `e8b8e152` - rejected/stale follow jobs

Files/functions: `src/tasks.*` (`Dispatcher::tryAddTask`),
`src/creature.*` (`requestFollowPathUpdate`, follow target/movement handling),
`src/player.cpp` (duplicate move-triggered refresh).

Before, pending follow state did not reliably track whether a task was actually
accepted or still belonged to the current target. A successful own step could
also trigger unnecessary route rebuilding. After, enqueue success is explicit,
weak ownership and a per-request generation reject stale callbacks, rejected
work leaves the request retryable, and an existing remaining route survives a
successful own step. Target movement, teleport/floor changes, an exhausted route
and forced refresh still request updates. The existing think/failure retry
mechanisms remain; no generic new no-path backoff or A* algorithm is introduced.

Risk: movement timing under fleeing, summons, blocked corridors, doors and rapid
target changes still needs scenario testing. Unit tests cover deduplication,
enqueue rejection, target supersession, stop-follow and own-step behavior, not
all gameplay pathfinding semantics.

### `a7f4272a` - preprocessing counts against reactor budget

File/functions: `src/reactor.*` (`runOnce`, `executeReadyTasks`).

Before, drain/sort time was excluded from the execution budget. After, the cycle
start precedes preprocessing. At least one ready callback still progresses,
and later ready tasks are deferred in normal order when the budget is consumed.
Cancellation and existing expiry semantics are preserved; no priorities, queue
limit, pool size or budget defaults changed. A long callback is not preemptible.
The regression test simulates preprocessing longer than the budget and verifies
progress, deferral and subsequent ordering.

Risk: work distribution across cycles changes at overload boundaries. This fix
alone did not eliminate the attack task amplification.

### `1034b5c1` - one independent player attack retry

File/functions: `src/player.*` (`scheduleAttackCheck`, `stopAttackCheck`,
`setAttackedCreature`, `doAttacking`, `maintainAttackFlow`).

Before, failed swings and same-target requests could establish parallel retry
lineages. After, an owned event ID/deadline retains the earliest pending check.
An earlier requested check replaces it; target cancellation/change invalidates
old generations, and weak callbacks cannot act on a removed/dead player or a
reused ID. Scheduler rejection leaves it retryable. Same-target selection still
passes base validation/chase logic but does not enqueue another immediate check.

Only independent retry branches changed. Shared `setNextActionTask` exhaustion
handling, weapon damage formulas, attack thresholds, successful-hit `lastAttack`
updates, movement rules and protocol 8.60 are unchanged.

Risk: interaction with classic attack speed, exhausted actions, ranged weapons,
stamina training, death and target switching requires broader manual regression.
Focused tests exercise actual `Player::doAttacking` with 100 failed out-of-range
fist swings, earliest-event retention/replacement/cancellation and scheduler
rejection/recovery. The fixture does not cover the full same-target setter path;
the benchmark exercises repeated actual attack packets.

### `ea1c1e66` - synchronize threaded item registry access

File/functions: `src/item.cpp` (`registerItem`, both constructors, destructor,
`isValidItemPointer`, `clearGlobalRegistry`); regression executable
`src/tests/test_item_registry.cpp`.

Before, threaded `ProtocolGame::login` called `IOLoginData::loadPlayerById` and
`loadItems`/`Item::CreateItem` on worker threads. Registry insertion could race
other worker insertions and main-thread lookup/destruction. The baseline GDB
stack reached `_M_find_before_node` / `_M_erase`, `Item::~Item`,
`Container::~Container`, `Player::~Player`, and `Game::shutdown`.

After, one immortal registry mutex protects every access to its membership and
`enabled` state, including retirement. Locks cover only registry operations,
not full construction, item callbacks or gameplay. Existing irreversible
retirement/static-lifetime semantics remain. Membership lookup still does not
pin an item's ownership after the lock is released; this is not a replacement
for shared ownership in asynchronous callbacks.

Regression tests exercise eight concurrent workers creating, copying, looking
up and destroying 102,400 items, plus retirement with live worker items. The
lock may add contention under heavy item creation, so measurements are repeated
using a separately frozen binary. No claim of race-detector coverage is made;
ASan/UBSan were not run, and a stress test alone cannot prove race freedom.

### Network, pool, movement, monsters and combat audit

Ingress remains bounded by the existing packet backlog admission and expiry
mechanisms. Measurement windows had no parser/unknown errors or reconnects and
no reactor drops; this is not proof under arbitrarily slow clients. Reactor
deferred counts include repeated deferral of the same task, not unique requests.
Expired counters were zero in the final 1000-bot TORTURE window.

The output pool remains **2048** retained entries. The attack-only TORTURE measurement
peak in-use count was 1755; window misses were 153. Earlier lifecycle peaks and
post-window values can differ. Receive-message allocation is separate from this
output pool. Neither pool enlargement nor a Boost.Asio migration is supported
by these measurements. Incoming/Lua `NetworkMessage` buffers remain zeroed.
The release-candidate TORTURE window had peak in-use 1862 and zero additional
pool misses. A retained-free-pool limit is not a cap on all live allocations.

Player-only item notification spectator optimization and initialized-output
safety fixes were already in the baseline `6891d487`; they cannot be credited
as new gains in this comparison. Ground/items and movement ordering were not
changed. Monster idle transitions, combat formulas and recipient semantics were
not weakened to reduce load. The arena's hostile monsters retained target-list
reasons for activity even during idle-bot runs, so forcing them idle would be an
incorrect optimization. No new spectator caching or area-combat target skipping
was introduced without evidence and gameplay tests.

## Reproduction controls

All controlled runs used the same frozen bot and external local OTB. Baseline
source was cleanly archived from Git, rather than built from the dirty live
checkout. Account positions, health/mana, conditions and login timing were reset
between runs, not every persisted skill/inventory field. This is not an exact
database snapshot replay; small CPU changes are not strong statistical evidence.

| Control | Value |
| --- | --- |
| CPU / OS | Intel i5-10300H, 4 physical / 8 logical cores; Ubuntu under WSL |
| Server build | GCC 13.3, Release `-O3 -DNDEBUG`, LTO, unity ON, native optimizations OFF |
| Diagnostics | Output-pool and packet-backlog diagnostics ON; ordinary Stats OFF |
| Main / network | Existing reactor configuration; 2 network threads |
| Before | Server `6891d487` |
| Intermediate | Server `a7f4272a` (follow + budget only) |
| Attack candidate | Server `1034b5c1` (adds owned attack retries) |
| Release candidate | Server `ea1c1e66` (also synchronizes the item registry) |
| Bot | Frozen `af87937` behavior/telemetry, before `268980c` |
| Private population | Synthetic accounts, 1000 hostile monsters, no NPCs |
| Map | Generated 252 x 102 flat arena, ground 106, accounts spaced five tiles apart |
| Other traffic | Controlled coin add/transform/remove every 2 seconds |
| Seed / login / cap | 180252 / 20 ms between logins / 18 writes/s/bot |
| Bot pacing | REALISTIC walk 1500 ms / scan 2000 ms; TORTURE walk 300 / scan 500; chat 30000; spell `exori` 6000; heartbeat 5000; idle turn 0 |
| Measurement | Full population, at least 20 s warm-up, then at least 60 s /proc samples |
| Safety | Separate synthetic database/runtime, loopback ports, isolated IP allowance 1100; normal ignored `config.lua` remained 100/IP |

SHA-256 hashes of the exact binaries:

```text
before:       370d280fbed6e222405d7f115676dcc06ddc7b54f9a3e6325a74a52d9b33ea65
intermediate: cb0989e84840316dbe7cd6a7874da5e7fe2d1d1b1e4efce33789a4df8984ed4a
attack:       28ba43109c313d551f42949d116f92bae0801d0e374e87834091f4d369790baa
release:      307a8e5baade75f7419eb3b6690fb0cdaee08ed7ead24e861e83dc2041f2f33a
frozen bot:   7187abee663f5aa1fe93eb1ce21da0414a809d5960f23aefec59bf74968e19df
```

CPU is process CPU time divided by elapsed time: **100% = one logical core**.
RSS includes only TFS, not MariaDB, the Windows bot or the WSL VM. Bot endpoint
counters are logged once a second, so computed packet/action rates are approximate.
Server diagnostics use five-second reporting windows that can straddle the
measurement boundaries. Reported p50/p95/p99 are the **worst interval histogram
upper bounds**, not exact pooled percentiles. Maximum delay is the maximum
observed sample; bucket bounds may exceed that maximum. Means from summed
diagnostic calls are scope wall time, not exclusive CPU time.

The idle cases still contain hostile monsters and item traffic, and the
300-bot cases also contain 1000 monsters. These are controls, not empty-town
idle capacity or the proposed smaller-monster production scenario. Fixture
health/mana of one million prevents death; potion/heal behavior was inactive.
The synthetic item event loops over 1000 positions in one callback every two
seconds, creating, transforming and removing a coin at each. This deliberate
fan-out fixture can itself produce non-preemptible global-event spikes. It is
not a natural production arrival distribution or an isolated combat benchmark.

## Controlled before/after results

Before = `6891d487`; attack candidate = `1034b5c1`; release candidate =
`ea1c1e66`. Every listed CPU window completed at full population. Exit codes
describe the subsequent shutdown, not measurement success. A missing early
observer is shown as unavailable, not zero latency. All listed bot streams had
zero parser errors, unknown opcodes, failures, disconnects and reconnects.

| Revision / case | Bots | CPU % | Mean RSS MiB | Actions/s/bot | Backlog max | Queue p99 bound ms | Queue max ms | Exit |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Before LOGIN_ONLY | 600 | 23.14 | 303.08 | 0.000 | 272 | 67.11 | 58.50 | 0 |
| Attack LOGIN_ONLY | 600 | 23.10 | 302.85 | 0.000 | 276 | 16.78 | 60.91 | 0 |
| Before LOGIN_ONLY | 1000 | 36.86 | 390.66 | 0.000 | 425 | 67.11 | 101.40 | -11 |
| Attack LOGIN_ONLY | 1000 | 36.67 | 391.84 | 0.000 | 420 | 33.55 | 97.93 | 0 |
| Before REALISTIC | 300 | 52.83 | 254.65 | 0.161 | 304 | 8.39 | 36.13 | 0 |
| Attack REALISTIC | 300 | 52.95 | 258.78 | 0.159 | 311 | 8.39 | 49.58 | 0 |
| Before REALISTIC (a) | 600 | 101.34 | 320.05 | 0.167 | unavailable | unavailable | unavailable | 0 |
| Before REALISTIC (b) | 600 | 101.39 | 336.81 | 0.171 | 546 | 67.11 | 67.54 | -11 |
| Attack REALISTIC | 600 | 104.13 | 337.84 | 0.171 | 548 | 67.11 | 100.60 | 0 |
| Before REALISTIC | 1000 | 171.03 | 423.87 | 0.173 | 860 | 134.22 | 145.88 | 0 |
| Attack REALISTIC | 1000 | 165.76 | 419.87 | 0.172 | 834 | 134.22 | 145.55 | -6 |
| Attack REALISTIC (repeat/GDB) | 1000 | 162.21 | 422.33 | 0.173 | 843 | 134.22 | 144.56 | 0 |
| Before TORTURE | 1000 | 100.63 | 1278.21 | 1.906 | 63890 | 17179.87 | 12728.31 | 0 |
| Attack TORTURE | 1000 | 120.11 | 478.93 | 1.872 | 2447 | 1073.74 | 608.38 | 0 |
| Release LOGIN_ONLY | 600 | 23.33 | 303.16 | 0.000 | 274 | 67.11 | 60.70 | 0 |
| Release LOGIN_ONLY | 1000 | 37.23 | 389.16 | 0.000 | 413 | 33.55 | 281.02 | 0 |
| Release REALISTIC | 300 | 53.18 | 258.47 | 0.162 | 299 | 33.55 | 53.04 | 0 |
| Release REALISTIC | 600 | 103.57 | 332.21 | 0.170 | 542 | 67.11 | 84.10 | 0 |
| Release REALISTIC | 1000 | 168.68 | 425.92 | 0.173 | 820 | 134.22 | 155.56 | 0 |
| Release TORTURE | 1000 | 123.63 | 483.21 | 1.890 | 2505 | 536.87 | 513.29 | 0 |

| Scenario / metric | Before | Release candidate | Delta |
| --- | ---: | ---: | ---: |
| 600 LOGIN_ONLY CPU % | 23.14 | 23.33 | +0.80% |
| 1000 LOGIN_ONLY CPU % | 36.86 | 37.23 | +1.03% |
| 300 REALISTIC CPU % | 52.83 | 53.18 | +0.66% |
| 600 REALISTIC CPU % (before b) | 101.39 | 103.57 | +2.15% |
| 1000 REALISTIC CPU % | 171.03 | 168.68 | -1.37% |
| 1000 TORTURE CPU % | 100.63 | 123.63 | +22.86% |
| 1000 TORTURE mean RSS MiB | 1278.21 | 483.21 | -62.20% |
| 1000 TORTURE backlog maximum | 63890 | 2505 | -96.08% |
| 1000 TORTURE queue maximum ms | 12728.31 | 513.29 | -95.97% |

Relative deltas use unrounded JSON values. These single-case CPU differences
do not establish statistical significance. In particular, the 1.37% realistic
decrease cannot be generalized while other scenarios increased. The final
1000-bot idle maximum-delay outlier (281.02 ms) is not hidden by its lower
histogram p99 bound. All six release-candidate cases subsequently exited
normally under the post-measurement GDB shutdown observer. This is encouraging
regression evidence after the registry fix, not a production stability proof.

Additional 1000-bot TORTURE diagnostic detail (12 five-second reports):

| Scope / revision | Mean ms | Worst p50 bound ms | Worst p95 bound ms | Worst p99 bound ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Reactor queue / before | 5021.94 | 17179.87 | 17179.87 | 17179.87 | 12728.31 |
| Reactor queue / release | 218.51 | 536.87 | 536.87 | 536.87 | 513.29 |
| Callback / before | 0.163 | 0.016 | 1.049 | 2.097 | 154.13 |
| Callback / release | 0.200 | 0.016 | 1.049 | 2.097 | 178.60 |

Queue statistics cover reactor tasks, not a one-to-one network packet latency
measurement. Callback tail did not improve universally, despite much less queued
work. Deferred counts were 28,683,395 / 905,469, with zero reactor drops in both
windows. Path requests were 3501 / 26413, failures 0 / 9, visited nodes 7824 /
23575. More path requests in the release case reflect more server progress,
not a claim that pathfinding became faster. Pool misses during the windows were
4976 / 0; peak in-use entries were 1782 / 1862. Retained-pool policy was unchanged.

### Intermediate results: not the final fix

| Mode / bots | CPU % | Mean RSS MiB | Backlog max | Queue p99 bound ms | Queue max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| LOGIN_ONLY / 600 | 22.32 | 297.05 | 266 | 16.78 | 53.42 |
| LOGIN_ONLY / 1000 | 35.76 | 376.43 | 420 | 33.55 | 108.50 |
| REALISTIC / 300 | 51.17 | 260.36 | 322 | 33.55 | 120.93 |
| REALISTIC / 600 | 104.61 | 338.40 | 593 | 134.22 | 159.33 |
| REALISTIC / 1000 | 169.00 | 415.57 | 853 | 134.22 | 122.53 |
| TORTURE / 300 | 119.44 | 273.29 | 812 | 67.11 | 66.68 |
| TORTURE / 1000 | 105.98 | 1847.68 | 91340 | 34359.74 | 22072.92 |

The intermediate 1000-bot TORTURE process required SIGKILL after 30 seconds of
shutdown waiting. Separate early TORTURE trials did not complete the full
60-second window and are excluded. Partial/later-started observers are not used
as full-window latency controls. Initial parser-broken runs and the old 125.22%
exploratory CPU figure are not included in matched before/after comparisons.

### What active vs idle means

The intermediate 300-bot TORTURE run generated **1.857 actions/s/bot** and used
**119.44% CPU**, while its 600-bot LOGIN_ONLY run generated **zero gameplay
actions** and used **22.32% CPU**. That is **5.35 times the process CPU with half
the logged-in population**. The attack-only 300-bot REALISTIC run used 52.95% CPU at
0.159 actions/s/bot. Those are different offered workloads, not proof that one
code change reduced CPU by that ratio.

Continuous bots drive combat, conditions, pursuit, movement, health/effect
notifications and per-recipient serialization. Mostly-idle connections still
receive pings/nearby events but do not impose the same workload. Thus bot count
alone is not an estimate of real-player capacity. This experiment has no direct
Astra ping measurement and does not reproduce every cause of the reported human
client lag.

The before/release TORTURE send rates were 2104.92 / 2097.10 packets/s and
1.906 / 1.890 actions/s/bot. Bytes received by bots rose from 4.47 to 5.49 MiB/s.
Main-thread CPU was 85.06% / 84.32%, while total process CPU was
100.63% / 123.63%; the increase was predominantly outside the main thread.
These observations support increased delivery/progress despite similar offered
traffic. They do not establish a universal throughput improvement or a ping SLA.

## Runtime Callgrind findings

Callgrind 3.22 ran separately from all CPU measurements. Collection started
only after the server was ready, 100 TORTURE bots were in-world and a 20-second
warm-up completed. Instrumentation was enabled, counters reset, and a runtime
dump collected after approximately 10 seconds. The profiles retained 1000
arena monsters. Startup is excluded from the named part-1 dumps.

| Hot path (inclusive instructions) | Before | Intermediate | Attack candidate |
| --- | ---: | ---: | ---: |
| Program total Ir | 926268356 | 987015734 | 579149593 |
| Reactor ready execution | 789901752 | 803999514 | 453708972 |
| `Creature::onAttacking` | 233345998 | 245907671 | 228782452 |
| `Player::doAttacking` | 230952127 | 244198224 | 218601200 |
| `memset` | 288038003 | 285729783 | 366527212 |
| Ready introsort | 353453905 | 355231302 | Not in candidate top list |
| Heap pop | 256608699 | 253631243 | Not in candidate top list |

The attack-candidate profile also shows `Game::combatChangeHealth` at 269849147 inclusive
instructions, `sendMagicEffect` at 126420678, and `memcpy` at 90070786. The
exclusive `memset` count is approximately 63.29% of attack-candidate Ir. Caller attribution
includes unresolved/LTO-merged frames, so it does **not** justify removing all
message initialization or declaring a precise exclusive gameplay function share.

Instruction percentages/counts overlap under parents and are not elapsed CPU
percentages. The profiler changes progress and packet reception; totals are not
normalized to equal completed actions. **Do not advertise the difference in
total Ir as a controlled speedup.** GCC Release/LTO/unity retain imperfect
function attribution, not the recommended complete `-O2 -g` source-line build.
Hardware perf/flamegraph collection was unavailable in this WSL kernel setup.
This capture predates the registry-lock commit; it is not a profile of the
latest release binary.

Attack-candidate 1000-bot REALISTIC instrumentation continues to show combat much larger
than path search in this arena. A* rewriting, SIMD and pool enlargement are not
evidence-supported first fixes. Packet fan-out and memory clearing/copying are
better next profiling candidates, subject to bounds/initialization safety tests.

## Validation and merge recommendation

Validation performed:

- Server Release build completed; five focused CTest targets passed:
  `test_creature_walk`, `test_reactor`, `test_outputmessage`,
  `test_protocolgame_pipeline`, `test_item_registry` (5/5).
- StressBot .NET 10 SDK targeting net8 Release built with zero warnings/errors.
  Its `--self-test` passed, including seeded schedules, packet classification,
  parser boundaries, OTB decoding and actual send admission/stale wait behavior.
- C++ changed regions formatted with clang-format; bot whitespace formatted;
  `git diff --check` passed for task changes.
- Runtime matrix and independent Callgrind captures completed. No ASan/UBSan
  was run, per the explicit user constraint. This is not the full CTest suite,
  Windows server build or full gameplay scenario matrix.
- User XML/world modifications and normal configuration were preserved.
  Private configs/accounts, XMLs, worlds, dumps, binaries and large logs are not
  staged. No force push is required.

| Commit | Classification | Reason |
| --- | --- | --- |
| Server `e8b8e152` | NEEDS MORE TESTING | Focused lifecycle tests pass; blocked/no-path, fleeing, summons and rapid floor/target transitions need gameplay regression. |
| Server `a7f4272a` | SAFE | Small accounting correction with progress/order regression coverage; cannot preempt a slow callback. |
| Server `1034b5c1` | NEEDS MORE TESTING | Strong overload evidence and production failed-swing test; attack/exhaustion configurations need broader regression. |
| Server `ea1c1e66` | SAFE | All shared registry accesses use one small lock; concurrent lifetime/retirement tests pass. Not a guarantee against unrelated item-ownership defects. |
| Bot `39bd43a` | NEEDS MORE TESTING | Zero parse errors in measured stream; containers/floor edges and viewport/known-creature lifecycle are not fully covered. |
| Bot `afc6c17` | SAFE | Explicit workload choices and seed tests; REALISTIC is a synthetic approximation, not a population capacity claim. |
| Bot `af87937` | NEEDS MORE TESTING | Actual admission tests pass; deliberately slow/partial writes, reconnect and prolonged critical-stream saturation need extended tests. |
| Bot `268980c` | SAFE | Telemetry accounting/labels only; actual stale-wait regression test passes. |
| Bot `350bca3` | SAFE | Comments only; aligns documented pacing with the implemented modes. |
| This report | SAFE | Documentation only; raw private artifacts are excluded from Git. |

Do not merge the whole server branch as a fully proven production performance
release yet. The registry race has a focused fix, but long-running ownership
regression and the remaining gameplay scenarios are needed. The controlled throughput/latency result is
valuable independently of that missing validation.

### Remaining work, not hidden by this report

- Earlier shutdown corruption: baseline REALISTIC 600 exited SIGSEGV after
  measurement; attack-only REALISTIC 1000 exited SIGABRT (`free(): invalid size`) during
  shutdown. Baseline LOGIN_ONLY 1000 also crashed with the captured item-registry
  stack. The registry synchronization fixes a verified race in that path;
  the historical SIGABRT was not captured and is not conclusively attributed.
  Do not extrapolate normal exits to an unconditional stability guarantee.
- Queue tails in 1000-bot TORTURE still exceed 600 ms at the observed maximum;
  individual long Lua/global-event callbacks remain non-preemptible.
- No new general failed-path backoff, bounded spectator-cache redesign, idle
  monster shortcut, or combat formula change was introduced.
- Out-of-viewport world/cache pruning and known-creature removal need dedicated
  packet fixtures before claiming fully faithful long-running bot navigation.
- Test classic/nonclassic attack speed, exhaustion on/off, ranged/interruptible
  weapons, target replacement, death/logout, summons, fleeing, stairs, doors,
  teleports, dense blocked corridors and no-path retry recovery.
- Repeat on production-equivalent maps/monster scripts with matched workload,
  longer steady windows and actual Astra ping measurements. The synthetic arena
  is not a replacement for those measurements.

## Local artifacts (outside Git)

The workspace sibling `atlas-performance-results-20261001` contains
`controlled-results.json`, `stressbot-performance-report.html`, per-run
`*-result.json` / `*-telemetry.json`, server/bot logs and the private harness.
Callgrind dumps are in WSL `/home/mateus/atlas-perf-profiles-20261001`.
They are deliberately excluded from commits. The HTML is self-contained and
labels CPU, workload differences and shutdown failures explicitly.
