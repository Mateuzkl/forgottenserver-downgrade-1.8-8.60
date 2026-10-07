# ItemRegistry lifetime audit and fix

Date: 2026-10-01. Branch: `perf/realistic-load-latency`, PR #318.
This is a focused ownership correction on an experimental research branch,
not a production-readiness or capacity guarantee. Nothing is merged into main.

## Root cause

The previous mutex protected an `unordered_set<Item*>`, but its boolean
membership result did not retain the object. The last external `shared_ptr`
could disappear after the check and before `internalRemoveItem` called
`getParent()`. The late ownership acquisition also missed the query-only
`test=true` path. Additionally, an Item base destructor erases membership only
after derived destruction has begun, so membership alone cannot prove that a
fully live derived object is available.

Calling `weak_from_this()` through that raw candidate would itself access
potentially destroyed storage. Keeping the registry mutex locked throughout
removal would instead risk deadlock when callbacks create or destroy items.

## Ownership model and construction audit

World tiles, containers, player inventory/depot/inbox members, Lua shared
userdata and temporary-item lists own items through `shared_ptr`. The deferred
release queue also holds shared owners. Decay holds weak references and locks
them before processing; it does not keep every decaying item alive forever.

Threaded login can construct and destroy inventory on a database/login worker.
`IOLoginData` holds the loaded inventory in shared ItemMap entries and transfers
ownership into player/container members before dropping those entries. The
asynchronous login path retains the player across the handoff to the dispatcher.
Registry metadata therefore must remain synchronized even though ordinary
world mutation is dispatcher-confined.

Shared construction now uses `Item::make<T>(...)`. It creates the complete
derived object/control block with `make_shared`, then registers its weak owner
before returning it to the caller. Neither the ordinary nor copy constructor
registers an incomplete object. The private registration operation is inside
the helper, not a second call each factory must remember.

Audited and integrated creation paths:

- `Item::CreateItem`: Item, Container, BedItem, Door, Teleport, MagicField,
  Mailbox, TrashHolder, DepotLocker, DepotBox, RewardChest and Inbox branches;
- `CreateItemAsContainer`, stream decoding, clones and transform replacements:
  flow through the same shared creation helper/factories;
- browse fields: `Container::createBrowseField`;
- house documents: `HouseTransferItem::createHouseTransferItem`;
- player-owned StoreInbox, DepotChest, DepotLocker and RewardChest;
- map loading, database inventory and Lua creation: use Item factories;
- existing fixtures, including the test-only derived BedItem: migrated to the
  same helper without changing their gameplay assertions.

A repository search found no remaining production `make_shared`,
`allocate_shared` or raw `new` creation of the audited Item-derived types.
One deliberate test bypass remains to verify that an unregistered shared item
fails closed rather than dereferencing the candidate.

Raw/embedded Items do exist: `House::transfer_container` is an embedded Container
used as an internal parent cylinder. It has no shared control block, is not
registered, and cannot be pinned as an independently owned item. Public
constructors remain available for that use. **Future shared-item creation must
use `Item::make`; direct `make_shared<T>` is not automatically intercepted.**
The helper establishes a construction contract, not compile-time prevention of
all possible future factory bypasses.

## Pinning and shutdown

The registry now stores `Item* -> weak_ptr<Item>`. `Item::pin(candidate)` treats
the candidate only as an address key, acquires the metadata mutex, finds the
entry and locks its external control block. It never reads Item storage first.
Null, unknown, expired or shutdown-retired entries return an empty owner.

If pinning wins the race against the last owner release, the returned strong
reference keeps the complete item alive. If release wins, `weak_ptr::lock`
fails, including while a derived destructor is still executing. The metadata
mutex is released before any gameplay method, Lua call, query, notification,
database work or ownership-changing callback runs.

`internalRemoveItem` obtains this pin before its first virtual dereference and
holds it for the entire operation, including query-only removal. `transformItem`
does the same before its initial ID check. `Game::getItemSharedRef` delegates to
the pin API, so existing move, trade, loot, container-reference and release-queue
consumers no longer recover ownership by dereferencing the raw candidate.
The standalone `isValidItemPointer` API has been removed.

