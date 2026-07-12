# PR 172 regression audit

The audit used the local `origin/main...perf/port-crystal-death-events-core` range only. The range contained 61 changed files before this work and included reactor, follow path, death-event, bestiary, influenced-spawn and Astra protocol fixes.

## Preserved invariants

- Tibia protocol 8.60 packet field order is unchanged.
- Existing custom `0x2B`, `0x2C`, `0xD1` and `0xCF` layouts remain parseable.
- Boss delta is represented as an ordinary one-entry `0x2C` list.
- Profiling correlation uses extended opcode 147, isolated from game-message parsing.
- Experience, loot, rewards and callback order are unchanged.
- The party coalescer changes transmission frequency, not accumulated values.
- Non-Astra clients retain per-item loot-stat packets.
- User world/map modifications and zero-byte historical profiles remain untouched.

## Crystal comparison

Crystal was read-only. Its useful architectural precedent is separation of boss cooldown KV mutation from full-window transmission and core-owned analyzer state. Crystal still sends full party data in several paths, so that code was not copied blindly. The current patch uses measured local behavior and keeps TFS/Astra compatibility.

## Verification status

Release compilation succeeds with GCC 13/C++23. All 13 CTest targets pass. A startup smoke test reached map/spawn initialization with no new Lua load error; pre-existing missing-NPC/spawn warnings from the user's world remain outside this patch. Live gameplay percentiles must come from a new capture and are not fabricated from old logs.
