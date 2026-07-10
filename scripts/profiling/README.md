# Profiling scripts

Helpers for Linux perf and Brendan Gregg FlameGraph.

For the default `tfs` process, run:

```bash
bash tools/run-flamegraph.sh
```

If `tfs` is not already running, the wrapper starts `build-release/tfs`, waits
for the server to become online, captures it, and stops that instance when the
capture finishes. An existing `tfs` process is only attached to and remains
running.

The wrapper selects a compatible versioned `perf` binary on WSL when the
`/usr/bin/perf` launcher does not support the running kernel. Extra capture
options are forwarded to `capture_flamegraph.sh`.

For a Callgrind-friendly build with separate translation units and debug
symbols, run:

```bash
bash tools/build-callgrind.sh
valgrind --tool=callgrind --callgrind-out-file=callgrind.out.%p ./tfs-valgrind
```

Start here:

```bash
./scripts/profiling/setup_flamegraph.sh
```

Then capture a full flamegraph:

```bash
./scripts/profiling/capture_flamegraph.sh \
  --process crystalserver \
  --duration 60 \
  --title "TFS before getSpectators" \
  --output profiling-output/tfs-before.svg
```

See `docs/performance/flamegraph.md` for the full PT-BR guide, including
filtered spectator flamegraphs and before/after comparison notes.