The existing irreversible shutdown contract is preserved. Clearing the registry
removes weak metadata and disables later registration/pinning under the same
mutex. Already acquired pins and external owners survive; later destruction is
safe. The previously immortal registry storage remains immortal to avoid
cross-translation-unit destruction-order hazards. No new permanent strong
Item owner, ownership cycle or per-Item lifetime leak was introduced.

Count semantics, logical-removal checks, occupied-bed handling, callback order,
decay, protocol output and unrelated packet/combat/Reactor changes are unchanged.

## Changed source and test files

| File | Reason |
| --- | --- |
| `src/item.h` | Declare the post-construction shared helper, explicit pin API and private registration. |
| `src/item.cpp` | Replace membership with weak lifetime metadata; migrate factory branches; preserve shutdown. |
| `src/game.h` | Make raw Item/Container ownership recovery use metadata pinning. |
| `src/game.cpp` | Pin before the first remove/transform dereference and retain the owner through callbacks. |
| `src/container.cpp` | Register shared browse-field construction. |
| `src/house.cpp` | Register shared house-document construction. |
| `src/player.cpp` | Register shared inbox/depot/reward construction. |
| `src/tests/test_item_registry.cpp` | Nine deterministic ownership/concurrency/retirement cases. |
| `src/tests/test_item_lifetime.cpp` | Use registered creation and actual pins in existing 77 lifetime cases. |
| `src/tests/test_creature_walk.cpp` | Use registered Item fixture creation. |
| `src/tests/test_imbuement_lifecycle.cpp` | Use registered Item/Container fixture creation. |
| `src/tests/test_loot_highlight_lifecycle.cpp` | Use registered corpse/loot fixture creation. |
| `src/tests/test_monster_target_state.cpp` | Use registered Item fixture creation. |
| `src/tests/test_reward_container_ancestry.cpp` | Use registered container fixture creation. |

## Validation

Verified locally in WSL Linux with GCC 13.3 and Clang 18.1.3:

- GCC Release/O3 server target `tfs` built successfully.
- GCC `test_item_registry` and `test_item_lifetime` passed: nine registry cases
  and all 77 existing lifetime cases, with no weakened gameplay assertions.
- Clang Debug, `ENABLE_TSAN=ON`, native optimization/mimalloc/ASan disabled:
  both focused targets built and passed with
  `TSAN_OPTIONS='halt_on_error=1 second_deadlock_stack=1 history_size=4'`.
  ThreadSanitizer reported no races in these executions.

The new cases use barriers/latches, not timing sleeps. Coverage includes both
pin/release orderings, a paused derived destructor, the first dereference inside
production `internalRemoveItem(test=true)`, stale remove/transform rejection,
all shared Item-derived classes, construction/copy/clone/stream factories,
parallel construction/copy/clone/pinning/destruction and irreversible retirement
with live worker-owned objects and a pre-existing strong pin. The final parallel
clone extension was validated in Clang/TSan; the preceding GCC focused run
passed before that extension.

Focused commands executed:

```sh
cmake --build /home/mateus/atlas-perf-20261001-gcc --parallel 2 \
  --target tfs test_item_registry test_item_lifetime
ctest --test-dir /home/mateus/atlas-perf-20261001-gcc \
  --output-on-failure -R 'test_item_registry|test_item_lifetime'
cmake --build /home/mateus/item-registry-20261001-clang-tsan --parallel 1 \
  --target test_item_registry test_item_lifetime
TSAN_OPTIONS='halt_on_error=1 second_deadlock_stack=1 history_size=4' \
  ctest --test-dir /home/mateus/item-registry-20261001-clang-tsan \
  --output-on-failure -R 'test_item_registry|test_item_lifetime'
```

The broader GCC regression rebuild completed for the 18 preceding PR targets
and the additional item/imbuement/loot/reward targets. The rebuilt targets were:

- `test_combat_packets`, `test_performance_metrics`, `test_outputmessage`,
  `test_protocolgame_pipeline`, `test_xtea`;
