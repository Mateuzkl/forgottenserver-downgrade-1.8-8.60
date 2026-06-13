# Lua 5.4 Garbage Collector - TFS Guide

## What is Lua GC?

Lua 5.4 uses an automatic garbage collector that frees memory no longer referenced.
Unlike C++, Lua tracks all objects (tables, strings, functions, userdata) and reclaims
them when no references remain.

## GC Modes

Lua 5.4 supports three GC modes, configured via `luaGcMode` in `config.lua`:

| Mode          | Description                                        | Best For               |
|---------------|----------------------------------------------------|------------------------|
| `generational`| Minor GCs collect short-lived objects fast. Major GCs run less often. | Long-running servers   |
| `incremental` | GC runs in small steps spread over time, reducing lag spikes. | Balanced performance   |
| `stopped`     | GC is stopped; only manual collections run.        | Debug/testing only     |

**TFS default: `generational`** - optimal for servers with many temporary Lua objects.

## Why NOT Full GC Every Second

- `lua_gc(L, LUA_GCCOLLECT)` is a **stop-the-world** operation
- On large states (50+ MB), it can pause the server for 50-200ms
- TFS uses **incremental steps** (`LUA_GCSTEP`) to spread work over time
- Full collects are reserved for startup, shutdown, and admin commands

## Configuration

All GC settings are in `config.lua`:

```lua
luaGcMode = "generational"       -- "generational", "incremental", or "stopped"
luaGcAutoTune = true             -- auto-configure GC parameters on startup
luaGcLogEnabled = false          -- periodic memory logs
luaGcLogInterval = 60000         -- log interval in ms (60s)
luaGcStepEnabled = true          -- run incremental GC steps
luaGcStepInterval = 1000         -- step interval in ms (1s)
luaGcStepSize = 200              -- step size (KB to process per step)
luaGcFullCollectOnStartup = true -- full GC after loading all scripts
luaGcFullCollectOnShutdown = true-- full GC before closing Lua state
luaGcWarnMemoryKb = 262144       -- warn threshold (256 MB)
luaGcCriticalMemoryKb = 524288   -- critical threshold (512 MB)
```

## Admin Commands

Use `/luagc` as GOD (account type 6):

```text
/luagc          - Show memory usage, mode, step config
/luagc collect  - Force full GC collect
/luagc step     - Run one GC step
/luagc memory   - Show memory before/after full collect
/luagc mode     - Show current GC mode
```

## Lua API

Available in scripts (for admin/debug use):

```lua
local kb = Game.getLuaMemoryUsage()   -- current memory in KB
Game.collectLuaGarbage()              -- force full GC
Game.stepLuaGarbage(size)             -- run incremental step (optional size)
```

## GcDebug Utility

Load via `dofile('data/lib/core/gc_debug.lua')` or use the auto-loaded table:

```lua
GcDebug.memory()              -- formatted memory string
GcDebug.collect("reason")     -- collect and log before/after
GcDebug.step(200)             -- step with custom size
GcDebug.snapshot("label")     -- save memory snapshot
GcDebug.diff("start", "end")  -- compare two snapshots
```

## Best Practices for Scripts

### DO: Store IDs, not objects

```lua
-- GOOD
local cache = {}
cache[player:getId()] = { guid = player:getGuid(), name = player:getName() }

-- BAD
local cache = {}
cache[player:getId()] = player  -- holds userdata forever!
```

### DO: Use Creature(cid) in addEvent callbacks

```lua
-- GOOD
addEvent(function(cid)
    local creature = Creature(cid)
    if creature then
        creature:addHealth(5000)
    end
end, 5000, creature:getId())

-- BAD - closure captures creature userdata directly
addEvent(function()
    creature:addHealth(5000)  -- creature may be dead/gone!
end, 5000)
```

### DO: Clean up on logout/death

```lua
-- In creaturescripts:
function onLogout(player)
    local id = player:getId()
    myCache[id] = nil
    return true
end
```

### DO: Use local for file-scope variables

```lua
-- GOOD
local myTable = {}

-- BAD - pollutes _G table forever
myTable = {}
```

### DON'T: Store heavy userdata in global tables

```lua
-- BAD
globalPlayers = {}  -- _G.globalPlayers holds players forever

-- GOOD
local trackedIds = {}  -- just store IDs
```

## Diagnosing Leaks

1. Enable logs: `luaGcLogEnabled = true`
2. Watch memory over time: if it grows continuously without dropping, there's a leak
3. Use `/luagc memory` to see how much a full collect frees
4. Check scripts for patterns that hold objects in global/upvalue scope
5. Check `data/lib/core/gc_debug.lua` for snapshot/diff tools

## Common Leak Sources

- **Global tables** storing player/monster/item objects directly
- **addEvent closures** capturing userdata (even if param is ID!)
- **Registry references** from C++ `luaL_ref` without matching `luaL_unref`
- **CreatureEvent callbacks** that store `self` in external tables
- **NPC onThink** accumulating data without cleanup
- **File-scope variables** in spell scripts that grow over time (shared mutable state)

## Important Rules

- NEVER call `Game.collectLuaGarbage()` in a fast loop or timer
- NEVER call `Game.stepLuaGarbage()` from non-dispatcher threads
- Lua state is NOT thread-safe - only access from the main dispatcher thread
- Full collects are safe at startup and shutdown only
- Use incremental steps for runtime GC management
