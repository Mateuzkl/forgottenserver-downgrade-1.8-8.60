## Rarity System — Documentacao

Sistema de raridade de itens para TFS 1.8 (protocolo 8.60). Engine em C++23 (smart pointers, `std::optional`, `std::span`), balance inteiramente configuravel em Lua via `data/scripts/systems/rarity/`.

---

### Arquitetura

```
┌─ C++23 (engine) ─────────────────────────────────────────────────────────┐
│                                                                          │
│  configmanager.h/cpp   toggle RARITY_SYSTEM_ENABLED (server_config.lua) │
│  item.h/cpp            namespace Rarity (constantes) + Item methods     │
│                          getRarityTier() → std::optional<int32_t>        │
│                          getRarityStat() → std::optional<int64_t>        │
│                          makeStatKey() com reserve() p/ perf             │
│  combat.cpp            getRaritySpellDamage() (implementado)            │
│                          processRarityOnAttack/OnHit/DoubleDmg/EleDmg   │
│                          ObserverPtr<T> + shared_ptr capture            │
│                          std::span<const slots_t> nos loops             │
│                          std::max com std::pair p/ onHit best item      │
│                          escala 100 (consistente com config %)          │
│  game.cpp              processRarityOnKill()                            │
│                          ObserverPtr<T> + shared_ptr capture            │
│                          callback Lua por stat individual               │
│  events.h/cpp          eventRarityOnAttack/Hit/Double/Ele/KillProc     │
│                          integrado ao sistema Events (events.xml)       │
│  luaitem.cpp           bindings: hasRarity, [gs]etRarity[Tier|Stat]    │
│  observer_ptr.h        using ObserverPtr<T> = T* (non-owning alias)     │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘

┌─ Lua (data/scripts/systems/rarity/) ─────────────────────────────────────┐
│                                                                          │
│  init.lua         entry point, carrega modulos se toggle ON              │
│  config.lua       tiers, 45 atributos, elegibilidade, monster tiers     │
│  balancing.lua    dano spells, duracao buffs — consumido pelo C++       │
│  core.lua         rollRarity(), itemAttributes(), processMonsterLoot()  │
│  combat.lua       creaturescripts healthChange / manaChange             │
│  events.lua       onDropLoot, onInventoryUpdate, onLogin                │
│  callbacks.lua    exemplo de registro de eventcallbacks                  │
│  helpers.lua      utilitarios                                           │
│                                                                          │
│  data/events/scripts/rarity.lua — dispatcher intermediario TFS         │
│  data/scripts/lib/event_callbacks.lua — registros ec.onAttackProc etc  │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
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

### C++23 — Gerenciamento de Memoria

| Feature | Onde | Por que |
|---------|------|---------|
| `ObserverPtr<T>` | Todos os parametros de funcao | Documenta non-owning semantics |
| `shared_ptr<Item>` | `getWeaponShared()`, `getInventoryItemShared()` | Lifetime garantido durante o proc |
| `std::optional<int32_t>` | `getRarityTier()` | `nullopt` em vez de sentinela `0` |
| `std::optional<int64_t>` | `getRarityStat()` | `nullopt` em vez de sentinela `0` |
| `std::span<const slots_t>` | Loops de slot | Type-safe range iteration |
| `std::max` + `std::pair` | `processRarityOnHitStat` | Algoritmo STL em vez de busca manual |
| `[[nodiscard]]` | `hasRarity()`, `getRarityTier()`, `getRarityStat()` | Compilador avisa se retorno ignorado |

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

#### On Attack Spells (C++ hook, escala 100 = %)

| Atributo | statKey | Formula de dano |
|----------|---------|-----------------|
| Cast Fire Strike on Attack | `onAttackFireStrike` | 20-45 + scale |
| Cast Ice Strike on Attack | `onAttackIceStrike` | 15-35 + scale |
| Cast Terra Strike on Attack | `onAttackTerraStrike` | 15-35 + scale |
| Cast Death Strike on Attack | `onAttackDeathStrike` | 15-40 + scale |
| Cast Energy Strike on Attack | `onAttackEnergyStrike` | 20-50 + scale |
| Cast Divine Missile on Attack | `onAttackDivineMissile` | 20-40 + scale |

#### On Hit Spells (C++ hook, escala 100 = %)

| Atributo | statKey | Formula de dano |
|----------|---------|-----------------|
| Cast Fire Strike on Hit | `onHitFireStrike` | 20-45 + scale |
| Cast Ice Strike on Hit | `onHitIceStrike` | 15-35 + scale |
| Cast Terra Strike on Hit | `onHitTerraStrike` | 15-35 + scale |
| Cast Death Strike on Hit | `onHitDeathStrike` | 15-40 + scale |
| Cast Energy Strike on Hit | `onHitEnergyStrike` | 20-50 + scale |
| Cast Divine Missile on Hit | `onHitDivineMissile` | 20-40 + scale |

**Formula de scale** (implementada em `combat.cpp:getRaritySpellDamage`):
```
baseDmg = random(dmgMin, dmgMax)
scaled  = (playerLevel * scaleLevel + magicLevel * scaleMagic) / divisor
total   = baseDmg + scaled
```
Valores padrao em `balancing.lua` > `rarityBalancing.spellScale`: `level=2, magic=3, divisor=5`.
Para level 100, magic 50: `(200 + 150) / 5 = 70` de bonus + base.

#### Damage Modifiers (C++ hooks, escala 100 = %)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Double Damage | `doubleDamage` | chance de dobrar dano (callback: `onDoubleDamage`) |
| Physical Damage % | `physicalDamage` | bonus de dano fisico |
| Fire Damage % | `fireDamage` | bonus de dano fire |
| Ice Damage % | `iceDamage` | bonus de dano ice |
| Energy Damage % | `energyDamage` | bonus de dano energy |
| Earth Damage % | `earthDamage` | bonus de dano earth |
| Holy Damage % | `holyDamage` | bonus de dano holy |
| Death Damage % | `deathDamage` | bonus de dano death |
| Elemental Damage % | `elementalDamage` | bonus para todos elementos (callback: `onElementalDamage`) |

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

#### On Kill Effects (C++ hook, escala 100 = %, callbacks: `onKillProc`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Explosion on Kill | `onKillExplosion` | efeito visual de explosao |
| Regen HP on Kill | `onKillRegenHp` | cura flat ao matar |
| Regen MP on Kill | `onKillRegenMp` | recupera mana ao matar |
| Bonus Damage on Kill | `onKillBuffDamage` | buff temporario de crit |
| Bonus Max HP on Kill | `onKillBuffMaxHp` | +% HP max |
| Bonus Max MP on Kill | `onKillBuffMaxMp` | +% MP max |

Valores de buff em `balancing.lua` > `rarityBalancing.onKill` (consumidos pelo C++).

#### Additional Loot (Lua `onDropLoot`)

| Atributo | statKey | Efeito |
|----------|---------|--------|
| Additional Loot % | `additionalLoot` | chance de loot extra |

---

### Event Callbacks (C++ → Lua)

Callbacks disparam **apenas quando o proc ocorre** (~4 chamadas/s sob carga), nao em todo hit.
Seguem o padrao TFS `Events` + `hasEvent` + `Event()`.

#### Callbacks disponiveis

| Callback | Quando dispara | Parametros | Retorno |
|----------|---------------|------------|---------|
| `onAttackProc` | On-attack spell acertou | `player, target, item, statKey, combatType, damage` | `bool` |
| `onHitProc` | On-hit spell acertou | `player, target, item, statKey, combatType, damage` | `bool` |
| `onDoubleDamage` | Double damage ativou | `player` | `bool` |
| `onElementalDamage` | Elemental damage aplicado | `player, item, fireDmg` | `bool` |
| `onKillProc` | On-kill stat processado | `player, target, item, statKey, value` | `bool` |

`true` = aplica comportamento padrao do C++ apos o callback.
`false` = pula comportamento padrao (callback fez tratamento customizado).

#### Exemplo de uso

```lua
-- data/scripts/myserver/rarity_hooks.lua
local cb = Event()

