# Sistema de Augments

Sistema de bônus de spell vinculados a **tipos de item** via `items.xml`. Ao equipar um item com augment e castar a spell associada, o bônus é aplicado automaticamente no dano, healing, critical, leech ou cooldown.

---

## Características

- **Sem alterações de banco** — augments são dados de `ItemType` (template), lidos do XML no startup
- **Por tipo de item** — todos os itens do mesmo ID compartilham os mesmos augments
- **Spell-agnóstico** — funciona com qualquer spell (Lua ou C++), o match é feito pelo nome registrado da spell
- **Acumulativo** — múltiplos itens equipados com augments para a mesma spell somam os efeitos
- **12 tipos de augment**

---

## Tipos de Augment

### Fixos (valor flat)

| XML `value` | Enum | Efeito | Escala |
|---|---|---|---|
| `"base"` | `Base` | Aumenta dano/healing base em % | basis points: `400` = `+4.00%` |
| `"powerful impact"` | `PowerfulImpact` | Impacto poderoso em % | basis points (usa config default se omitido) |
| `"strong impact"` | `StrongImpact` | Impacto forte em % | basis points (usa config default se omitido) |
| `"increased damage"` | `IncreasedDamage` | Dano aumentado em % | basis points (usa config default se omitido) |
| `"cooldown"` | `Cooldown` | Reduz cooldown da spell | milissegundos: `120000` = `-120s` |
| `"critical extra damage"` | `CriticalExtraDamage` | Dano crítico extra | valor direto |
| `"critical hit chance"` | `CriticalHitChance` | Chance de crítico extra | valor direto |
| `"life leech"` | `LifeLeech` | Life leech adicional | valor direto |
| `"mana leech"` | `ManaLeech` | Mana leech adicional | valor direto |

### Dinâmicos (escalam com skills/ML do jogador)

| XML `value` | Enum | Efeito | Fórmula |
|---|---|---|---|
| `"magic level healing"` | `MagicLevelHealing` | +X% do magic level como healing extra (só quando `damage > 0`) | `healing += (value / 100.0) * magicLevel` |
| `"magic level damage"` | `MagicLevelDamage` | +X% do magic level como dano extra (só quando `damage < 0`) | `dano += (value / 100.0) * magicLevel` |
| `"skill damage"` | `SkillDamage` | +X% da weapon skill como dano extra | auto-detecta a skill (ver abaixo) |

### SkillDamage — Auto-Detecção de Skill

O tipo `skill damage` detecta automaticamente qual skill usar baseado na arma equipada:

| Arma equipada | Skill usada |
|---|---|
| Sword | `SKILL_SWORD` |
| Axe | `SKILL_AXE` |
| Club | `SKILL_CLUB` |
| Distance (bow/xbow) | `SKILL_DISTANCE` |
| Wand / Rod | `magicLevel` (mages não têm weapon skill) |
| Fist (monk) / outros | `SKILL_FIST` |

Fórmula: `dano += (value / 100.0) * skillLevel`

---

## Arquitetura

### Fluxo Completo

```
Player casta spell ("exevo vis lux")
  │
  └─ InstantSpell::playerCastInstant
       └─ executeCastSpell(player, var)
            │
            ├─ [1] player->setSpellNameCasting("exevo vis lux")
            │      └─ armazena o nome ANTES da execução Lua
            │
            ├─ [2] Lua: spell.onCastSpell(creature, variant)
            │      └─ combat:execute(creature, variant)
            │           └─ Combat::doCombat(creature, target/position)
            │                │
            │                ├─ getCombatDamage(caster, target)
            │                │
            │                ├─ [3] player->applyItemAugments(damage)
            │                │      ├─ Itera CONST_SLOT_FIRST..CONST_SLOT_LAST
            │                │      ├─ Filtra itens com getAugments()
            │                │      ├─ Match: aug->spellName == lowerSpellNameCasting
            │                │      └─ Aplica modificador por tipo
            │                │
            │                └─ doTargetCombat / doAreaCombat
            │
            └─ [4] postCastSpell(player)
                   │
                   ├─ Momentum (tier system)
                   ├─ calculateAugmentCooldownReduction()
                   │    └─ Itera equipados com type == Cooldown, soma values
                   ├─ Aplica cooldown: max(baseCooldown / 2, baseCooldown - totalReduction)
                   ├─ Mana / Soul cost
                   └─ player->clearSpellNameCasting()
```

### Ponto-Chave

`spellNameCasting` é definido no Player em `executeCastSpell` **antes** da chamada Lua, e limpo em `postCastSpell` **depois**. Isso permite que qualquer `combat:execute()` dentro da mesma spell leia o nome e aplique os augments — funciona com spells `.lua`, `.xml` ou C++.

---

## Formato XML

### Estrutura

```xml
<attribute key="augments" value="1">
    <attribute key="nome da spell" value="tipo do augment">
        <attribute key="value" value="valor numerico" />
    </attribute>
</attribute>
```

