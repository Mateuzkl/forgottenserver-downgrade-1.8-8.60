# Pointer ownership audit for runtime hotspots

## Classification

| Type / path | Owner | Observer | Thread | Lifetime | Current risk | Representation |
| --- | --- | --- | --- | --- | --- | --- |
| `Game` creature registries | registry and map/tile graph | game systems | game thread | placement to removal/cleanup | ID reuse in delayed callbacks | `shared_ptr` in owning registries; ID plus lifetime token in callbacks |
| `Creature::followCreature` | creature registry | follower | game thread | may disappear at any tick | stale follow owner | stored `weak_ptr`; lock once per operation |
| `Creature::attackedCreature` | creature registry | attacker | game thread | may disappear during scripts/combat | repeated locks in hot loops | stored `weak_ptr`; local `shared_ptr` only while synchronous operation runs |
| `Creature::master` / summons | creature registry | summon/master | game thread | independent removal | ownership cycles or prolonged summons | `weak_ptr` in both non-owning relationships |
| `Game::checkCreatureLists` | shared with registry during scheduled checks | check loop | game thread | until check removal | entry must survive current check | `shared_ptr`; no extra copies inside loop |
| `SpectatorVec` | temporary snapshot | movement/combat/network | game thread | one synchronous operation | repeated ref-count traffic | temporary `shared_ptr` vector; never cached beyond map invalidation window |
| `Map::moveCreature` arguments | tile/map own objects | movement call | game thread | guaranteed for call | raw pointer escaping asynchronously | references/raw observers only inside call; no capture |
| combat target/caster arguments | creature registry | combat call | game thread | guaranteed for synchronous call | delayed chain can outlive both | raw observers synchronously; ID plus lifetime token when delayed |
| delayed condition target | creature registry | scheduler callback | reactor/game thread | may be removed before timer | condition could hit reused ID | ID plus lifetime token; condition remains uniquely owned until callback |
| follow-path callback | creature registry | dispatcher callback | reactor/game thread | may be removed or retargeted | duplicate/stale result and reused ID | ID, lifetime token and path generation; no long-lived `shared_ptr` |
| walk callback | creature registry | scheduler callback | reactor/game thread | may be removed before step | ID lookup may fail | ID lookup; callback exits when missing/removed/dead |
| `TaskReactor::Task::function` | task | reactor | producer then reactor | enqueue to execute/drop | accidental callback copy | `move_only_function` owned by task |
| scheduled `Task` | reactor heap | scheduler cancellation ID | reactor | schedule to execute/cancel | deferral must preserve ID | unique task callback plus stable identifier |
| `Combat` in chain spell | spell/combat graph and delayed callback | chain callback | game thread | chain delay | combat params needed after original call | intentional `shared_ptr`; bounded short delay |
| corpse highlight | item/tile systems | repeating callback | game thread | corpse may disappear | prolonging corpse lifetime | stored `weak_ptr<Item>`; lock only for callback |
| `Dispatcher::asyncTask` result | worker closure then dispatcher closure | callback | worker to game thread | until result delivery | raw game-object captures supplied by callers | values/IDs only; review each caller before adding raw captures |

## Changes made

- Follow callbacks carry `creatureId`, unique per-object `lifetimeToken` and
  follow generation. Old results cannot apply to a removed, reused or
  retargeted creature.
- Delayed condition add/remove callbacks now validate both ID and lifetime
  token. They do not hold a creature alive through the walk delay.
- Delayed chain combat validates caster and target lifetime tokens. `Combat`
  remains shared only because its immutable parameters are required by the
  delayed chain steps.
- Dispatcher returns reactor acceptance. Rejected follow work clears its
  pending gate instead of leaving the creature permanently stuck.

## Remaining rules

- Do not capture raw `Creature*`, `Player*`, `Monster*`, `Item*` or `Tile*` in
  scheduler, dispatcher, DB or worker callbacks.
- A raw pointer/reference is acceptable only as a documented synchronous game
  thread observer whose owner cannot release it during that call.
- Do not replace hot-loop observers with `shared_ptr` solely for safety. Use a
  local strong reference only when code can invoke scripts/removal and must
  finish using the object afterward.
- ASan/UBSan and TSan builds remain separate; never combine TSan with ASan.