function cb.onAttackProc(player, target, item, statKey, combatType, damage)
    if statKey == "onAttackFireStrike" then
        target:getPosition():sendMagicEffect(CONST_ME_FIREAREA)
        -- Aplica burn condition customizada
        local burn = Condition(CONDITION_FIRE)
        burn:setParameter(CONDITION_PARAM_DELAYED, 5)
        burn:setParameter(CONDITION_PARAM_TICKS, 5000)
        target:addCondition(burn)
    end
    return true
end

function cb.onKillProc(player, target, item, statKey, value)
    if statKey == "onKillRegenHp" then
        player:addHealth(value * 2)  -- dobro da cura
        player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
        return false  -- pula o heal padrao do C++
    end
    return true
end

function cb.onDoubleDamage(player)
    player:say("DOUBLE DAMAGE!", TALKTYPE_MONSTER_SAY)
    return true
end

cb:register()
```

Veja `data/scripts/systems/rarity/callbacks.lua` para o exemplo completo.

#### Fluxo de execucao do callback

```
C++ processRarityOnAttackStat:
  calcula chance → fez o roll → calcula damage
    └─ g_events->eventRarityOnAttackProc(player, target, item, key, type, dmg)
         └─ Lua: Rarity:onAttackProc() (rarity.lua — dispatcher TFS)
              └─ hasEvent.onAttackProc? → Event.onAttackProc(...)
                   └─ Itera callbacks registrados com Event()
                        ├─ callback retorna false → C++ NAO casta spell padrao
                        └─ callback retorna true  → C++ casta spell padrao
