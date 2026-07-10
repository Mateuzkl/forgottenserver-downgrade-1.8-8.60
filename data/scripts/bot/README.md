# Bot Players With Cast

This is the TFS 1.8 port inspired by the Canary bot/cast commits:

- `4dde9bc4b692903dcc59a932d1d565d5d2b74e7c`
- `3bf3fe6828b66512a07b9bdff007ad921f01b7d7`
- `9fde9411b8a950614efa4025160b8e585b75a4bb`
- `fc3b8f5288f7acb2cde7d5a953f689098e5640a6`
- `1ce8da72517d4ee7388017c6431037f4f1b9c198`

The Canary engine is much larger and tied to Canary-only APIs. This port keeps the safe runtime core for this server:

- registered bot players live in `bot_players`;
- `/bot add` creates the `botaccount` account and the requested player name when they do not exist;
- new bot accounts receive a generated password, and older public `botaccount` passwords are rotated automatically;
- bots are spawned as real `Player` objects loaded through `IOLoginData`;
- active bots are held by `std::shared_ptr<Player>` in `BotManager`;
- despawn uses the normal `Game::removeCreature` path, so logout hooks and player saving still run;
- cast uses the existing `ProtocolSpectator` system, so a bot can be watched through the normal cast list;
- socketless bots refresh their own pong inside `Player::sendPing`, so the `noPongKickTime` logout and the 7s attack-target drop never fire for them (spectator pings keep flowing);
- `BotBrain` equips starter gear without dropping it to the floor, disables loot/skill loss for bots, looks for nearby monsters, follows/attacks them, and wanders while idle;
- pure decision logic lives in `data/scripts/lib/bot_core.lua` and is covered by engine-free tests (`bash tests/lua/run.sh`).

## Config

```lua
botSystemEnabled = true
```

The system ships **disabled** (`botSystemEnabled = false` is the default); set it to `true` in `config.lua` to enable registration, spawn, auto-spawn, and the Lua brain loop.

Brain behaviour can be tuned from any script before startup, e.g.:

```lua
BotBrain.config = { tickInterval = 250, heal = { enabled = false } }
```

Missing keys are filled from `BotCore.defaults`.

## Commands

GOD command:

```text
/bot add name[, auto[, vocation]]
/bot remove name
/bot enable name
/bot disable name
/bot autospawn name, on|off
/bot spawn name[, cast]
/bot despawn name
/bot cast name, on|off
/bot list
/bot online
```

Example:

```text
/bot add Test Bot, auto
/bot spawn Test Bot, cast
```

`vocation` is optional. When omitted, a new bot is created as a knight.

Auto-spawned bots always open their cast on spawn; use `/bot cast name, off` to close it afterwards.

## SQL

Existing databases get the table from migration `data/migrations/55.lua`.

For manual import, run:

```sql
SOURCE data/scripts/bot/sql/00_bot_schema.sql;
```
