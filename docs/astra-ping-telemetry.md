# Astra ping queue telemetry

This work is based on PR #318's `perf/realistic-load-latency` branch at
`ff7600fa`, including the ItemRegistry lifetime pin. It does not replace or
rebase that work onto main.

The authenticated Astra login capabilities byte adds bit 7 (`0x80`,
`AstraClient::PingTelemetry`). Only an Astra connection advertising that bit
receives feature 153 (`GameFeature::AstraPingTelemetry`) in `sendFeatures`.
The response extension requires Astra identity, the capability and feature
advertisement. Legacy OTCv8/Mehah/Fonticak are not opted into the extension.

All integers retain existing little-endian serialization:

```text
legacy request:     40 + u32 id + u16 previous RTT + u16 FPS (9 bytes)
legacy response:    40 + u32 id                             (5 bytes)
telemetry request:  identical legacy request               (9 bytes)
telemetry response: 40 + u32 id + u32 queueMicroseconds     (9 bytes)

example request:    40 78 56 34 12 0F 00 3C 00
legacy response:    40 78 56 34 12
telemetry response: 40 78 56 34 12 E8 03 00 00   (1,000 us queue)
```

The historical OTCv8 local RTT/FPS fields remain intentionally ignored.
Requests shorter than a complete u32 ID are rejected without reading or replying.
Normal, spy and spectator paths retain their existing dispatcher guards.

`parsePacket` captures `steady_clock::now()` for opcode 0x40 before admission and
copy, without reading dispatcher-owned capability/player state. The timestamp
travels by value in the already-existing task. The delay ends at entry to
`parsePacketOnDispatcher`, before subsequent handler work. Negative deltas clamp
to zero and excessive delays saturate at UINT32_MAX microseconds. Non-ping packets
do not require additional clock reads. No new per-ping allocation or mutex is
introduced. `PacketBuffer<9>` serializes either the 5- or 9-byte response.

Admission, flood disconnects, RAII backlog tickets, queued-packet expiration,
connection-expiry checks, player checks and shutdown checks are unchanged. The
ping is **not echoed on ASIO**. Astra's RTT consequently retains dispatcher
backlog as part of gameplay responsiveness.

The queue metric includes admission/copy/enqueue and waiting, but not total
server processing/output time. RTT is not Internet latency, and RTT minus this
queue measurement is not a guaranteed transport RTT. Per-ping samples cannot be
equated to whole-window Reactor p95/p99 distributions.

Production-path packet tests in `test_astra_ping` cover exact legacy/negotiated
bytes, unadvertised capabilities, non-Astra clients, measured queue propagation,
clamping, short input and normal/spy/spectator/stopped/no-player paths.
Existing `test_protocolgame_pipeline`, `test_custom_ping_tracker`,
`test_combat_packets` and `test_spell_cooldown` remain unmodified.

See the client `docs/ping-latency.md` for bounded RTT tracking, smoothing and Lua
APIs. Stress validation is reported separately; no performance or production
capacity guarantee follows from this instrumentation change.
