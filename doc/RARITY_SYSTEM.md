## Rarity System — Documentacao

Sistema de raridade de itens para TFS 1.8 (protocolo 8.60). Engine em C++23, balance inteiramente configuravel em Lua via `data/scripts/systems/rarity/`.

---

### Arquitetura

```
┌─ C++ (engine) ──────────────────────────────────────────────────────────┐
│                                                                         │
│  configmanager.h/cpp    toggle RARITY_SYSTEM_ENABLED (server_config.lua) │
│  item.h/cpp             namespace Rarity (constantes) + Item methods    │
│  combat.cpp             hooks: OnAttack, OnHit, DoubleDamage, EleDmg   │
│  game.cpp               hook:  OnKill (explosion, regen, buffs)         │
│  luascript.h/cpp        configKeys.RARITY_SYSTEM_ENABLED                │
│  luaitem.cpp            bindings: hasRarity, [gs]etRarity[Tier|Stat]  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

┌─ Lua (data/scripts/systems/rarity/) ────────────────────────────────────┐
│                                                                         │
│  init.lua         entry point, carrega modulos se toggle ON             │
│  config.lua       tiers, atributos, elegibilidade, monster tiers        │
│  balancing.lua    valores de balance (dano spells, duracao buffs, %)    │
│  core.lua         rollRarity(), itemAttributes(), processMonsterLoot()  │
│  combat.lua       creaturescripts healthChange / manaChange            │
│  events.lua       onDropLoot, onInventoryUpdate, onLogin               │
│  helpers.lua      utilitarios                                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

### Enable / Disable

```lua
-- data/server_config.lua
raritySystemEnabled = true   -- ativa todo o sistema
```

Carregado via `data/startup/others/functions.lua`:
```lua
dofile('data/scripts/systems/rarity/init.lua')
```

---

### Tiers de Raridade

| Tier | Nome | Chance natural | 2o stat | Cor popup |
|------|------|---------------|---------|-----------|
| 1 | Rare | 7.5% (750/10000) | 20% | verde |
| 2 | Epic | 3.75% (375/10000) | 50% | azul |
| 3 | Legendary | 2% (200/10000) | 100% | laranja |

Configuravel em `config.lua` > `rarityConfig.tiers`.

---

### Monster Tiers (Loot)

Classificacao manual por nome do monstro:

```lua
rarityConfig.monsterTiers = {
    boss = {
        chance = 50,        -- 50% de chance do loot vir com rarity
        minTier = 2,        -- minimo epic
        monsters = {"orshabaal", "morgaroth", "ferumbras", ...},
    },
    miniboss = {
        chance = 20,
        minTier = 1,
        monsters = {"black knight", "elder wyrm", ...},
    },
}
rarityConfig.defaultMonsterChance = 5   -- monstros nao listados
```

---

### Atributos — Elegibilidade por propriedade (sem hardcode de IDs)

Cada atributo define uma funcao `eligible(itemType)`:

```lua
["maxHp"] = {
    statKey = "maxHp",
    name = "Max HP",
    rare = {30, 60}, epic = {80, 120}, legendary = {150, 250},
    eligible = function(itemType)
        return itemType:getArmor() > 0 or itemType:isWeapon()
    end,
}
```

Qualquer custom item adicionado ao `items.xml` funciona automaticamente.

---

### Lista completa de atributos

#### Stat Bonuses (aplicados no equip via ConditionAttributes)

| Atributo | statKey | Tipo | Elegibilidade |
|----------|---------|------|---------------|
| Max HP | `maxHp` | flat | armor > 0 ou isWeapon |
| Max MP | `maxMp` | flat | armor > 0 ou isWeapon ou shield |
| Max HP % | `maxHpPercent` | % | armor > 0 |
| Max MP % | `maxMpPercent` | % | armor > 0 |
| Magic Level | `magicLevel` | flat | armor > 0 ou wand |
| Sword Skill | `swordSkill` | flat | WEAPON_SWORD |
| Axe Skill | `axeSkill` | flat | WEAPON_AXE |
| Club Skill | `clubSkill` | flat | WEAPON_CLUB |
| Fist Skill | `fistSkill` | flat | armor > 0 |
| Distance Skill | `distanceSkill` | flat | WEAPON_DISTANCE ou armor > 0 |
| Shielding | `shielding` | flat | WEAPON_SHIELD ou armor > 0 |
| Melee Skills | `meleeSkills` | flat | armor > 0 |
| Experience | `experience` | % | armor > 0 ou isWeapon |

#### Base Stats (modifica o item diretamente)

| Atributo | statKey | Como funciona |
|----------|---------|---------------|
| Attack | `attack` | `item:setAttribute(ATTACK, base + value)` |
| Defense | `defense` | `item:setAttribute(DEFENSE, base + value)` |
| Armor | `armor` | `item:setAttribute(ARMOR, base + value)` |

#### On Attack Spells (C++ hook em `combat.cpp:doTargetCombat`, antes do block check)

| Atributo | statKey | Dano |
|----------|---------|------|
| Cast Fire Strike on Attack | `onAttackFireStrike` | 20-45 + scale |
| Cast Ice Strike on Attack | `onAttackIceStrike` | 15-35 + scale |
| Cast Terra Strike on Attack | `onAttackTerraStrike` | 15-35 + scale |
| Cast Death Strike on Attack | `onAttackDeathStrike` | 15-40 + scale |
| Cast Energy Strike on Attack | `onAttackEnergyStrike` | 20-50 + scale |
| Cast Divine Missile on Attack | `onAttackDivineMissile` | 20-40 + scale |

#### On Hit Spells (C++ hook em `combat.cpp:doTargetCombat`, apos block check)

| Atributo | statKey | Dano |
|----------|---------|------|
| Cast Fire Strike on Hit | `onHitFireStrike` | 20-45 + scale |
| Cast Ice Strike on Hit | `onHitIceStrike` | 15-35 + scale |
| Cast Terra Strike on Hit | `onHitTerraStrike` | 15-35 + scale |
| Cast Death Strike on Hit | `onHitDeathStrike` | 15-40 + scale |
| Cast Energy Strike on Hit | `onHitEnergyStrike` | 20-50 + scale |
| Cast Divine Missile on Hit | `onHitDivineMissile` | 20-40 + scale |

Formula de scale: `(level * spellScale.level + magicLevel * spellScale.magic) / spellScale.divisor`

Configuravel em `balancing.lua` > `rarityBalancing.spellScale`.

#### Damage Modifiers (C++ hooks em `combat.cpp`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Double Damage | `doubleDamage` | chance de dobrar o dano |
| Physical Damage % | `physicalDamage` | bonus de dano fisico |
| Fire Damage % | `fireDamage` | bonus de dano fire |
| Ice Damage % | `iceDamage` | bonus de dano ice |
| Energy Damage % | `energyDamage` | bonus de dano energy |
| Earth Damage % | `earthDamage` | bonus de dano earth |
| Holy Damage % | `holyDamage` | bonus de dano holy |
| Death Damage % | `deathDamage` | bonus de dano death |
| Elemental Damage % | `elementalDamage` | bonus para todos elementos |

#### Protections (Lua creaturescript `healthChange`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Physical Protection % | `physicalProtection` | reduz dano fisico |
| Fire Protection % | `fireProtection` | reduz dano fire |
| Ice Protection % | `iceProtection` | reduz dano ice |
| Energy Protection % | `energyProtection` | reduz dano energy |
| Earth Protection % | `earthProtection` | reduz dano earth |
| Holy Protection % | `holyProtection` | reduz dano holy |
| Death Protection % | `deathProtection` | reduz dano death |
| Elemental Protection % | `elementalProtection` | reduz todos elementos |

#### Life Leech (Lua creaturescript `healthChange`, lado atacante)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Life Leech % | `lifeLeech` | % do dano convertido em heal |

#### On Kill Effects (C++ hook em `game.cpp:combatChangeHealth`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Explosion on Kill | `onKillExplosion` | efeito visual de explosao |
| Regen HP on Kill | `onKillRegenHp` | cura flat ao matar |
| Regen MP on Kill | `onKillRegenMp` | recupera mana ao matar |
| Bonus Damage on Kill | `onKillBuffDamage` | buff temporario de crit (10% chance, +50% dmg) |
| Bonus Max HP on Kill | `onKillBuffMaxHp` | buff temporario de +5% HP max |
| Bonus Max MP on Kill | `onKillBuffMaxMp` | buff temporario de +5% MP max |

Valores de buff (duracao, %) configuraveis em `balancing.lua` > `rarityBalancing.onKill`.

#### Additional Loot (Lua `onDropLoot`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Additional Loot % | `additionalLoot` | chance de loot extra |

---

### Storage (KV via CustomAttribute)

Toda informacao de rarity e armazenada no item como custom attributes com prefixo `rarity.`:

```
rarity.tier                              = 2              (epic)
rarity.stat.maxHp                        = 80             (+80 HP)
rarity.stat.onAttackFireStrike           = 10             (10% chance)
rarity.stat.onAttackFireStrikeDmgMin     = 20             (dano min)
rarity.stat.onAttackFireStrikeDmgMax     = 45             (dano max)
rarity.stat.spellScaleLevel             = 2              (formula)
rarity.stat.spellScaleMagic             = 3
rarity.stat.spellScaleDivisor           = 5
rarity.stat.onKillBuffDuration          = 30000          (30s)
rarity.stat.onKillBuffCritChance        = 1000           (10%)
...
```

O C++ le esses valores em tempo de combate via `item:getRarityStat(key)`. Nenhum valor numerico e hardcoded no C++ — todos veem do Lua (config + balancing) e sao gravados no item durante o `rollRarity()`.

Serializacao automatica pelo TFS 1.8 — zero mudancas no schema do banco.

---

### Fluxo completo

```
Monstro morre
  └─ onDropLoot → processMonsterLoot(monster, corpse)
       ├─ Verifica tier do monstro (config.lua monsterTiers)
       ├─ Rolla chance de rarity
       └─ Para cada item no corpse:
            └─ rollRarity(item, nil, minTier)
                 ├─ Checa elegibilidade (filtra atributos)
                 ├─ Sorteia tier (rare/epic/legendary)
                 ├─ Seleciona 1-2 stats aleatorios
                 ├─ Grava stats + balancing via setRarityStat()
                 └─ Modifica base stats (attack/defense/armor)

