# Bot Players With Cast

This is the TFS 1.8 port inspired by the Canary bot/cast commits:

- `4dde9bc4b692903dcc59a932d1d565d5d2b74e7c`
- `3bf3fe6828b66512a07b9bdff007ad921f01b7d7`
- `9fde9411b8a950614efa4025160b8e585b75a4bb`
- `fc3b8f5288f7acb2cde7d5a953f689098e5640a6`
- `1ce8da72517d4ee7388017c6431037f4f1b9c198`

The Canary engine is much larger and tied to Canary-only APIs. This port keeps the safe runtime core for this server:

- registered bot players live in `bot_players`;
- bots are spawned as real `Player` objects loaded through `IOLoginData`;
- active bots are held by `std::shared_ptr<Player>` in `BotManager`;
- despawn uses the normal `Game::removeCreature` path, so logout hooks and player saving still run;
- cast uses the existing `ProtocolSpectator` system, so a bot can be watched through the normal cast list.

## Commands

GOD command:

```text
/bot add name[, auto]
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

## SQL

Existing databases get the table from migration `data/migrations/55.lua`.

For manual import, run:

```sql
SOURCE data/scripts/bot/sql/00_bot_schema.sql;
```
