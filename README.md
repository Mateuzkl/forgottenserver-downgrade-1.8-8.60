<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:0d0221,40:2d0b6e,70:5b1fa8,100:8b5cf6&height=240&section=header&text=%E2%9A%94%EF%B8%8F%20TFS%201.8%20Downgrade&fontSize=52&fontColor=ffffff&fontAlignY=40&desc=Protocol%208.60%20%7C%20Modern%20Engine%20%7C%20ClientID%20Native&descAlignY=62&descSize=17&animation=fadeIn" alt="TFS 1.8 Downgrade" />
</p>

<div align="center">

[![Discord](https://img.shields.io/discord/1498442075442516120.svg?style=flat-square&logo=discord&label=Discord&color=5865F2)](https://discord.gg/9ne5TZapd)
[![CI](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/actions/workflows/build.yml/badge.svg)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/actions/workflows/build.yml)
![Repository size](https://img.shields.io/github/repo-size/Mateuzkl/forgottenserver-downgrade-1.8-8.60?style=flat-square)
[![License](https://img.shields.io/github/license/Mateuzkl/forgottenserver-downgrade-1.8-8.60.svg?style=flat-square)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/blob/main/LICENSE)
[![Commits](https://img.shields.io/badge/commits-1000%2B-6a0dad?style=flat-square)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/commits)
[![Wiki](https://img.shields.io/badge/docs-wiki-8b5cf6?style=flat-square&logo=wikipedia&logoColor=white)](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki)

<br />

![Engine](https://img.shields.io/badge/ENGINE-TFS%201.8-7c3aed?style=for-the-badge)
![Protocol](https://img.shields.io/badge/PROTOCOL-8.60-f97316?style=for-the-badge)
![C++](https://img.shields.io/badge/C++-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Lua](https://img.shields.io/badge/Lua-5.5-2C2D72?style=for-the-badge&logo=lua&logoColor=white)
![MariaDB](https://img.shields.io/badge/MariaDB-003545?style=for-the-badge&logo=mariadb&logoColor=white)
![Ubuntu](https://img.shields.io/badge/Ubuntu-22.04%20%7C%2024.04%20%7C%2026.04-E95420?style=for-the-badge&logo=ubuntu&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-vcpkg-0078D4?style=for-the-badge&logo=windows&logoColor=white)

<br />
<br />

**TFS 1.8 Downgrade** brings the classic Tibia **8.60** protocol to a modern, optimized server engine, with native ClientID support and a large set of custom systems.

Developed and maintained by [Mateuzkl](https://github.com/Mateuzkl), based on [Nekiro's TFS 1.5 Downgrades](https://github.com/nekiro/TFS-1.5-Downgrades) and forked from [MillhioreBT's downgrade](https://github.com/MillhioreBT/forgottenserver-downgrade).

[Discord Community](https://discord.gg/9ne5TZapd) · [Full Wiki](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/wiki) · [Issues](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/issues)

</div>

---

## Highlights

| Area | Features |
|---|---|
| Core | TFS 1.8 engine, protocol 8.60, C++23, Lua 5.5, MariaDB, optimized decay |
| Maps | ClientID-native maps and items |
| Clients | OTCv8, Mehah, CipSoft, and custom client options |
| Tooling | Automatic Ubuntu/WSL build and Windows support through vcpkg |

---

## Map Editor

Use the ClientID-compatible map editor for this source:

**[Download NexaMap Editor](https://github.com/Mateuzkl/NexaMap-Editor)**

---

## Optional Systems

Optional systems are listed in `config.lua.dist` and enabled in `config.lua`. Most remain disabled by default to preserve the classic 8.60 experience.

<details>
<summary><strong>View optional systems and config keys</strong></summary>

| System | Config key |
|---|---|
| Forge | `forgeSystemEnabled` |
| Imbuements | `imbuementSystemEnabled` |
| Monk Vocation | `monkVocationEnabled` |
| Familiars | `familiarSystemEnabled` |
| Wheel of Destiny | `wheelSystemEnabled` |
| Bestiary | `bestiarySystemEnabled` |
| Market | `marketSystemEnabled` |
| Prey | `preySystemEnabled` |
| Battle Pass | `battlePassSystemEnabled` |
| Weapon Proficiency | `weaponProficiencySystemEnabled` |
| Augments | `augmentSystemEnabled` |
| Monster Levels | `monsterLevelEnabled` |
| Monster Factions | `monsterFactionSystem` |
| Character Bazaar | `characterBazaarEnabled` |
| Task Hunting / Task Board | `taskHuntingSystemEnabled` |
| Bounty Tasks | `bountyTasksEnabled` |
| Weekly Tasks | `weeklyTasksEnabled` |
| Soulpit | `soulpitSystemEnabled` |
| Soulseals | `soulsealsSystemEnabled` |

Set the desired key to `true` in `config.lua`. Bounty and Weekly Tasks require Task Hunting; Soulseals require Weekly Tasks or another configured source.

</details>

---

## Compilation

### Linux / WSL

On Ubuntu 22.04, 24.04, or 26.04, run:

```bash
chmod +x build.sh
./build.sh
```

The script detects the Ubuntu version, installs all required dependencies automatically, and builds the server in Release mode.

After the build finishes:

```bash
./tfs
```

### Windows

Use the latest version of [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

| Visual Studio | Platform toolset |
|---|---|
| Visual Studio 2026 | `v145` (project default) |
| Visual Studio 2022 | `v143` (retarget the project before building) |

Open `vc18/theforgottenserver.sln`, select `Release` and `x64`, then build the solution. Dependencies declared in `vcpkg.json` are downloaded automatically on the first build.

---

## Client Configuration

Read the full client setup guide before changing feature flags:

- English: [`docs/client-configuration.md`](docs/client-configuration.md)
- PT-BR: [`docs/client-configuration.pt-BR.md`](docs/client-configuration.pt-BR.md)

## Available Clients

Choose the client that matches the experience and feature set you want. They all target protocol 8.60, but use different engines and visual styles.

### OTClient-based

| Client | Description |
|---|---|
| [Astra Client 8.6](https://github.com/Mateuzkl/AstraClient) | OTCv8 client based on OTAcademy with a layout inspired by the CIP 15.x appearance |
| [Otcv8--Classic-8.6](https://github.com/Mateuzkl/Otcv8--Classic-8.6) | OTCv8 Classic using the original CIP 8.60 appearance |
| [OTC-Fonticak](https://github.com/soyfabi/OTC-Fonticak) | Alternative client based on a Mehah fork |

### CipSoft and custom clients

| Client | Description |
|---|---|
| [Client 8.60 + DLL Mount](https://github.com/Mateuzkl/Client-cip-8.60-with-DLL-Mount) | Classic CipSoft 8.60 client with mount support through a modified DLL |
| [Forgotten Client (Alpha 1.0)](https://github.com/Mateuzkl/The-Forgotten-Client) | Future custom client currently under development |

> CIP 15.x in the Astra description refers only to its visual layout. The server and every client listed here continue to use protocol 8.60.

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
