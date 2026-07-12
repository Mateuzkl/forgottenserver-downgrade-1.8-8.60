# Metrics (OpenTelemetry)

By default, no metrics are collected or exported. The server must be built
with the metrics feature and the exporters enabled in `config.lua`.

## Building with metrics

```bash
./build.sh --metrics on
```

This builds prometheus-cpp and opentelemetry-cpp (with `WITH_STL=CXX23`) into
`~/.local` and configures CMake with `-DENABLE_METRICS=ON`. With vcpkg, use the
`metrics` manifest feature instead (`-DENABLE_METRICS=ON` selects it).

## Prometheus exporter

```lua
-- config.lua
metricsEnablePrometheus = true
metricsPrometheusAddress = "0.0.0.0:9464"
```

This exposes a Prometheus endpoint at `http://localhost:9464/metrics`. You
still need a Prometheus instance scraping that endpoint.

The batteries-included way is the `docker-compose.yml` in this directory:

```bash
cd metrics
docker compose up -d
```

It starts a Prometheus instance (port 9090) already scraping the TFS endpoint
every 5 seconds, plus a Grafana instance at <http://localhost:3000> with the
Prometheus data source preconfigured. Default Grafana credentials are
`admin` / `admin` (you will be prompted to change the password on first
login).

## OStream exporter

```lua
metricsEnableOstream = true
metricsOstreamInterval = 1000
```

Dumps every metric to the server output on the given interval. Debugging
only — do not enable in production.

## Exported series

All series are pull-based: they read pre-aggregated relaxed atomics at scrape
time, so game hot paths never call into OpenTelemetry.

| Series | Content | Requires |
| --- | --- | --- |
| `tfs_creature_walk_events_total{state}` | scheduled / executed / cancelled / stale / rejected_* | `creatureSchedulerMetrics = true` |
| `tfs_creature_walk_pending` | walk events scheduled but not yet resolved | idem |
| `tfs_creature_walk_delay_microseconds{quantile}` | p50/p95/p99/max callback delay | idem |
| `tfs_creature_walk_delay_sum_microseconds` / `_count` | for rate-based averages | idem |
| `tfs_follow_updates_total{state}` | requested / coalesced_* / executed / stale | idem |
| `tfs_move_spectators_total{stat}` | moves, spectator and player fan-out sums | idem |
| `tfs_network_movement_packets_total` / `_bytes_total` | movement traffic (bytes are a lower bound) | idem |
| `tfs_reactor{stat}` | queue_current / queue_max / deferred / expired / dropped | `performanceMetricsEnabled = true` |
| `tfs_dispatcher_total{stat}` | tasks_processed / slow_tasks | always |
| `tfs_path_total{stat}` | pathfinding requests / reuse / failures / nodes | `performanceMetricsEnabled = true` |
| `tfs_method_calls_total{method}` / `tfs_method_duration_nanoseconds_total{method}` / `tfs_method_max_nanoseconds{method}` | per-method latency (`Game::checkCreatureWalk`, `Map::getPathMatching`, ...) | `performanceMetricsEnabled = true` |
| `tfs_online{type}` | players / monsters / npcs online | always |

Useful Grafana queries:

```promql
# stale walk callbacks per second (should be ~0 after the reactor fix)
rate(tfs_creature_walk_events_total{state="stale"}[1m])

# average Game::checkCreatureWalk latency
rate(tfs_method_duration_nanoseconds_total{method="Game::checkCreatureWalk"}[1m])
  / rate(tfs_method_calls_total{method="Game::checkCreatureWalk"}[1m])

# path reuse ratio
rate(tfs_path_total{stat="reused"}[5m])
  / (rate(tfs_path_total{stat="reused"}[5m]) + rate(tfs_path_total{stat="requests"}[5m]))
```

For an in-game snapshot of the same counters, use the `/creaturestats`
talkaction.

If the server runs inside Docker as well, put it on the same compose network
and change the scrape target in `prometheus.yml` accordingly; the default
`host.docker.internal:9464` assumes tfs runs on the host.
