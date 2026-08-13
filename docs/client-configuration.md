# Client Configuration

This server runs the Tibia 8.60 protocol, but the client assets can use the newer 15.24 appearance set.

In this project, "Client Update: Version 15.24" means the `.dat`, `.spr`, and `items.otb` assets were updated to the 15.24 appearance/content set. It does not mean the login protocol changed to 15.24. The allowed protocol is still 8.60:

```cpp
CLIENT_VERSION_MIN = 860
CLIENT_VERSION_MAX = 860
CLIENT_VERSION_STR = "860"
```

## Assets

Use matching `.dat`, `.spr`, and `items.otb` files. If the client assets and server `items.otb` do not match, you can get wrong item ids, missing sprites, bad look text, or map loading issues.

Supported client targets:

- OTCv8 / Mehah-style clients with extended feature support and matching protocol extensions.
- Classic CIP 8.60 client with the project DLL patches.

## Where Client Features Are Configured

Common OTCv8/Mehah paths:

```text
modules/game_features/features.lua
modules/game_features/game_features.lua
```

Different forks use different filenames. Edit the file that contains the `updateFeatures(version)` function and the `if(version >= 860) then` block.

Client feature ids must match:

```text
client: modules/gamelib/const.lua
client: src/client/const.h
server: src/const.h
```

## Server Feature Handshake

The server sends OTCv8/Mehah feature overrides from `ProtocolGame::sendFeatures()` in `src/protocolgame.cpp`.

Clients that support packet `0x43` (`GameServerFeatures`) should let the server control packet-layout flags.

`OTCv8` identifies only the client family. Optional wire extensions are
negotiated in the existing login marker loop with `OTCv8CapabilitiesV1`
followed by a 32-bit capability mask. A plain OTCv8 client that does not send
this marker receives only the common feature set below.

The account-login protocol is negotiated separately. Clients with the extended
character-list parser send `OTCv8LoginCapabilitiesV1` and its 32-bit mask after
the password. OTCv8 clients without that marker receive the standard 8.60
character list (`0x64`); capable clients may request the extended list (`0x65`),
cast list, and boosted-creature metadata independently.

The server currently sends these common flags to OTCv8:

```cpp
ExtendedOpcode = true
SkillsBase = true
PlayerMounts = true
MagicEffectU16 = true
OfflineTrainingTime = true
DoubleSkills = true
BaseSkillU16 = true
AdditionalSkills = true
ExtendedClientPing = true
CreatureIcons = true
ContainerPagination = true
BrowseField = true
QuickLootFlags = shouldSendQuickLootFlags()
ThingUpgradeClassification = shouldSendThingUpgradeClassification()
ItemTierByte = shouldSendItemTierByte()
```

For OTCv8, `shouldSendThingUpgradeClassification()` requires an OTCv8 session and an enabled item-tier byte. For Mehah, it follows the server's item-upgrade-classification setting. Other client families receive neither feature.

For Mehah-only detection, the server sends:

```cpp
ContainerPagination = true
BrowseField = true
ThingUpgradeClassification = shouldSendThingUpgradeClassification()
```

The server may also send these OTCv8 extension flags:

```cpp
ExperienceBonus = true
PlayerFamiliars = true
ExtendedCreatureIcons = true
QuiverCountU16 = true
OutfitStoreMode = true
DisplayItemDuration = true
DisplayItemCharges = true
PackedPlayerInventory = true
ItemMetadata = true
```

These extended flags require matching OTCv8 parser support.

## Recommended OTCv8 / Mehah 8.60 Block

Use this as the base for OTCv8/Mehah forks that need an 8.60 feature profile:

```lua
if(version >= 860) then
    g_game.enableFeature(GameAttackSeq)
    g_game.enableFeature(GameBot)
    g_game.enableFeature(GameExtendedOpcode)
    g_game.enableFeature(GameSkillsBase)
    g_game.enableFeature(GamePlayerMounts)
    g_game.enableFeature(GameMagicEffectU16)
    g_game.enableFeature(GameDistanceEffectU16)
    g_game.enableFeature(GameDoubleHealth)
    g_game.enableFeature(GameDoubleSkills)
    g_game.enableFeature(GameOfflineTrainingTime)
    g_game.enableFeature(GameBaseSkillU16)
    g_game.enableFeature(GameAdditionalSkills)
    g_game.enableFeature(GameIdleAnimations)
    g_game.enableFeature(GameEnhancedAnimations)
    g_game.enableFeature(GameExtendedClientPing)
    g_game.enableFeature(GameSpritesU32)
    g_game.enableFeature(GameDoublePlayerGoodsMoney)
    g_game.enableFeature(GameCreatureIcons)
    g_game.enableFeature(GamePurseSlot)
    g_game.enableFeature(GamePrey)
    g_game.enableFeature(GameSpellList)
end
```

