# Client stack consistency and movement diagnostics

The 8.60 wire format exposes at most ten things per tile. Previously,
GetTileDescription reserved a creature slot on the viewer's tile, whereas
getClientIndexOfCreature and getThing(player, index) could count omitted top
items. With ground and twelve top items the map description placed a creature
at index 9, but the lookup began creatures at index 13. OTC could also lose the
viewer entirely when another creature occupied the last slot.

Tile::getClientStack now defines a bounded stack shared by tile serialization,
creature indices, item indices and incoming player stack lookups. It preserves
ground/top/creature/down ordering and instance visibility. The viewer retains a
slot on its own tile for both stock and OTC clients. Hidden/overflow things
cannot resolve to an unrelated serialized object. Overflow visible creatures
retain the existing >=10 full-update path instead of emitting invalid 0x6D
references. The stack is a fixed array, with no heap allocation.

All overflow consumers must honor that sentinel. Movement now replaces affected
post-move tiles instead of implicitly inserting a creature omitted from the
destination stack. Overflow removals replace the post-removal tile. Emblem
updates do not add at stack 10: they invalidate the cached creature description
and refresh the tile, allowing the next visible appearance to include its emblem.
Entering self's reserved last slot uses a full map description, and leaving
that slot refreshes the old tile so newly exposed contents are not omitted.
Sparse movement and ordinary emblem updates retain their original wire format.

Regression tests compare actual serialized bytes with the expected stack,
exercise NPC ordering and instance isolation, and verify the 0x6D source stack
when moving out of a dense tile. Existing house and item lifetime tests remain
part of validation. MAX_STACKPOS_THINGS and the 18x14 map viewport are unchanged.

## Targeted reproduction

Set the environment variable `TFS_WALK_TRACE_PLAYER_GUID` to the numeric GUID of
one test character before launching TFS. This opt-in logs only that viewer's
movement positions, creature IDs, stack positions, teleport status, milliseconds,
opcode, output-buffer offset and payload length. Match the runtime viewer ID
with Astra's debugWalking trace. No passwords, tokens, IPs or raw frames are
logged by this instrumentation. Unset the variable and restart to disable it.

The report's Saike incident is not claimed as reproduced or definitively solved.
Collect traces before the first failure, including login, NPC entry, movement,
death/respawn, teleports and reconnects. Keep the logical aware range at 18x14;
a larger rendered panel is not a protocol negotiation. Do not enlarge the map
protocol or clean production maps as part of this patch.

ItemValues (0xC6) uses a U16 count and fixed six-byte records. It does not need
isolated transport frames. The companion Astra correction stops both ItemDetails
(0xC7) readers at their counted boundary; previously they drained later opcodes
in the same network frame. No unproven change is made to server 0xC6 framing.
