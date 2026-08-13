# Backlands — Proficiência de Equipamentos + Árvore de Habilidades (handoff)

Repositório: `c:\Users\aradantas\backlands1.8`, branch `weaponSystem` (partia de `b6298fb2`, idêntica a `main`).
**Nada foi compilado nem testado** — não há cmake/ninja/compilador/lua na máquina onde o código foi escrito.

## Objetivo

Substituir o antigo *Weapon Proficiency* (árvore de perks passivos vinda de um `proficiencies.json` de 27k linhas da Canary, desligado e com só 6 das ~400 árvores alcançáveis) por um modelo estilo **Final Fantasy Tactics Advance**:

- Qualquer **equipamento** (não só arma) acumula proficiência e **ensina spells a vocações específicas**.
- Com a peça equipada a spell é **emprestada**; ao atingir o nível de maestria ela vira **aprendida permanentemente** e continua castável com a peça fora.
- Uma **árvore de habilidades estilo Ragnarok** também ensina spells e amplifica as existentes, com pontos derivados do **maior level já alcançado**.

### Decisões de design fechadas

| Tema | Decisão |
|---|---|
| Granularidade | Item específico por `itemId`, em qualquer slot |
| Moeda de proficiência | XP de monstros (1%), **todas as peças equipadas ganham em paralelo** |
| Vocação | O mesmo item ensina spells diferentes por vocação (chaveado pela vocação **base**) |
| Influência nas spells | Dano, cooldown, mana, área/alcance — e novos eixos no futuro |
| Wheel of Destiny | Reaproveitar só a infraestrutura (KV, protocolo, validação); modelo de dados próprio |
| Pontos da árvore | Derivados do maior level já alcançado |
| Custo por nível | Dobra (1, 2, 4, 8…) com teto baixo de nível por skill |
| Reset de personagem | Zera marcador, pontos e distribuição |

---

## O que já foi implementado

### Fase 0 — Marcador de maior level (novo, não existia nada)

Não havia coluna, storage key nem KV rastreando o level máximo histórico. Foi construído em Lua puro, sem tocar em C++, porque `CreatureEvent.onAdvance` já é disparado em `src/player.cpp:3257` e **nenhum script registrava esse hook**.

- **`data/lib/core/player.lua`** — novos helpers: `Player.progressionKV()`, `Player.getHighestLevel()`, `Player.updateHighestLevel()`, `Player.clearHighestLevel()`. Persistência em `player:kv():scoped("progression")`, chave `highestLevel`.
  `getHighestLevel()` retorna `max(guardado, levelAtual)` — auto-corretivo, nunca reporta abaixo do level atual.
- **`data/scripts/creaturescripts/systems/highest_level.lua`** (novo) — `CreatureEvent("BacklandsHighestLevel")` com `onAdvance`, mais um `onLogin` que registra o evento por jogador e faz backfill para personagens pré-existentes.
- **`data/scripts/resetsystem/01_reset_core.lua`** — `doPlayerReset` agora limpa o marcador e a árvore **antes** da queda de level (senão o backfill do login reconstruiria o marcador a partir do level pré-reset).

### Fase 1 — Proficiência de arma → de equipamento

**C++ — portão de cast (a correção central):**

`src/spells.cpp` tinha um `if/else if` que fazia spell aprendível **ignorar a vocação por completo**. Virou sequencial — vocação sempre vale, conhecimento é filtro adicional:

- Novo `Spell::hasKnowledgeOfSpell(const Player*)` (`src/spells.h` + `src/spells.cpp`), usado tanto por `Spell::playerSpellCheck` quanto por `InstantSpell::canCast`. Os dois precisavam concordar: `canCast` alimenta o pacote `0x9F` (lista de spells do cliente) e divergir faz a UI oferecer spell que não casta.
- Sem regressão nas ~600 spells de monstro com `needLearn(true)`: elas têm `vocationSpellMap` vazio, que `spells.h` trata como "todos podem".

**C++ — spells emprestadas:**