- `key` da spell é sempre **lowercase**
- `value` do tipo usa **espaços** (ex: `"magic level healing"`, `"critical hit chance"`)
- `<attribute key="value">` aninhado define a magnitude

### Múltiplos Augments no Mesmo Item

```xml
<attribute key="augments" value="1">
    <attribute key="hell's core" value="base">
        <attribute key="value" value="600" />
    </attribute>
    <attribute key="energy wave" value="base">
        <attribute key="value" value="800" />
    </attribute>
</attribute>
```

### Exemplos por Categoria

**Dano/Healing Percentual (Base, PowerfulImpact, StrongImpact, IncreasedDamage)**

```xml
<!-- Sanguine Blade — +4% dano base no Fierce Berserk -->
<attribute key="augments" value="1">
    <attribute key="fierce berserk" value="base">
        <attribute key="value" value="400" />
    </attribute>
</attribute>

<!-- Stoic Iks Robe — +8% healing base no Spirit Mend -->
<attribute key="augments" value="1">
    <attribute key="spirit mend" value="base">
        <attribute key="value" value="800" />
    </attribute>
</attribute>
```

**Cooldown**

```xml
<!-- Sanguine Trousers — -900s cooldown no Avatar of Balance -->
<attribute key="augments" value="1">
    <attribute key="avatar of balance" value="cooldown">
        <attribute key="value" value="900000" />
    </attribute>
</attribute>
```

**Critical**

```xml
<!-- Norcferatu Skullguard — +200 critical hit chance no Ethereal Spear -->
<attribute key="augments" value="1">
    <attribute key="ethereal spear" value="critical hit chance">
        <attribute key="value" value="200" />
    </attribute>
</attribute>

<!-- Sanguine Trousers — +800 critical extra damage no Chained Penance -->
<attribute key="augments" value="1">
    <attribute key="chained penance" value="critical extra damage">
        <attribute key="value" value="800" />
    </attribute>
</attribute>
```

**Leech**

```xml
<!-- Maliceforged Helmet — +300 mana leech no Front Sweep -->
<attribute key="augments" value="1">
    <attribute key="front sweep" value="mana leech">
        <attribute key="value" value="300" />
    </attribute>
</attribute>

<!-- +500 life leech no Sweeping Takedown -->
<attribute key="sweeping takedown" value="life leech">
    <attribute key="value" value="500" />
</attribute>
```

**Dinâmicos — Magic Level**

```xml
<!-- +200% ML como healing extra no Exura Sio (ML 100 → +20000 healing) -->
<attribute key="augments" value="1">
    <attribute key="exura sio" value="magic level healing">
        <attribute key="value" value="20000" />
    </attribute>
</attribute>

<!-- +50% ML como dano extra no Exevo Gran Mas Vis (ML 100 → +5000 dano) -->
<attribute key="augments" value="1">
    <attribute key="exevo gran mas vis" value="magic level damage">
        <attribute key="value" value="5000" />
    </attribute>
</attribute>
```

**Dinâmicos — Skill Damage**

```xml
<!-- +10% Distance como dano no Ethereal Spear (dist 120 → +12 dano) -->
<attribute key="augments" value="1">
    <attribute key="ethereal spear" value="skill damage">
        <attribute key="value" value="1000" />
    </attribute>
</attribute>

<!-- +10% Club como dano no Front Sweep (club 110 → +11 dano) -->
<attribute key="augments" value="1">
    <attribute key="front sweep" value="skill damage">
        <attribute key="value" value="1000" />
    </attribute>
</attribute>

<!-- +15% ML como dano no Exevo Gran Mas Vis via wand (ML 100 → +1500 dano) -->
<attribute key="augments" value="1">
    <attribute key="exevo gran mas vis" value="skill damage">
        <attribute key="value" value="1500" />
    </attribute>
</attribute>
```

### Valores Default (via Config)

Para `IncreasedDamage`, `PowerfulImpact` e `StrongImpact`, se o `<attribute key="value">` não for especificado, o valor vem do `server_config.lua`:

```xml
<attribute key="augments" value="1">
    <attribute key="exori gran" value="increased damage">
        <!-- sem <attribute key="value"> → usa config default (5% = 500 basis points) -->
    </attribute>
</attribute>
```

---

## Configuração

### `data/server_config.lua`

```lua
-- Augment System
-- Valores percentuais padrão para tipos que não especificam value explícito no XML.
-- Valores entre 1 e 100 (ex: 5 = 5%)
augmentIncreasedDamagePercent = 5
augmentPowerfulImpactPercent = 10
augmentStrongImpactPercent = 7
```

---

## Convenções de Escala

| Categoria | Escala | Exemplo |
|---|---|---|
| Base / Impact / IncreasedDamage | basis points: `value / 100.0 = %` | `400` = `+4.00%` |
| MagicLevelHealing / MagicLevelDamage / SkillDamage | basis points: `value / 100.0 = %` da skill/ML | `20000` = `+200.00%` do ML |
| Cooldown | milissegundos | `120000` = `-120s` |
| CriticalExtraDamage / CriticalHitChance | valor direto | `1400` = `+1400` |
| LifeLeech / ManaLeech | valor direto | `400` = `+400` |

