# Runtime statistics and spectator storage

These changes adapt generic performance improvements from the DragonKeep source
to upstream `ecd8ff9575e6dd4636448b8399abdbbf6f39c836`.

## Avoid work without a statistics consumer

Lua callbacks and dispatcher tasks only read their execution clocks when an
active statistics collector or the independent slow-callback warning needs the
duration. Lua callbacks also avoid constructing discarded `Stat` objects and
copying timer origins when neither consumer needs them. The SQL statistics
recorder checks for an active collector before copying query fragments or
allocating a record; SQL execution, its existing start clock and transaction
handling remain unchanged.

`AutoStat` skips its clock and record allocation when the collector is disabled
or stopped. Recursive scopes still measure children of a timed parent if the
collector changes state, preserving the parent's exclusive-time calculation.

Regression tests cover disabled/stopped/running collectors, nested scope state
changes, callback return values and errors, Lua stack restoration, timer origins,
active statistics output, task expiration/exclusion, and independent slow warnings.

Callbacks recheck whether collection is enabled and running immediately before
constructing a record. Four allocation regressions cover disabling and stopping
collection inside boolean and void callbacks. Each compares completion allocations
with an otherwise identical callback that entered with collection disabled; this
excludes unrelated Lua/database cleanup. All four fail with the original gate.

The recursive-scope regression parses the outer duration and call count written
to `special.log`. It checks the measured exclusive interval, including observed
destructor overhead and millisecond-format tolerance. Removing nested-time
subtraction makes this assertion fail.

## Allocate spectator storage when an entry is inserted

An empty `SpectatorVec` no longer reserves 32 entries. Its first insertion keeps
the previous initial capacity, including null insertion and merging from another
vector. Empty partition spans avoid arithmetic on a null storage pointer.

Partitioning an empty vector resets both partition boundaries and returns before
running the partition algorithms or computing iterator differences. Regressions
cover fresh empty vectors, implicit partitioning during filtering, erasing all
spectators, null-only storage, and filtering out every player. Each empty result
is checked through the mutable and const span accessors.

The empty-span case checks the accessor contract. It does not claim that null
pointer plus zero is undefined: C++ explicitly defines that result as null, and
Clang's pointer-overflow sanitizer excludes arithmetic where both pointers are
null. See [C++ additive operators](https://eel.is/c++draft/expr.add#4.1) and
[UBSan checks](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#available-checks).

The allocation regression executes 10,000 iterations, each constructing two
vectors, ignoring a null insertion and merging an empty source. A second case
checks construction with 32 actual entries and construction with a nonempty merge.
Allocation counting excludes the shared owner used to populate the vectors.

Measured on 2026-10-18 with MSVC 19.44, x64, `/O2`, using the same standalone
`test_spectator_allocations.cpp` against the original and modified headers:

| Operation | Original allocation calls | Modified allocation calls |
| --- | ---: | ---: |
| 10,000 empty iterations, two vectors per iteration | 20,000 | 0 |
| Construction and 32 insertions | 1 | 1 |
| Construction and nonempty merge | 1 | 1 |

The empty-allocation regression fails against the original header as expected;
both tests pass against the modified header. Machine-readable counts are in
[`runtime-overhead-allocations.json`](runtime-overhead-allocations.json).

These checks measure component behavior and allocation calls; they do not
establish player capacity or gameplay latency. SQL timings and a complete
server workload were not benchmarked for this contribution.