- `src/player.h` / `src/player.cpp` — novo `std::forward_list<std::string> equipmentGrantedSpellList` com `clearEquipmentGrantedSpells()`, `addEquipmentGrantedSpell()`, `hasEquipmentGrantedSpell()` (case-insensitive). **Runtime-only, nunca persistido** — deriva do equipamento, que já é persistido.
- `src/luaplayer.cpp` — bindings `player:clearEquipmentGrantedSpells()`, `player:addEquipmentGrantedSpell(nome)`, `player:hasEquipmentGrantedSpell(nome)`.

O estado "aprendida" **reaproveita a infraestrutura que já existia**: `player:learnSpell(nome)` grava em `player_spells`, com load/save automáticos em `iologindata.cpp`. Nenhuma tabela nova.

**Lua — generalização para todos os slots.** Quatro travas removidas:

| Trava original | Situação |
|---|---|
| `getWeapon(true)` no lookup de bônus (`player.cpp:661`) | removida — mapa agora é plano por nome de spell |
| loop só em `LEFT`/`RIGHT` (`proficiency.lua`) | agora varre 8 slots (`PROFICIENCY_SLOTS`) |
| `isValidWeaponId` exigindo `WEAPON_NONE` | virou `hasProficiencyDefinition(itemId)` |
| `slot == LEFT or RIGHT` (`default_onInventoryUpdate.lua`) | removida — o evento C++ já disparava para todos os slots |

- **`data/scripts/network/proficiency/proficiency.lua`** — reescrito (~624 linhas alteradas). Global renomeado de `WeaponProficiencySystem` para **`EquipmentProficiencySystem`**. Preservados: motor de XP, cache por GUID, save debounced (5s), tabela `player_weapon_proficiency`. Removidos: carga do JSON, perks, `encodePerks`/`decodePerks`, mapas `CIPBIA_*` e `MARKET_CATEGORY_TO_PROFICIENCY`.
  Função central nova: `refreshEquipmentSpells(player)` — limpa e reconstrói do zero as spells emprestadas e os modificadores, promove o que atingiu maestria, e chama `player:reloadData()` para reenviar o `0x9F`.
- **`data/scripts/network/proficiency/equipment_spells.lua`** (novo) — substitui o `proficiencies.json`. Formato `[itemId][vocaçãoBase] = { { spell, masterLevel, modifiers } }`. **Contém apenas 1 entrada de exemplo (Fire Sword 2392)** — o conteúdo real ainda precisa ser criado.
- **`data/scripts/talkactions/god/player/weapon_proficiency.lua`** — `/proficiency` sem argumento agora relata XP, nível e estado de cada spell de cada peça equipada; com argumento, adiciona XP.
- Callers atualizados: `weapon_proficiency_onGainExperience.lua`, `default_onInventoryUpdate.lua`.

### Fase 2 — Pipeline extensível de modificadores

- Struct `ProficiencySpellAugmentBonus` renomeado para **`SpellModifiers`** (`src/player.h`). É compartilhado por proficiência, wheel e futuramente a árvore. Adicionar uma dimensão nova de poder = um campo + um `case` em `addSpellAugmentBonus` + uma linha no lambda `applyBonus` de `spells.cpp`, e **todas as fontes ganham de uma vez**.
- **Chave unificada em nome de spell.** `getProficiencySpellAugmentBonus` passou de `uint16_t spellId` para `std::string_view spellName`, igualando o lado wheel. Motivo: só 183 de 826 spells declaram id, **14 ids são duplicados** (`138` é Ignite *e* Invisible) e não existe lookup id→spell no engine.
- Storage de proficiência virou mapa plano `nome → SpellModifiers`, já somado entre as peças, em vez do antigo `itemId → spellId → bônus` que exigia resolver a arma no momento do cast.
- **Proficiência desacoplada do `augmentSystemEnabled`** — passou a ter gate próprio (`WEAPON_PROFICIENCY_SYSTEM_ENABLED`) nos 4 pontos de `spells.cpp` (dano, cooldown, cooldown de grupo secundário, custo de mana).
- **`data/lib/core/spell_modifiers.lua`** (novo) — tabela `SpellModifier` espelhando `Augment_t` de `src/items.h`, já marcando quais tipos o engine consome e quais são pontos de extensão reservados (`AffectedAreaEnlarged`, `AdditionalTargets`, `DurationIncreased`, `IncreasedDamageReduction`, `EnhancedEffect`, `IncreasedSkill` — declarados mas sem consumidor).
- Escala por nível fica **em Lua** (`perLevel` * nível do item), então rebalanceamento nunca exige recompilar.

