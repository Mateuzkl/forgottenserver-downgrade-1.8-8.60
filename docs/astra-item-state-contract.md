# Astra item state contract

The item state extension is server controlled. The server must only advertise or serialize it when all checks pass:

- The connection authenticated as AstraClient.
- The player has an active owner `ProtocolGame`.
- The connection is not spectator/cast.
- `astraItemStateEnabled` is enabled in `server_config.lua`.

When the gate passes, `sendFeatures()` may advertise:

- `DisplayItemDuration`
- `DisplayItemCharges`
- `PackedPlayerInventory`

Those features authorize the matching wire changes:

- Item serialization may append duration and charges.
- The server may send opcode `0xF5` with the packed player inventory snapshot.
- The actionbar may use the snapshot for item count and tier-aware equip state.

When the gate fails, the server must not send these feature flags, must not append duration/charges bytes to items, and must not send opcode `0xF5`. This includes non-Astra clients, OTCv8 Classic, Fonticak, spectators, and cast viewers.

The client must treat the feature flags as the source of truth. If `PackedPlayerInventory` is not enabled, the client should ignore `0xF5`, clear any cached snapshot, and keep old actionbar behavior instead of blocking use/equip locally.
