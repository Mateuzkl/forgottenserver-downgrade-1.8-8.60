# AstraClient kill stutter

## Instrumented path

The client maps server kill to:

`0xD1 parse -> onKillTracker -> HuntingAnalyser/DropTracker/LootAnalyser -> UI`

and separately maps party `0x2B`, boss `0x2C`, charm `0x2D`, imbuement `0x30` and skill `0x31` updates.

During a profiling death, Astra writes `ClientDeathTrace` records containing `deathId`, packet/callback stage, duration, item/member/widget counts and current FPS. Opcode 147 provides start/end markers without altering the 8.60 packet stream.

The native party and boss parsers also write `ClientPacketTrace` with separate byte-parse and Lua callback/UI microseconds. This keeps parser allocation time distinct from widget work.

## UI fixes

- `BossCooldown:setupCooldown` no longer clears the list and destroys every child for a one-boss update.
- `PartyHuntAnalyser:onPartyAnalyzer` no longer cancels and recreates its one-second cycle event for every packet.
- Hunt loot is counted once from the recursive kill packet rather than once again per `0xCF` packet. All corpse items are accumulated before one LootAnalyser graphics/window refresh, replacing one rebuild per item.

## Validation matrix

Compare: Astra with all analyzers, each analyzer alone, analyzers hidden, and a non-Astra/headless client. A server `deathId` that ends quickly while Astra reports a long UI stage identifies a client stall; matching long server stages identify server work; both can occur in the same death.