### Fase 3 — Árvore de habilidades estilo Ragnarok

**Flag de configuração** (novo sistema, seguindo o padrão dos demais):
- `src/configmanager.h` — `SKILLTREE_SYSTEM_ENABLED` no enum `Boolean`
- `src/configmanager.cpp` — mapeamento de `skillTreeSystemEnabled` (default `false`)
- `src/luascript.cpp` — global + `configKeys`
- `src/otserv.cpp` — banner de startup
- `config.lua.dist` — `skillTreeSystemEnabled = false`

**Canal C++ próprio** (espelha o da wheel, para os três sistemas somarem independentes):
- `src/player.h`/`.cpp` — `skillTreeSpellAugments` + `clearSkillTreeSpellAugments()`, `addSkillTreeSpellAugment()`, `getSkillTreeSpellAugmentBonus()`
- `src/luaplayer.cpp` — bindings `player:clearSkillTreeSpellAugments()` e `player:addSkillTreeSpellAugment(nome, tipo, valor)`
- `src/spells.cpp` — consumido nos 4 pontos (dano, cooldown, cooldown de grupo secundário, custo de mana)

**Lua:**
- **`data/scripts/network/skilltree/skilltree_nodes.lua`** (novo) — nós com `maxLevel`, `baseCost`, `requires = {{node, level}}`, `teaches`, `modifiers`, `attributes`. Traz 5 nós de exemplo em dois ramos (fogo e resistência).
- **`data/scripts/network/skilltree/skilltree.lua`** (novo) — o sistema:
  - Custo dobrando: `costOf = baseCost << (level-1)`; acumulado `baseCost * (2^level - 1)`
  - Orçamento = `(Player:getHighestLevel() - 8) * 1`, teto 1000
  - Persistência em `player:kv():scoped("skilltree")` — sem migração de schema
  - **Respec por reescrita total**: o cliente manda o vetor inteiro, o servidor valida em isolamento (pré-requisitos conferidos contra o próprio vetor submetido) e sobrescreve. Um item de respec futuro não exige mudança no núcleo.
  - **Revalida no apply de login** — o bug da wheel (`applyWheelBonuses` aplica o KV sem revalidar, deixando char resetado com 4000 pontos para sempre) foi deliberadamente não herdado. Alocação ilegal é descartada.
  - Bônus de atributo via `CONDITION_ATTRIBUTES` com subid **86062** (a wheel usa 86061)
  - Spells de `teaches` rastreadas em KV (`grantedSpells`) para o respec devolver exatamente o que a árvore deu — e logo depois dispara `EquipmentProficiencySystem.refreshEquippedSpells` para restaurar o que o equipamento tiver masterizado
  - Protocolo: `PacketHandler(0xBC)` de entrada, `0xC1` de saída
- **`data/scripts/talkactions/god/player/skilltree.lua`** (novo) — `/skilltree` relata orçamento/gasto/alocação, `/skilltree <nodeId>, <nível>` altera um nó, `/skilltree reset` limpa. **Passa pela mesma validação da rede**, então serve para testar tudo sem cliente customizado.

⚠️ **`0xC0` não estava livre** como a varredura inicial sugeria — é o pacote de quick-loot da Astra em `luanetworkmessage.cpp`. Por isso a saída usa `0xC1`, e ele foi registrado em `isOtcOnlyLuaOpcode` para bater com o gate Lua (`isUsingOtClient`).

---

