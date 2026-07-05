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

- OTCv8 / Mehah-style clients with extended feature support.
- AstraClient.
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

The server sends OTCv8/Mehah/Astra feature overrides from `ProtocolGame::sendFeatures()` in `src/protocolgame.cpp`.

Clients that support packet `0x43` (`GameServerFeatures`) should let the server control packet-layout flags.

The server currently sends these common flags to OTCv8/Astra:

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
ThingUpgradeClassification = false
ItemTierByte = shouldSendItemTierByte()
```

For Mehah-only detection, the server sends:

```cpp
ContainerPagination = true
BrowseField = true
ThingUpgradeClassification = shouldSendThingUpgradeClassification()
```

For AstraClient, the server may also send Astra-only flags:

```cpp
ExperienceBonus = true
PlayerFamiliars = true
AstraCreatureIcons = true
AstraQuiverCountU16 = true
AstraOutfitStoreMode = true
DisplayItemDuration = true
DisplayItemCharges = true
PackedPlayerInventory = true
AstraItemMetadata = true
```

Do not copy Astra-only flags into OTCv8 Classic. They need Astra parser support.

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

`shouldSendQuickLootFlags()` is true only for AstraClient when quick loot is enabled in config.

### GameThingUpgradeClassification

Server condition:

```cpp
ThingUpgradeClassification = false // OTCv8/Astra path
ThingUpgradeClassification = shouldSendThingUpgradeClassification() // Mehah path
```

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

and the server-side item tier byte mode.

## AstraClient Notes

AstraClient has its own 8.60 feature profile and Astra-only packet extensions. Do not treat Astra as a direct copy of OTCv8 Classic.

Astra-only features include:

```lua
GameAstraCreatureIcons
GameAstraQuiverCountU16
GameAstraOutfitStoreMode
GameAstraItemMetadata
```

These flags are sent only when the server recognizes AstraClient and the related config is enabled.

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
- [ ] Astra-only flags are used only by AstraClient.
- [ ] Classic CIP uses DLL patches instead of OTC feature flags.
- [ ] Login, walking, look, use, container open, corpse open, store inbox, and logout were tested.
