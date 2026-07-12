# Analyzer server/client audit

| System | Previous kill path | Confirmed cost/risk | Change |
|---|---|---|---|
| Hunt analyzer | One recursive `0xD1` corpse plus one `0xCF` packet for every leaf item, repeated for each receiver. | Duplicate serialization, packet parsing and UI accounting on the death frame. | Astra accounts leaf loot directly from `0xD1`; `0xCF` remains only for clients that do not support inline loot. |
| Party analyzer | Complete member and name tables were sent to every party member on each corpse and each heal component. Astra cancelled/recreated a cycle event and refreshed all member widgets for every packet. | Packet amplification and repeated layout/tooltips. | Server coalesces dirty updates for 100 ms; client keeps one cycle event. |
| Boss cooldown | `setBossCooldown` scanned all CustomBosstiary entries and synchronously loaded cold KV keys, then Astra destroyed/recreated all widgets. | A cold KV read was 17 ms; work occurred inside a death callback. | Server sends a one-entry packet using the existing `0x2C` shape; Astra merges the entry and updates/creates only its widget. |
| Misc analyzer | Small `0x2D`, `0x30`, `0x31` deltas; UI refreshes periodically. | Not the primary packet-volume source. | Added correlated callback/UI timing; no protocol change. |
| Battle Pass | Kill callback can cold-load KV, scan missions and send full mission/resources state. | An older callback reached 26 ms. | Traced through Lua/KV/packet `deathId`; further delta conversion requires a live post-attribution sample. |
| Bestiary/task/prey/proficiency | Several Lua callbacks, storage/KV work and delayed saves. | Older bestiary SQL reached 48 ms; task-board reached 93 ms; proficiency login reached 774 ms. | Existing C++ bestiary port remains; timers and synchronous DB/KV are now attributable. No unproven subsystem was blindly ported. |

The wire format of existing opcodes was not extended. This specifically avoids the earlier failure mode where an extra or reordered byte caused `invalid thing id (0)` after opcode `0x64`.