## Arquivos tocados

**Novos (4):**
```
data/lib/core/spell_modifiers.lua
data/scripts/creaturescripts/systems/highest_level.lua
data/scripts/network/proficiency/equipment_spells.lua
(pendente) data/scripts/network/skilltree/
```

**Modificados (18):**
```
.luacheckrc                                          config.lua.dist
data/lib/core/core.lua                               data/lib/core/player.lua
data/scripts/eventcallbacks/player/default_onInventoryUpdate.lua
data/scripts/eventcallbacks/player/weapon_proficiency_onGainExperience.lua
data/scripts/network/proficiency/proficiency.lua     data/scripts/resetsystem/01_reset_core.lua
data/scripts/talkactions/god/player/weapon_proficiency.lua
src/configmanager.cpp  src/configmanager.h  src/luaplayer.cpp  src/luascript.cpp
src/otserv.cpp  src/player.cpp  src/player.h  src/spells.cpp  src/spells.h
```

---

## O que precisa ser testado

### 0. Compilar
Nunca foi compilado. `./build.sh` (o projeto compila com clang e deve continuar — commit `6e75ab13`). Pontos de maior risco de erro de compilação:
- assinatura nova de `Player::addProficiencySpellAugment(std::string, Augment_t, double)` e o binding correspondente em `luaplayer.cpp`
- `SpellModifiers` (rename) — 15 ocorrências em `player.cpp`, `player.h`, `spells.cpp`
- `Spell::hasKnowledgeOfSpell` declarado em `spells.h`, definido em `spells.cpp`

### 1. Configuração
Não existe `config.lua` no repositório, só `config.lua.dist`. Copiar e ligar:
```lua
weaponProficiencySystemEnabled = true
skillTreeSystemEnabled = true   -- quando a Fase 3 estiver pronta
```
Confirmar no boot: `>> Systems: ... | Proficiency [ON] | Skill Tree [...]`.

### 2. Marcador de maior level
- Subir de level → `highestLevel` acompanha
- Morrer perdendo level → **marcador não cai**
- Relogar → backfill não corrompe o valor
- Resetar personagem → marcador zerado

### 3. Proficiência multi-slot (char Knight, com `equipment_spells.lua` populado)
- Equipar arma e elmo com definição → **as duas spells castáveis**
- `/proficiency` → XP subindo nas **duas** peças em paralelo
- Desequipar o elmo → só a spell dele recusada, com `YOUNEEDTOLEARNTHISSPELL`
- Cruzar o `masterLevel` da arma → mensagem de maestria
- Desequipar a arma → **spell continua castável**
- Relogar → `SELECT * FROM player_spells WHERE player_id = <guid>` mostra a spell
- Trocar para Sorcerer com o mesmo item → recebe a **outra** spell; Knight não vê a do Sorcerer
- Elite Knight deve receber o mesmo que Knight (resolução para vocação base)

### 4. Regressão de vocação (crítico — o portão de cast mudou)
- Spell comum de Sorcerer segue **recusada** para Knight
- Spells de monstro (`needLearn(true)`, sem vocação) seguem inacessíveis a jogadores
- Spells normais de cada vocação seguem funcionando

### 5. Modificadores
- Com `modifiers = { { type = SpellModifier.BaseDamage, perLevel = 2 } }`, o dano da spell deve escalar com o nível de proficiência da peça
- Desequipar a peça → bônus some, mas a spell mastered continua castável

### 6. Árvore de habilidades (testável sem cliente, via `/skilltree`)
- `/skilltree` → orçamento coerente com `getHighestLevel()`
- `/skilltree fire_affinity, 3` → aceita; custo total deve ser **7** (1+2+4)
- `/skilltree flame_strike, 1` **sem** `fire_affinity` nível 2 → recusa com "Missing skill requirement."
- Alocar acima do orçamento → recusa com "Not enough skill points."
- `/skilltree conflagration, 1` → aprende "Exevo Flam Hur" permanentemente (`teaches`)
- `/skilltree conflagration, 0` → **desaprende** a spell; se o equipamento também a tiver masterizado, ela deve voltar logo em seguida (o refresh de proficiência roda depois do forget)
- `/skilltree reset` → limpa tudo, condição de atributos removida
- Relogar → bônus reaplicados, e uma alocação que passou a exceder o orçamento é **descartada** (não herdamos o bug da wheel)
- Resetar personagem → marcador, pontos e distribuição zerados
- Conferir que a wheel (subid 86061) e a árvore (86062) não se apagam mutuamente

