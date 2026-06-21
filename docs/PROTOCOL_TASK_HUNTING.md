# Task Hunting protocol

Task Hunting is independent from the custom Task Board. It uses the native
Hunting Task packet shapes consumed by AstraClient.

| Direction | Opcode | Purpose |
| --- | --- | --- |
| Server -> client | `0xBA` | Task Hunting base data: creature difficulty, rewards and current prices. |
| Server -> client | `0xBB` | One Task Hunting slot state. Login sends all three immediately after `0xBA`. |
| Client -> server | `0xBA` | Task Hunting action: `slot(U8), action(U8), upgraded(U8), raceId(U16)`. |
| Server -> client | `0x53` | Custom Task Board data. Subtypes: `0x00` Bounty, `0x01` Weekly, `0x02` Hunting Shop, `0x03` SoulSeal, `0x04` Bounty kill update, `0x05` Weekly kill update. |
| Client -> server | `0x5F` | Custom Task Board action. SoulSeal fight is action `19` with `raceId(U16)`. |
| Server -> client | `0xEE` | Resource balance. |

The legacy 8.60 anti-DLL check also historically used `0xBB`. It is now
explicitly disabled for Astra sessions, so `0xBB` is unambiguous for the
Astra Task Hunting parser. Classic non-Astra clients retain their legacy
anti-DLL packet and never receive Task Hunting packets.

## Deployment note

Deploy the server and AstraClient protocol changes together. SoulSeal data was
moved from the old standalone `0xBA` path to Task Board `0x53` subtype `0x03`
so Task Hunting can own `0xBA`/`0xBB`. A server with this change requires a
client that routes Task Board subtype `0x03` to the SoulSeal parser.

## Task Hunting slot states

| State | Value | Extra payload before reroll time |
| --- | --- | --- |
| Locked | `0` | `lockType(U8)` |
| Exhausted | `1` | none |
| Select | `2` | `count(U16)`, then `raceId(U16), bestiaryComplete(U8)` |
| Wildcard | `3` | same list payload as Select |
| Active | `4` | `raceId(U16), upgraded(U8), requiredKills(U16), currentKills(U16), rarity(U8)` |
| Redeem | `5` | same payload as Active |

Every `0xBB` ends with `timeUntilFreeReroll(U32)` in seconds.

## Synchronization rules

`TaskHunting.sendFullTaskHuntingSync(player)` sends, without `addEvent` or a
tab-dependent request:

1. `0xBA` base data;
2. `0xBB` for slots 0, 1 and 2;
3. required `0xEE` balances.

It runs on AstraClient login. Actions and kills only send the changed `0xBB`
slot plus balances.

## Manual validation

1. Log in with AstraClient and open Hunting Task: all slots must already be
   populated.
2. Reroll, select a creature, kill it, and claim the reward. Check that only
   the affected slot changes and Task Hunting Points update through `0xEE`.
3. Open SoulSeal beside the obelisk, start an encounter, and confirm it still
   works through Task Board subtype `0x03` / action `19`.
4. Open Bounty, Weekly Tasks and Hunting Shop to confirm their `0x53`
   subtypes remain unchanged.

Set `TaskHunting.DEBUG = true` in
`data/scripts/network/task_hunting/task_hunting.lua` temporarily to log the
base and slot sends while validating packets.
