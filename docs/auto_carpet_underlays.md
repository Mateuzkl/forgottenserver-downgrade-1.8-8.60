# Decorative carpet underlays

Opening a stationary carpet inside a house also places its decorative layer
under eligible furniture in the eight adjacent tiles. This includes native beds,
whose parts can be down items with height rather than always-on-top items.
The bed objects, sleeper state and structural ground remain unchanged.

`Tile::synchronizeAutoCarpets` is called after manual carpet add, update and remove
events. This covers Lua `Item:transform`, including both remove/add transitions
and in-place updates. Generated decorations carry the persistent custom attribute
`__auto_carpet_source` and do not start another propagation event.

The destination must have the same house, floor and ground item ID. Unsupported
blocking items, incompatible stack order, floor changes, teleports, mailboxes and
trash holders prevent insertion. The normal `queryAdd` bed collision remains
unchanged; this is a restricted internal decoration operation.

Manual carpets take priority. Competing sources use ascending `(y, x, item ID)`
priority, and removing the winner selects another eligible adjacent source.
Only marked generated items are removed. Correct existing copies are retained.
Each affected destination checks only eight neighboring source positions; no
house-wide scan, timer or creature movement hook is introduced.

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

Every covered bed part must be an immediate neighbor of the source. Adding or
removing furniture later requires using/changing the carpet again. A destination
with ten things does not receive an additional decoration. Opaque bed sprites can
hide an underlying carpet even when the server stack is correct.

## Validation

`test_auto_carpet` loads the repository OTB/XML and exercises actual tile methods,
Lua action/binding and house tile serialization. Its nine cases cover two bed
parts, 100 C++ cycles, 100 Lua cycles, replacement/removal, manual carpet priority,
competing sources in both placement orders, house/ground/instance boundaries and
serializer restoration. Lua permission decisions use controlled responses;
they are not an authenticated player test.

Before gameplay acceptance, the following remain **NOT TESTED**:

- AstraClient 8.60 rendering beneath the requested headboard and both bed parts;
  sleeping/waking without changes to bed behavior.
- Authenticated invitation and protected-house denials, independent fold/wrap
  attempts on copies, and direct dragging/selection of the covered floor.
- Real database house save, logout, graceful shutdown and two restarts, with item
  counts checked before and after; house transfer/rollback and map/cache reload.
- Ground/house borders, manual carpets and competing sources observed in game.
- 500+ players/bots with comparable warmup, game-thread CPU and E2E p95/p99;
  Private Bytes and leak/sanitizer measurements during repeated use.

Compilation and serializer tests do not establish these gameplay results. No
client, protocol, item definitions, map assets or general collision rules change.
