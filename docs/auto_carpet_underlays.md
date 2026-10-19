# Decorative carpet underlays

Opening a stationary carpet inside a house also places its decorative layer
under eligible furniture in the eight adjacent tiles. This includes native beds,
whose parts can be down items with height rather than always-on-top items.
The bed objects, sleeper state and structural ground remain unchanged.

`Tile::synchronizeAutoCarpets` is called after manual carpet or ground add, update
and remove events. This covers Lua `Item:transform`, including remove/add
transitions and in-place updates. Generated decorations carry the persistent custom attribute
`__auto_carpet_source` and do not start another propagation event.
Furniture changes that affect support or blocking reconcile only their own tile.
This includes wrapping/unwrapping, placement, removal and type changes, using the
same stationary-bed/flat-surface and blocking criteria as underlay eligibility.

The destination must have the same house, floor and ground item ID. Unsupported
blocking items, incompatible stack order, floor changes, teleports, mailboxes and
trash holders prevent insertion. The normal `queryAdd` bed collision remains
unchanged; this is a restricted internal decoration operation.

Manual carpets take priority. Competing sources use ascending `(y, x, item ID)`
priority, and removing the winner selects another eligible adjacent source.
Only marked generated items are removed. Correct existing copies are retained.
Each affected destination checks only eight neighboring source positions; no
house-wide scan or timer is introduced. Each destination reconciles separately,
suppresses reentrant reconciliation and elects again after native removals and
insertions, because movement callbacks can change the sources or destination.
Tile references are retained for the entire update/removal hooks, including work
after reconciliation returns. Ground replacement also retains the tile through
the following removal notification. Detached/replaced tiles are not modified by
reconciliation; standalone tiles retain native flags and skip observer callbacks
until shared ownership exists.
Reconciliation is bounded to 16 passes; a continuously changing
script leaves no generated decoration and waits for a later relevant event.

Generated decorations use a fresh `Item::CreateItem` with their original item ID
and a source marker. They inherit no source UID, action ID, quest attributes,
wrap attributes or decay state. Removal retains shared references before mutation
and uses the native item removal path; insertion uses tile notifications.
The carpet action and native wrapping operation reject independent use of a copy.
Existing house invitation, protection and stacked-carpet handling are preserved.

## Scope and persistence

Generation is limited to persistent instance 0 because the existing item serializer
does not persist `Thing::instanceID`. Other instances retain ordinary carpet use
without generating copies. A generated carpet is saved by the existing house
carpet serialization and retains its source marker when restored through
`internalAddThing`; loading does not propagate a second copy.

Every covered bed part must be an immediate neighbor of the source. A destination
with ten things records a pending insertion. Its next item/creature removal
notification retries only that destination if capacity is available; ordinary
removals only check the pending flag and do not scan neighboring tiles. Removing
or invalidating the source clears the pending decision. Opaque bed sprites can
hide an underlying carpet even when the server stack is correct.
Pending flags are runtime state: restoration without lifecycle callbacks requires
a later relevant carpet, ground or furniture event to arm a missing-decoration retry.

## Validation

`test_auto_carpet` loads the repository OTB/XML and exercises actual tile methods,
Lua action/binding and house tile serialization. Its 19 regression cases cover two bed
parts, 100 C++ cycles, 100 Lua cycles, replacement/removal, manual carpet priority,
competing sources in both placement orders, house/ground/instance boundaries and
serializer restoration, reentrant source/manual-carpet changes, ground lifecycle
updates, capacity retries, furniture/blocker lifecycle, native wrap transforms,
unshared tile flags and deletion of a source tile during raw update/removal and
ground replacement calls.
Boundary destinations are checked using an active
instance-0 source. Lua permission decisions use controlled responses;
they are not an authenticated player test.

The focused Windows/MSVC AddressSanitizer run passes all 19 cases. Before the
hook lifetime fix, the raw removal case reproduced a heap-use-after-free in
`Tile::onRemoveTileItem`. Test callbacks are cleared before fixture cleanup so
their captures cannot outlive local variables. This does not establish live-game
memory behavior or leak coverage.

Before gameplay acceptance, the following remain **NOT TESTED**:

- AstraClient 8.60 rendering beneath the requested headboard and both bed parts;
  sleeping/waking without changes to bed behavior.
- Authenticated invitation and protected-house denials, independent fold/wrap
  attempts on copies, and direct dragging/selection of the covered floor.
- Real database house save, logout, graceful shutdown and two restarts, with item
  counts checked before and after; house transfer/rollback and map/cache reload.
- Ground/house borders, manual carpets and competing sources observed in game.
- 500+ players/bots with comparable warmup, game-thread CPU and E2E p95/p99;
  Private Bytes and live-game leak/sanitizer measurements during repeated use.

Compilation and serializer tests do not establish these gameplay results. No
client, protocol, item definitions, map assets or general collision rules change.