Jogador equipa item
  └─ onInventoryUpdate → itemAttributes(player, item, slot, equip)
       ├─ Le getRarityStat() de cada stat
       ├─ Cria ConditionAttributes (skills, HP, MP, ML, exp)
       └─ Aplica/remove condicoes

Combate (C++)
  Combat::doTargetCombat
    ├─ processRarityOnAttack()     → le weapon, casta spell on-attack
    ├─ [block check]
    ├─ [forge dodge, critical, fatal]
    ├─ processRarityOnHit()        → le equipped, casta spell on-hit
    ├─ processRarityDoubleDamage() → chance de 2x dano
    ├─ processRarityElementalDamage() → adiciona elemental ao dano
    └─ Game::combatChangeHealth()
         ├─ [Lua healthChange] → resistencias, life leech
         ├─ Aplica dano
         └─ processRarityOnKill()  → explosion, regen, buffs

Jogador loga
  └─ rarityLogin.onLogin
       ├─ registerEvent("rarityHealthChange")
       ├─ registerEvent("rarityManaChange")
       ├─ Re-aplica condicoes de todos os itens equipados
       └─ Corrige HP/MP se excederem max
```

---

### Comando Admin

`/roll` — aplica rarity forçada no item em frente ao jogador.
`/roll rare|epic|legendary` — força o tier especifico.

---

### Como adicionar um novo atributo

1. **`config.lua`** — adiciona entrada em `rarityConfig.attributes`:
```lua
["meuAtributo"] = {
    statKey = "meuAtributo",
    name = "Meu Atributo",
    valueType = "static",
    rare = {1, 5}, epic = {6, 10}, legendary = {11, 20},
    eligible = function(itemType) return itemType:isWeapon() end,
    -- opcional para stats que precisam de condicao:
    onEquip = function(player, slot, value, equip)
        -- criar/remover Condition
    end,
},
```

2. Se for **stat de condicao** (skills, HP, MP, etc.) — o `onEquip` ja resolve.

3. Se for **efeito de combate** (spell, double damage, elemental) — adicionar:
   - Constante em `item.h` no `namespace Rarity`
   - Logica em `combat.cpp` (se for on-attack/hit/damage) ou `game.cpp` (se for on-kill)
   - Chance no `config.lua` + valores de balance no `balancing.lua`
   - Gravacao do valor no item em `core.lua` > `rollRarity()`

4. Se for **protecao/leech** — adicionar em `combat.lua` no creaturescript.

---

### Arquivos modificados

#### C++ (8 arquivos)

| Arquivo | Alteracao |
|---------|-----------|
| `src/configmanager.h` | + `RARITY_SYSTEM_ENABLED` no enum Boolean |
| `src/configmanager.cpp` | + parse `raritySystemEnabled` |
| `src/item.h` | + `namespace Rarity` (constantes de keys) + Item methods (hasRarity, get/setRarityTier/Stat, clearRarityStats) + move CustomAttributeMap e alias para public |
| `src/item.cpp` | + implementacoes dos metodos de rarity |
| `src/combat.cpp` | + anonymous namespace: processRarityOnAttack/Hit/DoubleDamage/ElementalDamage + hooks em doTargetCombat |
| `src/game.cpp` | + anonymous namespace: processRarityOnKill + hook em combatChangeHealth + `#include "item.h"` |
| `src/luaitem.cpp` | + 6 metodos Lua: hasRarity, [gs]etRarityTier, [gs]etRarityStat, clearRarityStats |
| `src/luascript.h` | + 6 declaracoes de metodos |
| `src/luascript.cpp` | + `RARITY_SYSTEM_ENABLED` global + configKeys |

#### Lua (8 arquivos)

| Arquivo | Descricao |
|---------|-----------|
| `data/server_config.lua` | + `raritySystemEnabled = true` |
| `data/startup/others/functions.lua` | + `dofile('data/scripts/systems/rarity/init.lua')` |
| `data/scripts/systems/rarity/init.lua` | entry point |
| `data/scripts/systems/rarity/config.lua` | tiers, atributos, monster tiers |
| `data/scripts/systems/rarity/balancing.lua` | dano spells, duracao buffs, % |
| `data/scripts/systems/rarity/core.lua` | rollRarity, itemAttributes, processMonsterLoot |
| `data/scripts/systems/rarity/combat.lua` | healthChange / manaChange |
| `data/scripts/systems/rarity/events.lua` | onDropLoot, onInventoryUpdate, onLogin |
| `data/scripts/systems/rarity/helpers.lua` | utilitarios |
