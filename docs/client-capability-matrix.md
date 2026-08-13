# Client Capability Matrix

Baseline: `7cf30cd3` (main, after PR #250).

This document describes what the server actually does today, established by reading
the code — not by what the architecture was intended to be. Every count in it was
produced by `rg` over `src/`, not estimated.

## 1. Supported clients

| Client | Detected by | Family today | `isOTC` | `isOTCv8` | `isMehah` |
|---|---|---|---:|---:|---:|
| Classic CIP 8.60 | default | Cip860 | no | no | no |
| CIP 8.60 + DLL | `CLIENTOS_CUSTOM_DLL` + weather login magic | Cip860 | no | no | no |
| OTCv8 | `"OTCv8"` string in the login packet | Otcv8 | yes | yes | no |
| AstraClient | `"OTCv8"` **then** marker `"A"` + signature | Otcv8 | yes | yes | no |
| Fonticak | `"OTCv8"` **then** marker `"F"` + signature | Otcv8 | yes | yes | no |
| Mehah | `CLIENTOS_OTCLIENT_WINDOWS` | Mehah | yes | no | yes |

### The detection invariant that everything else rests on

In `ProtocolGame::onRecvFirstMessage` the Astra and Fonticak markers are parsed
**inside** the block guarded by `msg.getString(5) == "OTCv8"`, which has already set
`isOTCv8 = true`. Nothing clears it afterwards. Therefore, on this codebase:

```
isAstraClient    ⇒ isOTCv8 ⇒ isOTC
isFonticakClient ⇒ isOTCv8 ⇒ isOTC
isMehah          ⇒ isOTC
```

Astra is not a sibling of OTCv8. It is a **subset** of it: OTCv8 plus extra
capabilities, minus one (see `ThingUpgradeClassification` below).

This invariant is what makes several existing brand checks provably redundant, and
it is why the correct representation is `family + capabilities`, not five booleans.

## 2. Capability matrix

Source of truth column says who decides: `handshake` = negotiated on the wire,
`config` = `config.lua`, `both` = config gates a negotiated feature.

| Capability | CIP 8.60 | CIP + DLL | OTCv8 | Astra | Mehah | Fonticak | Source of truth | Changes bytes |
|---|:--:|:--:|:--:|:--:|:--:|:--:|---|:--:|
| ExtendedOpcode | no | no | yes | yes | yes | yes | handshake | no |
| ContainerPagination | no | no | yes | yes | yes | yes | handshake | **yes** |
| RewardChestPagination | no | no | no | **yes** | no | no | handshake | **yes** |
| QuickLootFlags | no | no | no | **yes** | no | no | both (`QUICK_LOOT_ENABLED`) | **yes** |
| ItemTierByte | no | no | opt-in¹ | opt-in¹ | no | opt-in¹ | both (`ITEM_TIER_DISPLAY`) | **yes** |
| ThingUpgradeClassification | no | no | **yes**² | **no**² | yes³ | yes² | both | **yes** |
| QuiverCountU16 | no | no | no | **yes** | no | no | handshake | **yes** |
| ItemMetadata | no | no | no | **yes** | no | no | both (`ASTRA_ITEM_STATE_ENABLED`) | **yes** |
| CreatureIcons | no | no | no | **yes** | no | no | handshake | **yes** |
| NativeZoneWeather | no | yes⁴ | opt-in⁵ | **yes** | no | opt-in⁵ | handshake | **yes** |
| OutfitStoreMode | no | no | no | **yes** | no | no | handshake | **yes** |
| HirelingProtocol | no | no | no | **yes** | no | no | both (`HIRELING_SYSTEM_ENABLED`) | **yes** |
| MonsterPodium | no | no | no | **yes** | no | no | handshake | **yes** |
| CharacterBazaar | no | no | no | **yes** | no | no | both (`CHARACTER_BAZAAR_ENABLED`) | **yes** |
| ColorizedLoot | no | no | no | **yes** | no | no | both | no (text only) |

¹ Requires the `OTCv8TierByte` marker in the login packet.
² `shouldSendThingUpgradeClassification()` returns `isOTCv8 && !isAstraClient && shouldSendItemTierByte()`.
   Astra is **deliberately excluded**. This is the one place where Astra is not a
   superset of OTCv8, and it is the reason "just alias Astra to OTCv8" is wrong.
³ Mehah takes a different path: `ITEM_UPGRADE_CLASSIFICATION` config, ignoring the tier byte.
⁴ Requires the DLL weather login magic; otherwise the connection is rejected.
⁵ Requires the `OTCv8ZoneWeather` marker.

## 3. Brand-check inventory

Counted with `rg` over `src/`, excluding declarations, assignments and comments.

| Flag | Active checks |
|---|---:|
| `isAstraClient` | 70 |
| `isOTC` | 48 |
| `isOTCv8` | 14 |
| `isMehah` | 10 |
| `isFonticakClient` | 6 |
| **Total** | **148** |

By layer:

| Layer | Checks |
|---|---:|
| Protocol (`protocolgame.*`, `protocollogin.*`, `protocolspectator.h`) | 70 |
| Outside protocol (`game.cpp`, `player.*`, `lua*.cpp`, `raids.cpp`) | 18 |

By file (Astra/Fonticak mentions):

| File | Mentions |
|---|---:|
| `src/protocolgame.cpp` | 127 |
| `src/protocollogin.cpp` | 46 |
| `src/player.cpp` | 34 |
| `src/networkmessage.cpp` | 18 |
| `src/protocolgame.h` | 9 |
| `src/player.h` | 8 |
| `src/luaplayer.cpp` | 8 |
| `src/game.cpp` | 7 |

## 4. Classification of the Astra/Fonticak surface

Using the categories from the refactor brief.

### E — dead / provably redundant

These are redundant by the detection invariant in §1, not by opinion. Each one
evaluates identically before and after because `isAstraClient ⇒ isOTCv8 ⇒ isOTC`:

| Site | Current | Equivalent to |
|---|---|---|
| `protocolgame.cpp:614` | `isOTCv8 \|\| isAstraClient \|\| isMehah` | `isOTCv8 \|\| isMehah` |
| `protocolgame.cpp:2673` | `!isOTC && !isOTCv8 && !isAstraClient` | `!isOTC` |
| `protocolgame.cpp:2847` | `!isAstraClient && !isOTC && !isOTCv8 && !isMehah` | `!isOTC` |
| `protocolgame.cpp:5164` | `!isOTCv8 && !isOTC && !isAstraClient` | `!isOTC` |
| `protocolgame.cpp:5218` | `!isOTCv8 && !isAstraClient` | `!isOTCv8` |

### C — real capability of wire format

`RewardChestPagination`, `QuickLootFlags`, `QuiverCountU16`, `ItemMetadata`,
`CreatureIcons`, `NativeZoneWeather`, `OutfitStoreMode`, `HirelingProtocol`,
`MonsterPodium`, `CharacterBazaar`, and the `ThingUpgradeClassification`
**exclusion**. These must survive the refactor as capabilities named after the
format, not the brand.

### D — gameplay leak (brand check outside the protocol layer)

18 checks. Gameplay should produce canonical state and let the protocol layer
decide representation.

| Site | Check |
|---|---|
| `game.cpp:3576` | monster podium |
| `game.cpp:3746` | reward-chest seek |
| `game.cpp:3764`, `3801`, `3812` | Astra-only packet entry points |
| `player.cpp:767`, `1307`, `5829`, `5833`, `7273` | Astra-only behaviour |
| `player.cpp:7646` | colorized loot text |
| `player.h:933` | Astra-only accessor guard |
| `luanetworkmessage.cpp:83`, `86` | Lua-visible brand |
| `luaplayer.cpp:3492`, `3505` | `isUsingAstraClient()` / `isUsingFonticakClient()` |

### F — historical compatibility with no wire effect

`isFonticakClient`. Audited every use:

| Site | Purpose |
|---|---|
| `protocolgame.cpp:1178` | detection |
| `protocolgame.cpp:881`, `1031` | copied to `player->client` |
| `protocolgame.cpp:1205`, `1214` | `FONTICAK_CLIENT_ONLY` login gate + log line |
| `protocollogin.cpp:703-737` | same gate on the login protocol |
| `player.h:1556`, `luaplayer.cpp:3498-3505` | Lua accessor |
| `protocolspectator.h:1056` | field declaration |

**`isFonticakClient` does not appear in a single serialization branch.** It never
changes a byte. It is a login gate and a Lua accessor, nothing more.

## 5. Open architecture conflict — Fonticak family

The refactor brief mandates:

```
Fonticak: isOTC = true, isOTCv8 = false, isMehah = true
```

The code does the opposite, and it does so on the wire: Fonticak announces
`"OTCv8"` before sending marker `"F"`, so it is currently detected as **OTCv8**
and receives the OTCv8 layout.

Moving Fonticak to the Mehah family would change real bytes, because `isMehah`
drives distinct branches at `protocolgame.cpp:653`, `2574`, `2583` and `5201`.
Notably `5201` reads `isMehah && !isOTCv8`, a branch Fonticak does not take today
and would start taking.

This is a wire-format change for a client that exists, so it is **not** applied
here. It requires a decision from the maintainer — see the note in the pull
request. The zero-risk half of the mandate (Fonticak must not be a server-side
*family*) is satisfied regardless, because the flag has no serialization effect.