Some forks also define:

```lua
GameLeechAmount
```

Only enable `GameLeechAmount` if your client source defines it and your parser expects it. This server feature enum does not currently send a `GameLeechAmount` feature.

## Packet-Layout Flags

These flags can break the protocol if the client and server disagree:

```lua
GameQuickLootFlags              -- id 123
GameThingUpgradeClassification  -- id 130
GameItemTierByte                -- id 131
```

Recommended behavior:

```lua
-- Keep these controlled by the server feature handshake when possible.
-- Do not blindly switch them to enableFeature.
g_game.disableFeature(GameQuickLootFlags)
g_game.disableFeature(GameThingUpgradeClassification)
g_game.disableFeature(GameItemTierByte)
```

Then let packet `0x43` enable or disable the final values after login.

### GameQuickLootFlags

Server condition:

```cpp
QuickLootFlags = shouldSendQuickLootFlags()
```

`shouldSendQuickLootFlags()` is true only when the client advertises the quick-loot capability and quick loot is enabled in config.

### GameThingUpgradeClassification

Server condition:

```cpp
ThingUpgradeClassification = shouldSendThingUpgradeClassification() // OTCv8 path
ThingUpgradeClassification = shouldSendThingUpgradeClassification() // Mehah path
```

The helper applies the family-specific rule: OTCv8 requires the negotiated `ItemTierByte` capability and must not negotiate the mutually exclusive item-metadata layout, while Mehah uses `enableItemUpgradeClassification`. Unsupported clients remain excluded.

For Mehah, this depends on:

```lua
enableItemTierDisplay = true
enableItemUpgradeClassification = true
```

### GameItemTierByte

Server condition:

```cpp
ItemTierByte = shouldSendItemTierByte()
```

This depends on:

```lua
enableItemTierDisplay = true
```

and client support announced either by the legacy `OTCv8TierByte` marker or the V1 capability mask.

## OTCv8 extension notes

These packet extensions are enabled only when the OTCv8 client advertises the matching capability.

Extended OTCv8 features include:

```lua
GameExtendedCreatureIcons
GameQuiverCountU16
GameOutfitStoreMode
GameItemMetadata
```

For every extension, the effective value is `clientSupportsFeature && serverEnablesFeature`. Serializers use that same effective capability, so a feature is never merely advertised without its matching wire layout (or serialized without being negotiated).

## Classic CIP Client

The classic CIP client does not use `g_game.enableFeature`.

It needs matching 8.60-compatible assets and the project DLL patches for extended limits:

| DLL patch | Purpose |
|---|---|
| `__MAGIC_EFFECTS_U16__` | Magic effects above 255 |
| `__DISTANCE_SHOOT_U16__` | Distance effects above 255 |
| `__PLAYER_HEALTH_U32__` | Player health above 65535 |
| `__PLAYER_MANA_U32__` | Player mana above 65535 |
| Outfit Limit Changer | Outfit ids above old 8.60 limits |

Store inbox on classic CIP should be accessed with commands such as `!storeinbox`, `!sinbox`, or `!inbox`.

## Final Checklist

- [ ] The client is still connecting as protocol 860.
- [ ] `.dat`, `.spr`, and server `items.otb` come from the same asset set.
- [ ] OTCv8/Mehah has the 8.60 base features enabled.
- [ ] `GameSpritesU32` matches the sprite file format.
- [ ] `GameQuickLootFlags`, `GameThingUpgradeClassification`, and `GameItemTierByte` match `sendFeatures()`.
- [ ] Extended flags match the capabilities announced by the OTCv8 client.
- [ ] Classic CIP uses DLL patches instead of OTC feature flags.
- [ ] Login, walking, look, use, container open, corpse open, store inbox, and logout were tested.
