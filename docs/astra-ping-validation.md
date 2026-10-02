# Astra ping telemetry validation summary

Base: PR #318 at `ff7600fa`. Branch: `feat/astra-ping-telemetry`.
The existing performance changes and ItemRegistry lifetime pin are retained.
See [astra-ping-telemetry.md](astra-ping-telemetry.md) for protocol/clock semantics.

The Linux Release server builds successfully with GCC 13.3, unity and LTO.
`test_astra_ping` passes four cases. The following focused selection passes 5/5:

```sh
ctest --test-dir /home/mateus/atlas-perf-20261001-gcc --output-on-failure \
  -R 'astra_ping|spell_cooldown|protocolgame_pipeline|custom_ping_tracker|combat_packets'
```

The Astra desktop client builds with C++23 on Windows (MSVC 19.51/v145,
DirectX x64) and Linux (GCC 13.3/LuaJIT). Tracker/decoder and Lua UI tests pass.
No ASan, UBSan or ThreadSanitizer run was performed.

## Isolated stress measurements

Intel i5-10300H, WSL2, two network threads, synthetic database/arena only.
Each final window keeps the full requested bot population online after a
20-second warm-up and lasts at least 60 seconds. One actual Astra client samples
RTT and negotiated queue under Xvfb/software OpenGL. CPU 100% means **one core**.
No production world, normal config, XML or existing executable is overwritten.

| Scenario | Total CPU % | Main CPU % | Network CPU % | Client RTT mean / max ms | Ping queue mean / max ms | Reactor queue p95 / p99 / max ms |
| --- | ---: | ---: | ---: | --- | --- | --- |
| LOGIN_ONLY 600 | 23.58 | 8.04 | 15.56 | 10.10 / 16 | 0.00 / 0 | 2.10 / 16.78 / 74.03 |
| REALISTIC 300 | 50.21 | 20.09 | 30.12 | 10.03 / 37 | 1.27 / 24 | 8.39 / 33.55 / 55.11 |
| REALISTIC 600 | 101.84 | 38.92 | 62.89 | 9.77 / 22 | 0.69 / 6 | 33.55 / 134.22 / 123.94 |
| REALISTIC 1000 | 161.50 | 64.44 | 97.03 | 22.58 / 101 | 5.57 / 45 | 134.22 / 536.87 / 375.24 |
| TORTURE 1000 | 132.75 | 79.33 | 53.39 | 230.56 / 559 | 144.67 / 311 | 536.87 / 536.87 / 432.14 |

All final cases have zero bot disconnects/parser errors/unknown opcodes,
zero client ping timeouts/unknown/duplicate replies and zero Reactor task drops.
Server and observer exit with code 0. Smoothed RTT/jitter means are 13.34/6.81 ms
for REALISTIC 600 and 242.38/80.20 ms for TORTURE 1000. Corresponding bot packet
rates to/from server are 239.62/14059.99 and 2097.39/7199.95 per second.
Successful weapon/fist execution rates are approximately 624.59 and 1959.70/s,
using existing interval counters rather than sent attack requests.

Client values are approximately one-second snapshots and may miss shorter
spikes. Reactor p95/p99 are worst five-second histogram **upper bounds**, not
exact whole-run quantiles; bucket bounds can exceed observed maxima.
Per-ping queue is a different population from Reactor callbacks. A measured
sub-millisecond queue displays 0 ms; unsupported telemetry is unavailable, not zero.

The same new client also works with the frozen legacy `combat-packet-after`
reference: REALISTIC 600 CPU 104.48%, RTT mean 11.27 ms; TORTURE 1000 CPU 134.66%,
RTT mean 226.43 ms. Queue remains unavailable. The reference is not asserted to
be the exact `ff7600fa` parent; these single-window differences are not proof of
a performance gain. TORTURE RTT is slightly higher with telemetry, despite
slightly lower measured server CPU. Existing Reactor tails remain substantial.

## Evidence and limitations

Full tables, API/consumer audit, exact build commands, binary hashes, failed
attempts and repeat explanations are in AstraClient's `docs/ping-validation.md`
and `docs/ping-latency.md`. Raw artifacts remain outside both repositories in the
local `atlas-performance-results-20261001` directory. Final labels are:

```text
ping-v2-login-600
ping-v1-realistic-300
ping-v1-realistic-600
ping-v1-realistic-1000
ping-v2-torture-1000
ping-legacy-reference-realistic-600
ping-v2-legacy-reference-torture-1000
```

Earlier setup attempts had no real-client samples due to private mount/profile
issues. Initial LOGIN_ONLY/TORTURE windows had valid measurements but observer
teardown failures after the window; those cases were repeated after correcting
the direct-login fixture's UI lifecycle. Their original logs/numbers are retained,
not silently promoted to successful validation.

This is not a 4,000 telemetry-pings/s benchmark, a production capacity guarantee,
or Android/WASM validation. RTT is not pure Internet latency; dispatcher queue
is not total server processing, and subtracting it does not measure network RTT.
