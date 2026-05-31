<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:0d0221,40:2d0b6e,70:5b1fa8,100:8b5cf6&height=240&section=header&text=%E2%9A%94%EF%B8%8F%20TFS%201.8%20Downgrade&fontSize=52&fontColor=ffffff&fontAlignY=40&desc=Protocol%208.60%20%7C%20Modern%20Engine%20%7C%20ClientID%20Native&descAlignY=62&descSize=17&animation=fadeIn" alt="TFS 1.8 Downgrade" />
</p>

<div align="center">

[![Build Status](https://ci.appveyor.com/api/projects/status/github/Mateuzkl/forgottenserver-downgrade?branch=official&svg=true)](https://ci.appveyor.com/project/Mateuzkl/forgottenserver-downgrade)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)](LICENSE)
[![Commits](https://img.shields.io/badge/commits-1000%2B-6a0dad?style=flat-square)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/commits)
[![Wiki](https://img.shields.io/badge/docs-wiki-8b5cf6?style=flat-square&logo=wikipedia&logoColor=white)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki)

<br />

![Engine](https://img.shields.io/badge/ENGINE-TFS%201.8-7c3aed?style=for-the-badge)
![Protocol](https://img.shields.io/badge/PROTOCOL-8.60-f97316?style=for-the-badge)
![C++](https://img.shields.io/badge/C++-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Lua](https://img.shields.io/badge/Lua-5.5-2C2D72?style=for-the-badge&logo=lua&logoColor=white)
![MariaDB](https://img.shields.io/badge/MariaDB-003545?style=for-the-badge&logo=mariadb&logoColor=white)
![Ubuntu](https://img.shields.io/badge/Ubuntu-22.04%2B-E95420?style=for-the-badge&logo=ubuntu&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-vcpkg-0078D4?style=for-the-badge&logo=windows&logoColor=white)

<br />
<br />

**TFS 1.8 Downgrade** brings the classic Tibia **8.60** protocol to a modern, optimized server engine, with native ClientID support and a large set of custom systems.

Developed and maintained by [Mateuzkl](https://github.com/Mateuzkl), based on [Nekiro's TFS 1.5 Downgrades](https://github.com/nekiro/TFS-1.5-Downgrades) and forked from [MillhioreBT's downgrade](https://github.com/MillhioreBT/forgottenserver-downgrade).

[Discord Community](https://discord.com/invite/GxTm7DyXVe) · [Full Wiki](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki) · [Issues](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/issues)

</div>

---

## Highlights

| Area | Features |
|---|---|
| Core | TFS 1.8 engine, protocol 8.60, C++23, Lua 5.5, MariaDB, optimized decay |
| ClientID | Native `.dat` loading, no ServerID/ClientID conversion layer |
| RPG systems | Forge, Imbuements, Reward Boss, Monk Harmony, Offline Training |
| World systems | Instances, Zones, Guild Halls, House Protection, Live Cast |
| Security & tools | Token Protection, System Spy, AutoLoot, Store Inbox support |
| Clients | OTCv8/Mehah features, extended sprites, modified CIP DLL options |

---

## ClientID Native

This source uses **ClientID everywhere**: items, maps, Lua scripts and database records all use the same IDs seen by the client.

| What changes | Benefit |
|---|---|
| Reads item definitions from the client `.dat` | Easier content editing |
| Removes ServerID/ClientID mapping | Fewer conversion bugs |
| OTBM maps use ClientID directly | Better compatibility with modern workflows |

Use the modified map editor:

**[Download RME-CLIENTID](https://github.com/Mateuzkl/RME-CLIENTID)**

Map tutorials:

| Tutorial | Description |
|---|---|
| [ServerID to ClientID Conversion](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki/Map-Conversion-Tutorial-%E2%80%90-ServerID-to-ClientID) | Convert standard TFS maps |
| [Import Canary/Crystal Maps](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki/Importing-Canary-Crystal-Maps-to-RME-8.60) | Downgrade and import modern maps |
| [Importar Mapas Canary/Crystal](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki/%F0%9F%97%BA%EF%B8%8F-Importando-Mapas-Canary-Crystal-para-RME-8.60) | Tutorial em portugues |

---

## Systems

<details>
<summary><b>AutoLoot</b></summary>

Built into the C++ source for high performance, with optional automatic gold deposit through AutoMoney.

| Command | Description |
|---|---|
| `!autoloot` | Open the management window |
| `!autoloot on/off` | Toggle AutoLoot |
| `!autoloot clear` | Clear your loot list |

</details>

<details>
<summary><b>Guild War</b></summary>

Real guild wars with live ally, member and enemy emblems.

| Command | Description |
|---|---|
| `/war invite, guildname` | Invite a guild |
| `/war accept, guildname` | Accept a war |

</details>

<details>
<summary><b>Reward Boss</b></summary>

Tibia-like boss reward system with contribution tracking and reward chests.

| Tracking | Description |
|---|---|
| Damage Done | DPS contribution |
| Damage Taken | Survivability contribution |
| Healing Done | Support contribution |
| Fair Distribution | Loot based on total score |

</details>

<details>
<summary><b>Offline Training</b></summary>

Train skills while offline using beds. Premium is required, and gains are calculated from logout duration up to 12 hours.

Trainable skills: Sword, Axe, Club, Distance, Shielding and Magic Level.

</details>

<details>
<summary><b>Guild Halls</b></summary>

Guild leaders can buy special halls using the guild bank balance. Guild halls support normal house features such as doors, beds and protection zones.

| Command | Access | Description |
|---|---|---|
| `!buyhouse` | Leader / Vice-Leader | Buy a guild hall |
| `!leavehouse` | Leader | Leave the hall |

</details>

<details>
<summary><b>House Protection</b></summary>

Owners can protect each house independently. When enabled, only the owner and listed guests can move items.

| Command | Description |
|---|---|
| `!protecthouse on/off` | Toggle protection |
| `!houseguest add <name>` | Add guest |
| `!houseguest remove <name>` | Remove guest |
| `!houseguest list` | Show guests |

</details>

<details>
<summary><b>Live Cast</b></summary>

Players can stream gameplay with `!cast`, receive configurable EXP bonuses, and chat with spectators in a dedicated channel.

| Command | Access | Description |
|---|---|---|
| `!cast` | Caster | Start or stop broadcasting |
| `/spectators` | Caster | List spectators |
| `/kick <name>` | Caster | Kick spectator |
| `/mute <name>` | Caster | Mute spectator |
| `/ban <name>` | Caster | Ban spectator |

</details>

<details>
<summary><b>Improved Decay</b></summary>

Optimized item decay processing and state management to reduce CPU usage on populated servers.

</details>

<details>
<summary><b>Token Item Protection</b></summary>

Locks important items to the character to reduce theft risk when sharing accounts or recovering from compromised access.

| Command | Description |
|---|---|
| `!token set, YOUR_TOKEN` | Set your secret token while disabled |
| `!token on` | Enable protection |
| `!token off, YOUR_TOKEN` | Disable protection |

> Keep your token secret. It is required only to disable protection.

</details>

<details>
<summary><b>System Spy</b></summary>

Administrative tool for GODs and Admins to silently inspect a player session, including viewport, inventory and open containers.

| Command | Permission | Description |
|---|---|---|
| `/spy <name>` | GOD / Admin | Observe a player |
| `/spyinv <name>` | GOD / Admin | View inventory and containers |
| `/unspy` | GOD / Admin | Stop observing |

</details>

<details>
<summary><b>Magic Roulette</b></summary>

Interactive roulette stations with animated map effects, configurable rewards and database tracking.

![Magic Roulette](https://user-images.githubusercontent.com/40324910/236821618-63cb56a4-3003-4156-a05f-02375649fe55.gif)

Configuration file: `data/scripts/magic-roulette-master/configroulette.lua`

</details>

<details>
<summary><b>Forge System</b></summary>

![Forge System](tools/forge.gif)

Fuse two identical items to upgrade one of them by **+1 Tier**. Failures destroy one item and keep the other at its current tier.

| Tier | Success Rate | Extra Materials |
|:---:|:---:|:---:|
| T0 to T1 | 50% | - |
| T1 to T2 | 40% | - |
| T2 to T3 | 30% | - |
| T3 to T4 | 25% | - |
| T4 to T5 | 20% | - |
| T5 to T6 | 15% | Exalted Core |
| T6 to T7 | 10% | Exalted Core |
| T7 to T8 | 8% | Exalted Core |
| T8 to T9 | 5% | Exalted Core |

Materials: Forge Dust, Silver Tokens and Exalted Cores.

| Command | Description |
|---|---|
| `/forge info` | Show forge info |
| `/forge dust` | Check Forge Dust |
| `/forge silver` | Check Silver Tokens |

</details>

<details>
<summary><b>Imbuement System</b></summary>

Supports scroll-based imbuements and raw material crafting through an Etcher/workbench flow.

![Scroll Method](tools/imbuments_scroll.gif)

![Raw Materials Method](tools/imbuments_items.gif)

| Feature | Description |
|---|---|
| Workbench | 1 equipment slot plus up to 3 material slots |
| Owner Lock | Only the item owner can apply imbuements |
| Tiers | Flawed, Intricate and Powerful |
| Types | 27 imbuements, including skills, leech, critical, elemental protection and more |

</details>

<details>
<summary><b>Instance System</b></summary>

Private dungeon instances where monsters, effects and items are visible only to players in the same instance.

```lua
player:setInstanceId(id)
Game.registerInstanceArea(area)
Game.createMonster(name, pos, extended, force, master, instanceId)
```

Use it for solo dungeons, party rooms, boss fights and quests without affecting the main world.

</details>

<details>
<summary><b>Zones System</b></summary>

Configurable geographical zones for safe areas, PvP arenas, custom events and territory control.

Key APIs:

```lua
zone:getCreatures([type])
zone:getItems([itemId])
zone:getTiles([flags])
Creature:onChangeZone(fromZone, toZone)
```

Zones can be loaded from `data/world/world-zones/1.xml` or created dynamically in Lua.

</details>

<details>
<summary><b>Harmony System - Monk Vocation</b></summary>

Exclusive to Monks. Harmony points are gained by builder spells and spent by stronger finishing spells.

| Points | Base Bonus | With Virtue of Harmony |
|:---:|:---:|:---:|
| 0 | 0% | 0% |
| 1 | +15% | +25% |
| 2 | +30% | +55% |
| 3 | +60% | +100% |
| 4 | +120% | +200% |
| 5 | +240% | +400% |

Command:

| Command | Description |
|---|---|
| `!harmony` | Show points, active virtue, serene state and bonus |

</details>

---

## Optional Systems

Some custom systems are disabled by default in `config.lua` and `config.lua.dist`, keeping the server closer to classic 8.60 behavior.

| Config key | Default | Controls |
|---|---|---|
| `forgeSystemEnabled` | `false` | Forge tiers, commands, portal and item look text |
| `imbuementSystemEnabled` | `false` | Imbuements, scrolls, workbench, portal and item look text |
| `monkVocationEnabled` | `false` | Monk vocation behavior, outfits, spells and checks |
| `familiarSystemEnabled` | `false` | Familiar summons, spells and remaining-time look text |
| `wheelSystemEnabled` | `false` | Wheel of destiny, increase stats comp HP, mana, cap or mitigation. |
| `bestiarySystemEnabled` | `false` | Custom Cyclopedia/Bestiary packets, kills, tracker and charms |
| `marketSystemEnabled` | `false` | Custom OTC market packets, market tables and market depot item |
| `preySystemEnabled` | `false` | Custom Prey packets, prey bonuses, timers and wildcard usage |
item |
| `monsterLevelEnabled` | `false` | Monsters with certain levels, increasing HP, damage, and experience |

Set a system to `true` only when you want that feature active:

Example:

```lua
forgeSystemEnabled = false
imbuementSystemEnabled = true
monkVocationEnabled = false
familiarSystemEnabled = false
wheelSystemEnabled = false
bestiarySystemEnabled = true
marketSystemEnabled = false
preySystemEnabled = false
```

---

## Compilation

## **IMPORTANT / ATENCAO**

### **RECOMMENDED: USE THE AUTOMATIC BUILD SCRIPT**

For Ubuntu 22.04 / 24.04 or WSL, run:

```bash
chmod +x build.sh
./build.sh
```

The script detects your Ubuntu version, lets you choose the language, installs missing dependencies, prepares Lua 5.5, Boost, simdutf and builds the server in Release mode.

Useful examples:

```bash
./build.sh --lang pt
./build.sh --lang en --ubuntu 24.04
./build.sh --lang es --ubuntu 22.04 --clean
```

After the build finishes, run:

```bash
./tfs
```

Manual compilation notes are kept below only for advanced/custom setups.

### Ubuntu 22.04 / 24.04

Manual build requirements:

- Ubuntu 24.04 is recommended.
- Ubuntu 22.04 works, but its system Boost can be older than the required version when HTTP support is enabled. In that case, use `./build.sh` or install a newer Boost manually.
- Lua 5.5 is installed manually because Ubuntu does not ship it as a normal apt package.
- `simdutf` is installed manually into `$HOME/.local`.

Install system dependencies:

```bash
sudo apt update
sudo apt install -y \
  git wget cmake build-essential pkg-config \
  libmysqlclient-dev \
  libboost-system-dev libboost-iostreams-dev libboost-filesystem-dev \
  libboost-locale-dev libboost-regex-dev libboost-json-dev \
  libpugixml-dev libfmt-dev libssl-dev libspdlog-dev libmimalloc-dev \
  libabsl-dev
```

Install Lua 5.5:

```bash
cd /tmp
wget https://www.lua.org/ftp/lua-5.5.0.tar.gz
tar -xzf lua-5.5.0.tar.gz
cd lua-5.5.0

make linux
sudo make install

lua -v
```

The build command below passes Lua explicitly. Keep these CMake flags when building manually:

```bash
-DLUA_INCLUDE_DIR=/usr/local/include \
-DLUA_LIBRARY=/usr/local/lib/liblua.a \
-DLUA_LIBRARIES="/usr/local/lib/liblua.a;m;dl" \
-DLUA_VERSION_STRING=5.5.0
```

Install `simdutf`:

```bash
cd ~
git clone https://github.com/simdutf/simdutf.git
cd simdutf
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -- -j"$(nproc)"
cmake --install build
```

Build the server in Release mode:

```bash
git clone https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60
cd forgottenserver-downgrade-1.8-8.60

rm -rf build-release
mkdir build-release
cd build-release

cmake -DCMAKE_BUILD_TYPE=Release \
  -DDISABLE_STATS=1 \
  -DENABLE_SLOW_TASK_DETECTION=ON \
  -DUSE_MIMALLOC=ON \
  -DLUA_INCLUDE_DIR=/usr/local/include \
  -DLUA_LIBRARY=/usr/local/lib/liblua.a \
  -DLUA_LIBRARIES="/usr/local/lib/liblua.a;m;dl" \
  -DLUA_VERSION_STRING=5.5.0 \
  -DCMAKE_PREFIX_PATH="/usr/local;$HOME/.local" \
  ..

cmake --build . -- -j"$(nproc)"
```

| CMake flag | Effect |
|---|---|
| `-DCMAKE_BUILD_TYPE=Release` | Enables optimized release build |
| `-DDISABLE_STATS=1` | Removes runtime stats overhead |
| `-DENABLE_SLOW_TASK_DETECTION=ON` | Enables slow task diagnostics |
| `-DUSE_MIMALLOC=ON` | Uses Microsoft's mimalloc allocator |
| `-DCMAKE_PREFIX_PATH="/usr/local;$HOME/.local"` | Helps CMake find manual Lua and simdutf installs |

For memory/lifetime pull requests, install the Boost.Test development package and run the Linux ASAN test preset:

```bash
sudo apt install -y libboost-test-dev
cmake --preset asan-linux-tests
cmake --build --preset asan-linux-tests -- -j"$(nproc)"
ctest --preset asan-linux-tests --output-on-failure
```

Optional Linux tuning:

```bash
sudo cpupower frequency-set -g performance
sudo nice -n -10 ./tfs
```

### Windows

Install vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

Build with **Visual Studio 2022** or newer. Choose backend, platform and build normally; dependencies are installed through vcpkg.

Full guide: [Windows Compilation Wiki](https://github.com/MillhioreBT/forgottenserver-downgrade/wiki/Compiling-on-Windows-(vcpkg))

---

## Client Configuration

### 📢 Client Update: Version 15.24

**Client version upgraded to 15.24.** The items.otb and sprite files (.spr and .dat) have been updated to support version 15.24 for both OTCv8 and the classic CIP client. Download the latest assets below.

### OTCv8 / Mehah

Enable the required features for protocol 860 in `modules/game_features/game_features.lua`:

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
    g_game.enableFeature(GameLeechAmount)
    g_game.enableFeature(GameIdleAnimations)
    g_game.enableFeature(GameEnhancedAnimations)
    g_game.enableFeature(GameExtendedClientPing)
    g_game.enableFeature(GameSpritesU32)
    g_game.enableFeature(GameDoublePlayerGoodsMoney) 
    g_game.enableFeature(GameCreatureIcons)
    g_game.enableFeature(GamePurseSlot)
    g_game.enableFeature(GamePrey)
    g_game.enableFeature(GameSpellList)
    g_game.enableFeature(GameThingUpgradeClassification)
    g_game.enableFeature(GameItemTierByte)
end
```

Store Inbox compatibility:

| Client | Behavior |
|---|---|
| OTCv8 / Mehah | Enable `GamePurseSlot` for protocol 860 |
| Original CIP 8.60 | Use `!storeinbox`, `!sinbox` or `!inbox` |
| Close inbox | Use `!fecharinbox` or `!closeinbox` |

Extended sprites:

- [Otcv8--Classic-8.6](https://github.com/Mateuzkl/Otcv8--Classic-8.6)
- [OTC-Fonticak (Fork Mehah)](https://github.com/soyfabi/OTC-Fonticak)
- Extract `.spr` and `.dat` to your OTC directory.

CIP client with mounts:

- [Client 8.60 + DLL Mount](https://github.com/Mateuzkl/Client-cip-8.60-with-DLL-Mount)

CIP client, future client (<span style="color: green;">Work Progress</span>)
- [Forgotten Client (Alpha 1.0)](https://github.com/Mateuzkl/The-Forgotten-Client)

### Modified CIP DLL Options

| DLL patch | Status | Extends |
|---|:---:|---|
| `__MAGIC_EFFECTS_U16__` | Done | Magic effects: 255 to 65535 |
| `__DISTANCE_SHOOT_U16__` | Done | Projectile effects: 255 to 65535 |
| `__PLAYER_HEALTH_U32__` | Done | Player HP: 65535 to 4.2 billion |
| `__PLAYER_MANA_U32__` | Done | Player mana: 65535 to 4.2 billion |
| `__PLAYER_SKILLS_U16__` | Pending | Skill levels up to 65535 |
| Outfit Limit Changer | Done | Outfits: 255 to 65535+ |

---

## Downloads

### Manual AppVeyor artifacts

1. Open the [AppVeyor project page](https://ci.appveyor.com/project/Mateuzkl/forgottenserver-downgrade-1-7-8-60).
2. Click the latest job.
3. Open the **Artifacts** tab.
4. Download the `.zip` or `.7z` package.
5. Extract it into your client folder.

### Client updater

Update the CipSoft client, executables and DLLs automatically:

**[Client Updater Tutorial - Mateuzkl/Client_Mout_Updater](https://github.com/Mateuzkl/Client_Mout_Updater)**

---

## Contributing

The base is stable and actively maintained. Bug reports and pull requests are welcome.

When opening an issue, include:

- Clear description
- Steps to reproduce
- Relevant logs, crash output or Valgrind output

Pull requests should stay focused and change only what is necessary.

---

## Community & Support

Join the Discord to discuss ideas, report bugs, share systems, keep the 8.60 ecosystem alive and follow project updates:

**[Join the Discord Community](https://discord.gg/GxTm7DyXVe)**

If this project helps you, donations are appreciated and help support continued development.

```text
PIX key: f8761afe-5581-417d-afc8-08cac410a1b0
```

<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:8b5cf6,50:5b1fa8,100:0d0221&height=140&section=footer&text=Made%20with%20%F0%9F%92%9C%20by%20Mateuzkl&fontSize=18&fontColor=ffffff&fontAlignY=65" alt="Made by Mateuzkl" />
</p>
