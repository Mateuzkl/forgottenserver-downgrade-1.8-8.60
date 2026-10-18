# Dispatcher admission and reactor cycle work

This contribution adapts the remaining dispatcher/reactor work from the local
DragonKeep source to upstream `ecd8ff9575e6dd4636448b8399abdbbf6f39c836`.
It is independent of the statistics/spectator changes in PR #332.

## Dispatcher

Task descriptions and source locations are constructed when performance
telemetry, slow-task warnings or enabled statistics consume them. Statistics
enabled before its worker starts still retains startup metadata. The common
unobserved path avoids constructing the source-location strings.
Task and scheduler factories accept `std::string_view`, so macro literals and
borrowed inputs do not become temporary owned strings before the consumer check.
The task owns copies when metadata is observed, including scheduler tasks.

`Dispatcher::addTask` returns whether submission was accepted. An expired task
is rejected before publication. A task with an expiration uses the reactor's
expiring-work admission path, including overload rejection; `setDontExpire()`
continues to select lossless admission. The original Task deadline is checked
again before invoking its callback, so rounding the reactor's millisecond
deadline cannot make an expired Task run.

## Reactor

- Reuse ready, immediate, scheduled and cancellation scratch storage. Executing
  or discarding a ready callback clears its capture immediately; moved captures
  are not retained in ingestion scratch. Ingestion scratch larger than 8,192
  entries is released after a burst.
- Admit up to 1,024 new tasks into the owner heap per cycle by default; zero
  disables this ingestion limit. Cancellation control traffic is drained every
  cycle. Existing callback-count and cooperative time budgets remain available.
  Cancelled and expired due entries also consume the per-cycle task count.
- Deduplicate cancellations from their first admission until the scheduled task
  retires. Repeating a cancellation after ingestion adds no control work, and
  retirement clears cancellation state before an identifier can be reused.
- Merge both incoming task types into the persistent owner heap, with batched
  heap construction and amortized capacity growth. This removes the additional
  ready-list sort and leaves deferred callbacks in the heap.
- Include ingestion/heap work in the cycle's cooperative time budget while
  guaranteeing progress through at least one eligible callback.
- Observe heap size through an atomic snapshot instead of concurrently reading
  the owner's vector from a different thread.
- Keep transferred inbox entries in the pending count until heap publication.
  Publishing the new heap size and clearing that transfer count use the producer
  mutex together, so expiring admission cannot observe a gap between the queues.

The upstream-compatible callback wrapper is preserved, including its fallback
for toolchains without `std::move_only_function`. Both the class default and the
startup fallback when `reactorMaxInboxSize` is omitted are 100,000. An explicit
configuration overrides that value; `config.lua.dist` selects the high-load
preset of 400,000. At the configured threshold, expiring work can be
rejected, while authoritative work without a deadline, scheduled events and
cancellations remain accepted. This is not a hard memory limit for a sustained
producer of lossless work.

## Order during bounded ingestion

A FIFO scheduled inbox is insufficient when later submissions have earlier
deadlines. The adapted scheduled inbox is a producer-side heap. Immediate
timestamps and sequence numbers are assigned under the producer mutex, so the
immediate deque retains admission order.

After bounded ingestion, the owner remembers the earliest undrained task from
that snapshot. A ready callback cannot overtake that task. Ordering uses
`(fireAt, sequence)`, including equal-deadline cases. Work submitted concurrently
after the snapshot remains eligible on a later cycle, as with the previous
inbox-swap implementation.

The time budget is cooperative: a single callback cannot be preempted, and
cancellation/retirement cleanup can exceed the configured duration. Functional
stress tests do not establish a hard maximum cycle time or gameplay p95/p99.

## Native allocation probe

`test_dispatcher_admission --allocation-probe` counts calling-thread C++
allocations. The factory comparison uses the complete original upstream factory
body and the optimized factory in the same executable, both constructing real
`Task` objects. The description and origin inputs are prepared before counting.
It also counts 10,000 idle reactor cycles after warmup. These are component
allocation counts, not an end-to-end server or SQL benchmark.

On 2026-10-18, MSVC 19.44 x64 Release with statistics compiled and test LTO
disabled produced these counts:

| Component, 10,000 iterations | Original factory body | Optimized |
| --- | ---: | ---: |
| Task construction with unobserved description/origin | 30,000 | 10,000 |
| Idle reactor cycles after warmup | Not measured | 0 |

All 18 selected CTest checks passed, including the native 200,000-task concurrent
producer stress and 250,000 scheduled-task fairness stress. These are tasks,
not player sessions. The original Windows connection-write fixture also exposed
an accept-completion race on the previous engine and the candidate. It now
establishes its TCP pair synchronously and releases connection-manager ownership
before destroying its I/O context; six fresh processes passed with that fixture.

Review regressions exercise repeated cancellation before and after ingestion,
bounded discard of cancelled/expired tasks, allocations through the task macros,
scheduler metadata ownership, and real configuration loading. The preprocessing
budget test calls `runOnce()` and delays a test-only ingestion allocation. A
separate allocation hook lets an actual producer submit while the owner heap is
being constructed, making the inbox-to-heap admission window reproducible.
Starting the budget clock after preprocessing makes the real `runOnce()` test
fail, as verified by a temporary mutation of the isolated engine.

[Machine-readable results](dispatcher-reactor-results.json) record the component
counts and validation scope. No production runtime or datapack was changed.