---

## Riscos conhecidos e pendências

1. **Curvas de XP desbalanceadas.** `EXPERIENCE_TABLES` em `proficiency.lua` foi calibrada para uma arma só. Agora até 8 peças ganham em paralelo. **Precisa recalibração antes de qualquer tuning sério.** Há comentário no arquivo marcando isso.
2. **`equipment_spells.lua` tem só 1 entrada de exemplo.** Todo o conteúdo real precisa ser criado.
3. **`spell:needLearn(true)` é destrutivo.** Marcar uma spell hoje disponível por vocação a torna inacessível até ser aprendida. Só usar em spells **exclusivamente** ensinadas por equipamento — spells novas são o caminho seguro.
4. **`sendBasicData` só roda para `isAstraClient`** (`protocolgame.cpp:3102`). Fora disso a lista de spells do cliente não atualiza; o portão do servidor continua valendo, mas a UI não reflete. Confirmar qual cliente o Backlands usa.
5. **Toda a UI depende de cliente customizado.** Nem a wheel nem a proficiência trazem `.otui` ou módulo OTC neste repositório. A interface da árvore é trabalho fora do escopo.
6. **`sendSpellCooldown` descarta id > 255** (`protocolgame.cpp:4716`).
7. **14 ids de spell duplicados** já quebram a lista do cliente hoje (há `std::unique` em `protocolgame.cpp:3128`). Não foi causado por estas mudanças, mas convém auditar antes de criar spells novas.
8. **Protocolo da proficiência mudou** (payload de info agora carrega nível + lista de spells em vez de perks). Qualquer UI de cliente que falava com o sistema antigo precisa ser refeita.
9. **Legado dormente por decisão:** `src/weapon_proficiency.h`/`.cpp` (32 bônus passivos), a coluna `perks` de `player_weapon_proficiency` e o `data/items/proficiencies.json` continuam no repositório, sem consumidor. Ficaram porque estão atrás do flag, custam zero desligados, e os 32 bônus são matéria-prima natural para os efeitos da árvore.

## Próximo passo (Fase 3)

Construir `data/scripts/network/skilltree/`:
- `skilltree_nodes.lua` — nós com `maxLevel` (teto baixo, ~5), `baseCost`, `requires = {{node, level}}`, `teaches` (nome de spell) e `modifiers`
- `skilltree.lua` — `costOf(node, level) = baseCost * 2^(level-1)`; orçamento = f(`Player:getHighestLevel()`); persistência em `player:kv():scoped("skilltree")`; respec por **reescrita total do vetor** (semântica que a wheel já usa: cliente manda tudo, servidor valida em isolamento e sobrescreve); `PacketHandler(0xBC)` de entrada e `0xC0`/`0xC1` de saída (bytes livres nas duas direções — verificar o gating em `src/luanetworkmessage.cpp:13-40`, senão o pacote é descartado em silêncio para cliente vanilla)

⚠️ **Não herdar o bug da wheel**: `applyWheelBonuses` aplica o que está no KV **sem revalidar contra o orçamento atual**, então um char resetado mantém 4000 pontos alocados para sempre. A árvore precisa revalidar dentro do apply de login.

⚠️ **Respec e spells aprendidas**: ao remover um nó com `teaches`, chamar `player:forgetSpell`, e em seguida disparar `EquipmentProficiencySystem.refreshEquippedSpells` para restaurar o que o equipamento tiver masterizado — senão o respec apaga uma spell que o jogador conquistou pelo outro caminho.
