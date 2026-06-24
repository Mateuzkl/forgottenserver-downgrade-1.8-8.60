# Optional multi-world

Multi-world is disabled by default. With `multiWorld = false`, the server keeps
the legacy single-world login packet, ports, configuration and SQL paths.

When enabled, each server process selects one row from `worlds` through
`worldId`. Accounts remain global while characters, VIP entries, houses,
market offers/history, casts and highscores are scoped to a world. The database
migration is global and must run successfully before starting a multi-world
process.

## Add a second world

Run the migration by starting the server once, then add the world row:

```sql
INSERT INTO worlds (id, name, type, motd, location, ip, port, port_status, creation)
VALUES (2, 'Pex World 2', 'pvp', 'Welcome to World 2', 'South America', '127.0.0.1', 7173, 7174, UNIX_TIMESTAMP());
```

Characters assigned to that server need `world_id = 2`. Character IDs remain
globally unique, so child tables such as `player_items`, depot and inbox data
continue to be isolated through `player_id`.

### Choosing the world in the Account Manager

When `multiWorld = true` and more than one row exists in `worlds`, the in-game
Account Manager adds an extra step to character creation: after the name, sex
and (optional) vocation are confirmed it asks *"In which world should this
character be created?"* and lists the available worlds by id and name. The
answer (id number or world name) is stored as the new character's `world_id`,
so a player connected to the world 1 login service can create a character
directly on world 2, 3, etc. With a single world (or `multiWorld = false`) the
step is skipped and the legacy flow is preserved.

The per-account character limit and the name-availability prompt during name
entry are still evaluated against the world hosting the Account Manager; the
authoritative per-world name check happens at creation time, so a duplicate
name in the chosen world fails the creation safely.

## Example configurations

World 1 owns the shared login service:

```lua
multiWorld = true
multiWorldLoginServer = true
worldId = 1
loginProtocolPort = 7171
```

World 2 uses a distinct game/status endpoint and does not bind the shared
login port:

```lua
multiWorld = true
multiWorldLoginServer = false
worldId = 2
```

`gameProtocolPort` and `statusProtocolPort` are not used for a multi-world
instance; their effective values come from the selected `worlds` row. The
`worldGamePort` and `worldStatusPort` config values only seed an absent world
row during first startup.