```

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

O C++ le esses valores em tempo de combate via `item:getRarityStat(key)` (retorna `std::optional<int64_t>`, Lua recebe `.value_or(0)`). Nenhum valor numerico e hardcoded no C++ — todos veem do Lua (config + balancing) e sao gravados no item durante o `rollRarity()`.

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
                 ├─ Seleciona 1-2 stats aleatorios (sem reposicao)
                 ├─ Grava stats + balancing via setRarityStat()
                 ├─ Grava spell scale + DmgMin/DmgMax (consumido pelo C++)
                 ├─ Grava onKill buff defaults
                 └─ Modifica base stats (attack/defense/armor)

Jogador equipa item
  └─ onInventoryUpdate → itemAttributes(player, item, slot, equip)
       ├─ Le getRarityStat() de cada stat (retorna number em Lua)
       ├─ Cria ConditionAttributes (skills, HP, MP, ML, exp) com SUBID por slot
       └─ Ao desequipar: remove condicoes + re-aplica as restantes

Combate (C++)
  Combat::doTargetCombat
    ├─ processRarityOnAttack()     → le weapon, casta spell on-attack
    │    └─ getRaritySpellDamage() → formula level*scale + magic*scale + base
    │    └─ Lua callback: onAttackProc (se registrado)
    ├─ [block check + forge dodge/crit/fatal]
    ├─ processRarityOnHit()        → le equipped slots, best chance wins
    │    └─ Lua callback: onHitProc (se registrado)
    ├─ processRarityDoubleDamage() → chance somada de todos os slots
    │    └─ Lua callback: onDoubleDamage (se registrado)
    ├─ processRarityElementalDamage() → adiciona fire/elemental ao dano
    │    └─ Lua callback: onElementalDamage (se registrado)
    └─ Game::combatChangeHealth()
         ├─ [Lua healthChange] → resistencias, life leech
         ├─ Aplica dano
         └─ processRarityOnKill()  → itera slots, agrega stats
              └─ Lua callback: onKillProc por stat (se registrado)
                   false → pula efeito padrao daquele stat

Jogador loga
  └─ rarityLogin.onLogin
       ├─ registerEvent("rarityHealthChange")
       ├─ registerEvent("rarityManaChange")
       ├─ Re-aplica condicoes de todos os itens equipados
       └─ Corrige HP/MP se excederem max
```

---

### Comando Admin

`/i [rare|epic|legendary] <itemname|id> [, tier N] [, count]` — cria item com rarity e/ou forge tier opcionais. Se o caster nao tiver container, adiciona uma backpack automaticamente.

`/roll [rare|epic|legendary]` — aplica rarity forcada no item em frente ao jogador.

---

### Como adicionar um novo atributo

