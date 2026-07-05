# Configuracao do client

Este server usa protocolo Tibia 8.60, mas os assets do client podem usar o conjunto de aparencia 15.24.

Neste projeto, "Client Update: Version 15.24" significa que os arquivos `.dat`, `.spr` e `items.otb` foram atualizados para aparencias/conteudo 15.24. Isso nao muda o protocolo de login para 15.24. O protocolo permitido continua sendo 8.60:

```cpp
CLIENT_VERSION_MIN = 860
CLIENT_VERSION_MAX = 860
CLIENT_VERSION_STR = "860"
```

## Assets

Use `.dat`, `.spr` e `items.otb` compatíveis entre si. Se os assets do client e o `items.otb` do server nao baterem, podem aparecer IDs errados, sprites faltando, look quebrado ou problema ao carregar mapa.

Targets suportados:

- OTCv8 / clients estilo Mehah com suporte a features estendidas.
- AstraClient.
- Client CIP 8.60 classico com as DLLs modificadas do projeto.

## Onde configurar features no client

Caminhos comuns em OTCv8/Mehah:

```text
modules/game_features/features.lua
modules/game_features/game_features.lua
```

Cada fork usa um nome. Edite o arquivo que contem `updateFeatures(version)` e o bloco `if(version >= 860) then`.

Os ids precisam bater entre:

```text
client: modules/gamelib/const.lua
client: src/client/const.h
server: src/const.h
```

## Handshake de features do server

O server envia overrides de features para OTCv8/Mehah/Astra em `ProtocolGame::sendFeatures()`, no arquivo `src/protocolgame.cpp`.

Clients que suportam o pacote `0x43` (`GameServerFeatures`) devem deixar o server controlar as flags que mudam tamanho/formato de pacote.

O server atualmente envia estas flags comuns para OTCv8/Astra:

```cpp
ExtendedOpcode = true
SkillsBase = true
PlayerMounts = true
MagicEffectU16 = true
OfflineTrainingTime = true
DoubleSkills = true
BaseSkillU16 = true
AdditionalSkills = true
ExtendedClientPing = true
CreatureIcons = true
ContainerPagination = true
BrowseField = true
QuickLootFlags = shouldSendQuickLootFlags()
ThingUpgradeClassification = false
ItemTierByte = shouldSendItemTierByte()
```

Para detecção Mehah-only, o server envia:

```cpp
ContainerPagination = true
BrowseField = true
ThingUpgradeClassification = shouldSendThingUpgradeClassification()
```

Para AstraClient, o server tambem pode enviar flags exclusivas:

```cpp
ExperienceBonus = true
PlayerFamiliars = true
AstraCreatureIcons = true
AstraQuiverCountU16 = true
AstraOutfitStoreMode = true
DisplayItemDuration = true
DisplayItemCharges = true
PackedPlayerInventory = true
AstraItemMetadata = true
```

Nao copie flags `Astra*` para OTCv8 Classic. Elas precisam do parser do Astra.

## Bloco recomendado OTCv8 / Mehah 8.60

Use este bloco como base para forks OTCv8/Mehah que precisam de perfil 8.60:

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
    g_game.enableFeature(GameIdleAnimations)
    g_game.enableFeature(GameEnhancedAnimations)
    g_game.enableFeature(GameExtendedClientPing)
    g_game.enableFeature(GameSpritesU32)
    g_game.enableFeature(GameDoublePlayerGoodsMoney)
    g_game.enableFeature(GameCreatureIcons)
    g_game.enableFeature(GamePurseSlot)
    g_game.enableFeature(GamePrey)
    g_game.enableFeature(GameSpellList)
end
```

Alguns forks tambem definem:

```lua
GameLeechAmount
```

So habilite `GameLeechAmount` se o seu client tiver essa feature definida no source e se o parser esperar isso. O enum de features deste server nao envia `GameLeechAmount` atualmente.

## Flags que mudam pacote

Estas flags podem quebrar o protocolo se client e server discordarem:

```lua
GameQuickLootFlags              -- id 123
GameThingUpgradeClassification  -- id 130
GameItemTierByte                -- id 131
```

Comportamento recomendado:

```lua
-- Deixe estas flags controladas pelo handshake do server quando possivel.
-- Nao troque cegamente para enableFeature.
g_game.disableFeature(GameQuickLootFlags)
g_game.disableFeature(GameThingUpgradeClassification)
g_game.disableFeature(GameItemTierByte)
```

Depois deixe o pacote `0x43` ligar/desligar os valores finais apos o login.

### GameQuickLootFlags

Condição no server:

```cpp
QuickLootFlags = shouldSendQuickLootFlags()
```

`shouldSendQuickLootFlags()` e verdadeiro apenas para AstraClient quando quick loot esta habilitado na config.

### GameThingUpgradeClassification

Condição no server:

```cpp
ThingUpgradeClassification = false // caminho OTCv8/Astra
ThingUpgradeClassification = shouldSendThingUpgradeClassification() // caminho Mehah
```

Para Mehah, depende de:

```lua
enableItemTierDisplay = true
enableItemUpgradeClassification = true
```

### GameItemTierByte

Condição no server:

```cpp
ItemTierByte = shouldSendItemTierByte()
```

Depende de:

```lua
enableItemTierDisplay = true
```

e do modo server-side de item tier byte.

## Notas para AstraClient

O AstraClient tem perfil proprio de features 8.60 e extensoes proprias de pacote. Nao trate Astra como copia direta do OTCv8 Classic.

Features exclusivas do Astra:

```lua
GameAstraCreatureIcons
GameAstraQuiverCountU16
GameAstraOutfitStoreMode
GameAstraItemMetadata
```

Essas flags so devem ser usadas quando o server reconhece AstraClient e a config relacionada esta ativa.

## Client CIP classico

O client CIP classico nao usa `g_game.enableFeature`.

Ele precisa de assets 8.60 compativeis e das DLLs modificadas do projeto para limites estendidos:

| Patch DLL | Para que serve |
|---|---|
| `__MAGIC_EFFECTS_U16__` | Magic effects acima de 255 |
| `__DISTANCE_SHOOT_U16__` | Distance effects acima de 255 |
| `__PLAYER_HEALTH_U32__` | Vida do player acima de 65535 |
| `__PLAYER_MANA_U32__` | Mana do player acima de 65535 |
| Outfit Limit Changer | Outfits acima do limite antigo do 8.60 |

Store inbox no CIP classico deve ser acessado por comandos como `!storeinbox`, `!sinbox` ou `!inbox`.

## Checklist final

- [ ] O client continua conectando como protocolo 860.
- [ ] `.dat`, `.spr` e `items.otb` do server sao do mesmo conjunto de assets.
- [ ] OTCv8/Mehah tem as features base 8.60 ligadas.
- [ ] `GameSpritesU32` bate com o formato do arquivo de sprites.
- [ ] `GameQuickLootFlags`, `GameThingUpgradeClassification` e `GameItemTierByte` batem com `sendFeatures()`.
- [ ] Flags exclusivas do Astra so sao usadas pelo AstraClient.
- [ ] CIP classico usa DLL patch em vez de feature flag OTC.
- [ ] Login, andar, look, use, abrir backpack, abrir corpse, store inbox e logout foram testados.
