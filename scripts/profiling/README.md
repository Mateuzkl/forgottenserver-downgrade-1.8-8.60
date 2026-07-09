# Profiling scripts

Helpers for Linux perf and Brendan Gregg FlameGraph.

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
