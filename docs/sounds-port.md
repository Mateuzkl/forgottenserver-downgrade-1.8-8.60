# Sound port (CrystalServer to 8.60)

Reference: local CrystalServer ad5510a2. Client: OTC-Fonticak, branch implement-sounds.
The custom protocol requires the corresponding client build. Sound packets (0x46/0x47) are sent only when the login handshake identifies `isFonticakClient`; other clients receive no sound packets.

## Implemented

- All 512 numeric IDs from the source SoundEffect_t enum are available as SOUND_EFFECT_TYPE_* Lua globals.
- Monster attacks/defenses select projectile and impact sounds from damage type, radius and wave shape. Explicit soundCast and soundImpact (also source spelling impactCast) override defaults, including 0 for silence.
- Scripted monster attacks play their wrapper sounds after a successful Lua cast; ordinary combat uses its combat parameters. The old extra generic melee sound was removed.
- Monster soundChance(), soundSpeedTicks(), addSound(), getSounds(), deathSound() have getters/setters matching the source sound APIs.
- Without an explicit sounds table, monster voices/death use a matching Bestiary family for Dragon, Demon, Undead, Slime, Plant, Giant, Construct, Bird, Fey and Lycanthrope; other classes use generic monster sounds. These family defaults are an adaptation for the existing datapack.
- NpcType soundChance(), soundSpeedTicks(), addSound(), getSounds(), plus the NPC timer. Native NPC registration and the Crystal compatibility bridge accept npcConfig.sounds. XML accepts a sounds node with interval/ticks, chance and sound id children. Configuration updates also affect spawned NPCs.
- Charm(id) and Game.createBestiaryCharm(id) expose castSound()/impactSound() configuration for existing charm IDs 0..24. Sound is emitted by the native activation path, including the Lua misc analyzer bridge. This ports the sound configuration, not CrystalServer's unrelated Charm gameplay API.
- Offensive charm defaults reuse existing elemental catalog IDs; CrystalServer has no dedicated charm sound IDs. Other charms remain silent unless configured.
- Player:sendSingleSoundEffect(id[, own=true]) and sendDoubleSoundEffect(main, secondary[, own=true]). Position versions broadcast to nearby players and accept a Creature actor.
- Sources distinguish global (0), own player (1), other players (2), creatures (3), preserving instance filtering.
- Weapon overrides preserve cast/impact order and support impact-only sounds; unarmed attacks emit the fist sound. Ground containers use public movement audio, while held containers use backpack audio. Valuable/tool/ammo classifications follow the available 8.60 item types.
- Imbuement XML sound attributes are read and emitted with elemental weapon damage, without changing item persistence.
- Player:sendAmbientSound(id) selects a catalog location ambience; 0 stops it. Custom packet 0x47 carries a U32 ID. Client plays the loop and periodic effects on the ambient channel, respects audio/volume settings, and stops on disconnect. Item ambience remains managed by the client.

## Configuration examples

NPC Lua (before spawning):

~~~lua
npcConfig.sounds = {ticks = 10000, chance = 10, ids = {SOUND_EFFECT_TYPE_HUMAN_MALE_BARK}}
npcType:register(npcConfig)
-- For native scripts without register():
npcType:sounds({ticks = 10000, chance = 10, ids = {SOUND_EFFECT_TYPE_HUMAN_FEMALE_BARK}})
~~~

Charm sound overrides:

~~~lua
Charm(1):castSound(SOUND_EFFECT_TYPE_SPELL_OR_RUNE)
Charm(1):impactSound(SOUND_EFFECT_TYPE_MONSTER_SPELL_SINGLE_TARGET_FIRE)
-- Use 0 to silence either component.
~~~

Location ambience:

~~~lua
player:sendAmbientSound(catalogAmbientId)
player:sendAmbientSound(0) -- leaving the region
~~~

Ambient IDs are ambience_stream IDs, not numeric spell sound IDs. Set them from map-specific zone/movement scripts; no invented location IDs are assigned to this map.

## Remaining data/compatibility work

- Select accurate voice IDs for individual NPCs and monster subclasses where the source datapack supplies no configuration. NPCs with no sounds table remain silent, as in the source.
- Assign location ambient IDs to actual map regions; validate the official catalog's item IDs against this 8.60 map/client assets.
- Twelve modern spell sound names used by scripts have no numeric ID in the local source enum. They remain 0 rather than sending invented IDs.
- Validate playback in game (including settings, regions, charms, summons and instance boundaries). Compilation and Lua regression checks do not establish audio quality or asset completeness.

Lua regression check: run src/tests/test_sound_port.lua from the repository root with Lua 5.4. It covers registration/selection and explicit overrides, not live audio playback.