- `test_combat`, `test_equipment_combat_bonus`, `test_creature_walk`,
  `test_reactor`, `test_game_creature_checks`;
- `test_map_spectators`, `test_spectators`, `test_monster_target_state`,
  `test_monster_idle_events`;
- `test_condition_damage_queue`, `test_condition_shared_lifetime`,
  `test_connection_write_lifetime`, `test_script_env_bounds`;
- `test_item_registry`, `test_item_field_parsing`, `test_imbuement_lifecycle`,
  `test_loot_highlight_lifecycle`, `test_reward_container_ancestry`.

An additional normal CTest run passed imbuement, loot highlight and both item
targets. Its reward test was not started because that executable was still being
linked. After the rebuild completed, the user requested immediate commit/push
without further testing, so no final all-target CTest run or further sanitizer
run was performed. No Windows/MSVC build, ASan/UBSan run, final GCC rerun of the
clone extension or full live gameplay acceptance is claimed for this correction.

## Hot-path impact

This fix has **not** been rebenchmarked with StressBot. Earlier CPU numbers in
the performance report describe earlier packet changes, not this lifetime fix.

Operation counts from the implemented code:

- construction/destruction: one metadata mutex acquisition each, as before;
  registration now stores a weak owner rather than only an address;
- non-null pin: one mutex acquisition, one hash lookup and one control-block
  weak lock; no Item construction or heap allocation solely for pinning;
- removal entry: replaces the old mutex-protected membership check with the
  pin and removes the later duplicate ownership recovery;
- transform entry and other `getItemSharedRef` callers: add a metadata
  mutex/hash lookup compared with their former raw `weak_from_this` recovery;
- callbacks do not hold the registry lock; nested removal/release may perform
  their own short pin operations after the preceding lock has been released.

Weak values consume more registry metadata than an address-only set. Mutex
contention and end-to-end CPU/RSS effects are not measured here. No speculative
reserve size, global gameplay lock, custom reference count or per-pin allocation
was added. Correctness is verified; a throughput improvement is not claimed.

## Remaining limits and suspicious boundaries

- A pin protects Item storage, not the parent cylinder, logical validity or
  concurrent field mutation. Dispatcher/world-state confinement is still
  required; arbitrary concurrent world removal is not newly supported.
- An address-only API cannot identify an old item if its address has already
  been reused for another live item (ABA). Queued work that needs identity must
  capture a shared/weak owner or an identity/generation token, not only `Item*`.
- `transformItem` still returns a borrowed raw pointer. Its internal pin ends
  when the function returns; callers need their existing world/local owner for
  subsequent use. This is not a new long-lived handle API.
- `ScriptEnvironment::localMap` stores legacy raw UID entries. It depends on
  script/world ownership and the deferred-release queue during callbacks;
  this patch does not prove those handles safe across arbitrary future cleanup
  or callback boundaries. It is a separate candidate for weak/owned handles.
- `Lua::pushItem`, movement callbacks and local helpers in Container/Tile/Player
  still use `weak_from_this` for objects supplied live by dispatcher-confined
  owners or existing snapshots. They are **not** safe entry points for arbitrary
  stale pointers. No demonstrated additional asynchronous lifetime gap in those
  paths was changed in this focused patch.
- Scheduled player item actions re-resolve IDs/positions; decay and loot timers
  retain weak/shared owners. This audit is not a proof of every raw pointer in
  the server, nor a guarantee against future construction-helper bypasses.

Private config, user XML changes, world data, credentials, binaries and raw
benchmark artifacts are excluded. Main and the PR description are unchanged.

## Suggested PR-description replacement

> The ItemRegistry check/use race has been replaced with weak-metadata lookup
> that acquires a strong lifetime pin before raw Item access. Shared construction
> is registered after the control block exists, and remove/transform operations
> retain ownership through callbacks. Deterministic concurrency/retirement tests
> and the existing lifetime suite pass under GCC and Clang ThreadSanitizer.
> This resolves the reviewed lifetime gap, not full production readiness or
> arbitrary concurrent world-state mutation.