1. **`config.lua`** — adiciona entrada em `rarityConfig.attributes`:
```lua
["meuAtributo"] = {
    statKey = "meuAtributo",
    name = "Meu Atributo",
    valueType = "static",        -- ou "percent"
    rare = {1, 5}, epic = {6, 10}, legendary = {11, 20},
    eligible = function(itemType) return itemType:isWeapon() end,
    isPercent = false,           -- true se for %
    onEquip = function(player, slot, value, equip)
        -- criar/remover Condition (para stats de condicao)
    end,
},
```

2. **Stat de condicao** (skills, HP, MP, etc.) — o `onEquip` resolve sozinho.

3. **Efeito de combate novo** (spell, double damage, elemental) — adicionar:
   - Constante em `item.h` no `namespace Rarity`
   - Logica em `combat.cpp` ou `game.cpp`
   - Chance no `config.lua` + valores de balance no `balancing.lua`
   - Gravacao no item em `core.lua` > `rollRarity()`

4. **Protecao/leech** — adicionar em `combat.lua` no creaturescript + statKey em `config.lua`.

5. **Event callback novo** — adicionar em `events.h/cpp` seguindo o padrao existente + entrada em `events.xml` + entry no `event_callbacks.lua`.

---

### Arquivos modificados

#### C++23 (9 arquivos)

| Arquivo | Alteracao |
|---------|-----------|
| `src/configmanager.h` | + `RARITY_SYSTEM_ENABLED` no enum Boolean |
| `src/configmanager.cpp` | + parse `raritySystemEnabled` |
| `src/item.h` | + `namespace Rarity` (constantes + `makeStatKey()`) + Item methods com `[[nodiscard]]` e `std::optional` |
| `src/item.cpp` | + implementacoes (`Rarity::TIER`, `makeStatKey()`, `std::optional` returns) |
| `src/combat.cpp` | + `getRaritySpellDamage()` + 4 rarity procs + 5 callbacks + `ObserverPtr` + `shared_ptr` + `std::span` + `std::max` |
| `src/game.cpp` | + `processRarityOnKill()` + 11 callbacks Lua + `ObserverPtr` + `shared_ptr` |
| `src/events.h` | + 5 campos + 5 metodos de rarity callback |
| `src/events.cpp` | + loading XML + 5 implementacoes de invoke (~110 linhas) |
| `src/player.h` | + `getWeaponShared()`, `getInventoryItemShared()` |
| `src/player.cpp` | + implementacoes retornando `shared_ptr<Item>` |
| `src/luaitem.cpp` | + 6 metodos Lua + check config em `setRarityStat` |
| `src/luascript.h` | + 6 declaracoes de metodos |
| `src/luascript.cpp` | + `RARITY_SYSTEM_ENABLED` global + configKeys |

#### Lua (11 arquivos)

| Arquivo | Descricao |
|---------|-----------|
| `data/server_config.lua` | `raritySystemEnabled = true` |
| `data/startup/others/functions.lua` | `dofile('data/scripts/systems/rarity/init.lua')` |
| `data/scripts/systems/rarity/init.lua` | entry point, carrega 7 modulos |
| `data/scripts/systems/rarity/config.lua` | tiers, 45 atributos, monster tiers |
| `data/scripts/systems/rarity/balancing.lua` | spell damage, onKill — consumido pelo C++ |
| `data/scripts/systems/rarity/core.lua` | rollRarity, itemAttributes, processMonsterLoot |
| `data/scripts/systems/rarity/combat.lua` | healthChange (protecoes + life leech) |
| `data/scripts/systems/rarity/events.lua` | onDropLoot, onInventoryUpdate, onLogin + docs callbacks |
| `data/scripts/systems/rarity/callbacks.lua` | exemplo de registro de eventcallbacks |
| `data/scripts/systems/rarity/helpers.lua` | `rarityDebug()` |
| `data/events/scripts/rarity.lua` | dispatcher intermediario TFS (onAttackProc etc.) |
| `data/scripts/lib/event_callbacks.lua` | +5 registros `ec.onAttackProc` etc. |
| `data/events/events.xml` | +5 entradas `<event class="Rarity" ... />` |
