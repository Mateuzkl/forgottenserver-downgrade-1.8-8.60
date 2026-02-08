# 🌺 𝓜𝓲𝓵𝓵𝓱𝓲𝓸𝓻𝓮 𝓣𝓕𝓢 𝓓𝓸𝔀𝓷𝓰𝓻𝓪𝓭𝓮 🌺

[![Build status](https://ci.appveyor.com/api/projects/status/github/Mateuzkl/forgottenserver-downgrade?branch=official&svg=true)](https://ci.appveyor.com/project/Mateuzkl/forgottenserver-downgrade)

### 🐱 [Based nekiro downgrade.](https://github.com/nekiro/TFS-1.5-Downgrades)

**Version: TFS 1.7 | Protocol: 8.60**  
_Developed by MillhioreBT & Mateuzkl_

---

## 🚀 Overview

This is a **custom modified version** of The Forgotten Server, downgraded to protocol 8.60 but running on the modern TFS 1.7 engine. It includes numerous exclusive systems and improvements.

---

## 🎮 New Systems & Features

This custom version includes several enhanced systems and fully integrated features:

### 🛡️ AutoLoot System
- **Fully Integrated**: Built directly into the source for maximum performance.
- **Commands**: 
  - `!autoloot` - Opens the GUI management window.
  - `!autoloot on/off` - Quickly toggle the system.
  - `!autoloot clear` - Clear your loot list.
- **Bank Integration**: Automatically deposits gold if "AutoMoney" mode is enabled.

### ⚔️ Guild War System
- **Real Guild Wars**: Fully working war system with emblems.
- **Commands**: `/war invite, guildname`, `/war accept, guildname`.
- **Emblems**: Ally, Member, and Enemy emblems update in real-time.

### ⚔️ Tier & Classification System
- Items can have **Tier** and **Classification** attributes.
- **Upgrades**: Tier system allows for item upgrades and strong progression.
- **Classification**: System for categorizing items by rarity or power.
- Fully integrated with Lua scripting API for custom RPG systems.

### 🏆 Reward Boss System
- **Tibia-like Rewards**: Global Tibia style boss reward system.
- **Contribution Tracking**: Tracks damage done, damage taken, and healing done.
- **Fair Distribution**: Distributes loot based on contribution score.
- **Reward Chests**: Rewards are stored in reward containers (ID: 21518/21584).
- Configurable reward rates via config manager.

### 💤 Offline Training System
- Train skills while offline using beds (Premium required).
- **Skills**: Sword, Axe, Club, Distance, Shielding, and Magic Level.
- **Usage**: simply click on the bed to start training.
- Automatically calculates gain based on logout duration (Max 12h).
- **Configuration**: efficiency and vocation rates configurable in config.lua.

### 🏰 Guild Halls System
- **Guild Leaders** can purchase special Guild Halls.
- Paid via Guild Bank balance.
- **Features**: Supports all house features (doors, beds, protection).
- **Commands**:
  - `!buyhouse` - Purchase a guild hall (Leader/Vice-Leader).
  - `!leavehouse` - Abandon the hall.

### 🛡️ House Protection System
- **Per-house control**: Owners can toggle protection state.
- **Secure**: When enabled, ONLY owner and guests can move items.
- **Commands**:
  - `!protecthouse on/off` - Toggle protection.
  - `!houseguest add/remove/list` - Manage safe list.
- Door messages show real ownership info.

### ⚡ Improved Decay System
- Enhanced decay system for better server performance.
- Optimized item decay processing and state management.

### 📺 Live Cast System
- Stream your gameplay (`!cast`) for others to watch.
- **Bonus**: Configurable EXP bonus for active casters.
- **Spectators**: Can chat in the Live Cast channel.
- **Commands**: `/spectators`, `/kick`, `/mute`, `/ban` (for broadcasters).

---

## 🛠️ Compilation

### 🐧 Ubuntu 22.04 / 24.04

```bash
# Install dependencies
sudo apt install git cmake build-essential libluajit-5.1-dev libmysqlclient-dev libboost-system-dev libboost-iostreams-dev libboost-filesystem-dev libboost-locale-dev libpugixml-dev libfmt-dev libssl-dev libspdlog-dev -y

# Clone and compile
git clone -b Revscrypt-full --single-branch https://github.com/Mateuzkl/forgottenserver-downgrade-1.7-8.60.git
cd forgottenserver-downgrade-1.7-8.60
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 🪟 Windows

Recommended usage with **vcpkg**. See [Wiki Guide](https://github.com/MillhioreBT/forgottenserver-downgrade/wiki/Compiling-on-Windows-(vcpkg)).

---

## 📦 Client Configuration (OTCv8)

To fully utilize features like extended sprites and mounts, updates are required:

**1. Update `modules/game_features/game_features.lua`:**

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
    g_game.enableFeature(GameDoubleExperience)
    g_game.enableFeature(GameOfflineTrainingTime)
    g_game.enableFeature(GameDoubleSkills)
    g_game.enableFeature(GameBaseSkillU16)
    g_game.enableFeature(GameAdditionalSkills)
    g_game.enableFeature(GameIdleAnimations)
    g_game.enableFeature(GameEnhancedAnimations)
    g_game.enableFeature(GameSpritesU32)
    g_game.enableFeature(GameExtendedClientPing)
    g_game.enableFeature(GameChangeMapAwareRange)
    g_game.enableFeature(GameDoublePlayerGoodsMoney)
end
```

**2. Extended Sprites (GameSpritesU32)**
- Download the sprites: [860-otcv-revisado2.rar](https://www.mediafire.com/file/iyas69j0sanddyr/860-otcv-revisado2.rar/file)
- Extract `.spr` and `.dat` to your OTCv8 directory.

> **Client With DLL (Mounts)**: [Download Client 8.60 + DLL](https://github.com/Mateuzkl/Client-cip-8.60-with-DLL-Mount).*

---

## 🎮 Extended Options for Modified 8.60 CIP Clients

This server supports extended protocol features for **modified 8.60 CIP clients** using DLL patches. These features allow the old Tibia client to handle values beyond the original protocol limitations:

| Feature | Status | Description |
|---------|--------|-------------|
| `__MAGIC_EFFECTS_U16__` | ✅ OK | Supports magic effects up to 65535 (uint16) instead of 255 |
| `__DISTANCE_SHOOT_U16__` | ✅ OK | Supports distance/projectile effects up to 65535 (uint16) instead of 255 |
| `__PLAYER_HEALTH_U32__` | ✅ OK | Supports player health values up to 4.2 billion (uint32) instead of 65535 |
| `__PLAYER_MANA_U32__` | ✅ OK | Supports player mana values up to 4.2 billion (uint32) instead of 65535 |
| `__PLAYER_SKILLS_U16__` | ⏳ Pending | Skill levels up to 65535 (requires implementation) |
| **Outfit Limit Changer** | ✅ OK | Allows more than 255 outfits (uses uint16 for outfit count) |
---

## 🐛 Contributing & Issues

Found a bug? Please report it on our [Issues Page](https://github.com/MillhioreBT/forgottenserver-downgrade/issues).
Pull requests are welcome!

_Maintained by Mateuzkl._

---

## 📢 Estado do Projeto e Doações

Hoje marca um ponto importante no desenvolvimento deste projeto. Com mais de **500 commits** realizados, entregamos uma base robusta e repleta de funcionalidades exclusivas.

Entretanto, até o momento, não houve colaboração significativa da comunidade — sem report de bugs, sem Pull Requests e sem ajuda nas correções. Por conta disso, o desenvolvimento ativo está sendo **pausado** hoje. A base permanecerá como está, com todo o conteúdo atual.

Possuo diversos sistemas bônus de alto nível prontos (como **Market Idêntico ao Global**, **Forge System**, entre outros). Porém, **não irei liberá-los gratuitamente**. O desenvolvimento e adaptação desses sistemas exigem muito trabalho e dedicação, e não faz sentido entregar tudo de graça sem retorno. 

**Se a comunidade ajudar financeiramente, eu trarei esses sistemas para a base.** Caso contrário, o projeto segue como está.

### 🤝 Como Apoiar o Projeto

Se você deseja ver este projeto evoluir, receber novas atualizações, correções e sistemas exclusivos, sua contribuição é essencial. Doações incentivam a manutenção e o aprimoramento contínuo da base TFS 1.7.

**Chave PIX (Aleatória) para Doações:**
Basta copiar a chave abaixo e colar no seu banco na opção **Chave Aleatória**. Você pode doar o valor que quiser!

`f8761afe-5581-417d-afc8-08cac410a1b0`

Agradeço imensamente a quem puder contribuir para manter este projeto vivo e melhorando!

---

## 📥 How to Download the Latest Client (Executable & DLLs) / Client Updater

If you need the ready-to-use **executable** and **DLLs**, follow these simple steps:

1.  **Click this link**: [Project on AppVeyor](https://ci.appveyor.com/project/Mateuzkl/forgottenserver-downgrade-1-7-8-60)
2.  Look for the **"Artifacts"** tab (or click on the latest Job specific to your platform, often named `Environment: ...` or `Configuration: ...`, then find the "Artifacts" tab there).
    *   *Note: If you are on the main project page, you might need to click on the latest "Job" listed under "Job name" first.*
3.  **Download** the files listed there (usually a `.zip` or `.7z` file containing the executable and DLLs).
4.  **Extract** the downloaded files into your client folder, replacing existing ones if necessary.

---

## 🔄 Client Updater (Automated)

To easily update your client files (CipSoft client, executables, DLLs), follow our simple tutorial: 

*   **[Click here for the Client Updater Tutorial](https://github.com/Mateuzkl/Client_Mout_Updater)**

This guide explains how to automatically update everything with just a few clicks.