**Cooldown mínimo:** nunca reduz abaixo de 50% do cooldown base (`baseCooldown / 2`).

---

## Arquivos Modificados

| Arquivo | Mudanças |
|---|---|
| `src/items.h` | `Augment_t` enum (12 valores), `AugmentInfo` struct, `ITEM_PARSE_AUGMENT`, `ItemType::augments`, `addAugment()`, `parseAugmentDescription()` |
| `src/items.cpp` | `"augments"` no `ItemParseAttributesMap`, XML parsing aninhado, `parseAugmentDescription()`, `getAugmentNameByType()`, `isAugmentWithoutValueDescription()` |
| `src/item.h` | `Item::getAugments()`, `getAugmentsBySpellName()`, `getAugmentsBySpellNameAndType()` |
| `src/enums.h` | `instantSpellName` no `CombatDamage` |
| `src/player.h` | `spellNameCasting`, `getEquippedAugmentItems()`, `getEquippedAugmentItemsByType()`, `applyItemAugments()`, `calculateAugmentCooldownReduction()`, proficiency stubs |
| `src/player.cpp` | Iteração de equipados, aplicação de todos os 12 tipos de augment (incluindo SkillDamage auto-detect), cooldown reduction |
| `src/spells.cpp` | `InstantSpell::executeCastSpell` e `RuneSpell::executeCastSpell` definem `spellNameCasting`; `postCastSpell` aplica cooldown reduction e limpa o nome |
| `src/combat.cpp` | Ambos `doCombat()` chamam `player->applyItemAugments()` após `getCombatDamage()` |
| `src/configmanager.h` | `AUGMENT_INCREASED_DAMAGE_PERCENT`, `AUGMENT_POWERFUL_IMPACT_PERCENT`, `AUGMENT_STRONG_IMPACT_PERCENT` no enum `Integer` |
| `src/configmanager.cpp` | Loading dos 3 valores via `getGlobalInteger()` de `data/server_config.lua` |
| `data/server_config.lua` | 3 entradas de config |

---

## Extensão: Network

Para enviar descrição de augments ao cliente (OTCv8 / custom):

```cpp
// protocolgame.cpp — ao enviar dados do item
std::string desc = Items::parseAugmentDescription(it, true);
if (!desc.empty()) {
    msg.addString(desc);
} else {
    msg.add<uint16_t>(0x00);
}
```

## Extensão: Bindings Lua

Registrar em `luaitemtype.cpp`:

```cpp
int LuaScriptInterface::luaItemTypeGetAugments(lua_State* L) {
    ItemType* itemType = getUserdata<ItemType>(L, 1);
    if (!itemType) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    int index = 1;
    for (const auto& aug : itemType->augments) {
        lua_newtable(L);
        setField(L, "spellName", aug->spellName);
        setField(L, "type", static_cast<int>(aug->type));
        setField(L, "value", aug->value);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}
// registerMethod("ItemType", "getAugments", LuaScriptInterface::luaItemTypeGetAugments);
```

---

## Proficiency Augments (Stub)

Estruturas em `player.h` para futura integração com árvore de proficiência de armas:

```cpp
enum WeaponProficiencyPerkAugmentType_t : uint8_t {
    PROFICIENCY_AUGMENTTYPE_NONE = 0,
    PROFICIENCY_AUGMENTTYPE_BASE_DAMAGE = 2,
    PROFICIENCY_AUGMENTTYPE_HEALING = 3,
    PROFICIENCY_AUGMENTTYPE_COOLDOWN = 6,
    PROFICIENCY_AUGMENTTYPE_INCREASED_DAMAGE = 9,
    PROFICIENCY_AUGMENTTYPE_LIFE_LEECH = 14,
    PROFICIENCY_AUGMENTTYPE_MANA_LEECH = 15,
    PROFICIENCY_AUGMENTTYPE_CRITICAL_EXTRA_DAMAGE = 16,
    PROFICIENCY_AUGMENTTYPE_CRITICAL_HIT_CHANCE = 17,
};

struct WeaponProficiencyAugment {
    uint16_t spellId = 0;
    WeaponProficiencyPerkAugmentType_t augmentType = PROFICIENCY_AUGMENTTYPE_NONE;
    float value = 0.0f;
};
```

Para integrar: definir perks em JSON, parsear em C++, armazenar em `EquippedWeaponProficiencyBonuses::spellAugments`, aplicar em `applyItemAugments()`.

---

## Debug

Log de augments mal formatados:
```
[Warning - Items::parseItemNode] Unknown augment type: X for item: Y
```

Verificar augments de um item em Lua (após registrar os bindings):
```lua
local augments = item:getAugments()
for _, aug in ipairs(augments) do
    print(aug.spellName, aug.type, aug.value)
end
```
