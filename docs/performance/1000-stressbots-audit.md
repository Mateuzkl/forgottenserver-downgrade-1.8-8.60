# Auditoria TFS 1.8 / 8.60 — 1.000 stressbots

## Baseline e limites da conclusão

- Data: 2026-10-15. Base: `a5068b48918254cbf2e13c5aec19cc691f2d2cb8`.
- Branch: `perf/tfs18-1000-stressbots`, criada depois de `git pull --ff-only origin main`.
- Alterações anteriores, incluindo `src/combat_packet.h`, preservadas no stash
  `d122d8ee4bbcbb37bf6716c6b13385efcd627614`. Não fazem parte desta auditoria.
- Mapas, spawns, casas e logs locais não foram removidos nem usados como fixtures
  destrutivas. Não há alteração em `config.lua`, banco ou regras de gameplay.
- Stack confirmada: C++23 no CMake; protocolo 8.60 no README; headers e biblioteca
  Lua 5.5.0 disponíveis no WSL Ubuntu 24.04.
- Máquina: Intel Core i5-10300H, 4 cores / 8 threads; WSL com aproximadamente
  7,7 GiB de RAM e 2 GiB de swap. Compilador GCC 13.3.0; CMake 3.28.3.

**Não há perfil novo do servidor com 1.000 bots neste trabalho.** O relato e os
documentos anexados orientam a inspeção, mas não provam a causa do lag. Os números
históricos de `CALLGRIND_RUNTIME_HOTSPOTS.md` não são um baseline desta revisão.
Resultados de build, regressões e microbenchmark ficam no documento de validação.

## Diagnóstico diferencial

| Hipótese | Evidência no checkout | Frequência / custo sob carga | Prioridade | Intervenção segura |
| --- | --- | --- | --- | --- |
| Game thread / fila saturada | `TaskReactor::executeReadyTasks` mede callbacks, latência, deferred e orçamento | Não confirmado: falta captura com bots | P1 | Usar métricas existentes; não aumentar limites para esconder backlog |
| Fan-out e espectadores | `Map::moveCreature` consulta old/new posição, une snapshots e calcula stackpos por observador | Cópia de cada referência do segundo snapshot comprovada; participação no lag não medida | P1 | Avaliar transferência de ownership de snapshots locais descartáveis |
| AI e pathfinding | `Monster::updateTargetListAfterMovement`, `MonsterOnThink`, `MapGetPathMatching` e métricas de paths existem | Não confirmado nesta revisão | P2 | Separar bots dispersos/concentrados e repetir o mesmo replay |
| Scripts, condições e GC | Condition/KV já têm finalizador tipado; `Item` destruía o slot sem reconstruí-lo; Spell owned não tinha GC | Novo teste no baseline confirmou retenção de 100 spells descartadas; não é diagnóstico do lag | P0 correctness | GC tipado de Item e finalização de Spell distinguindo owned/borrowed |
| Rede / clientes lentos | `Connection::send` já limita `MAX_PENDING_WRITE_MESSAGES`; escrita captura o owner da mensagem | Ausência de limite relatada no HTML não se confirma no código atual | Monitorar | Manter backpressure e ordem de pacotes; testar a regressão existente |
| Churn de shared_ptr | União de snapshots usa `insert` com iteradores const; loops principais já usam `const auto&` e pointers locais | Número de cópias redundantes é demonstrável; custo acumulado depende de profiling | P1 | Medir união isolada e manter snapshots fortes |
| Rate limit dos bots | Config possui limites por IP, quantidade de conexões e packets/s | Config efetiva e ações/s do cenário real não fornecidos | Antes do stress | Validar ambiente privado; não afrouxar a configuração pública |

## Comparação com Canary

Referência: [commit 48f7af8 / #3987](https://github.com/opentibiabr/canary/commit/48f7af828c432da4989c9ffb15900fd6cf958337).
Usado como referência de contratos de ownership, não como patch para cherry-pick.

Já existem nesta base os accessors `Creature::getPlayer/getMonster/getNpc` raw,
type checks sem `shared_from_this`, snapshots fortes em `SpectatorVec`, GC tipado
de Condition e KV, e validação fraca de userdata Creature/Tile. NetworkMessage
recebida em `Events::eventPlayerOnNetworkMessage` é **shared**, não um objeto de
stack emprestado: `pushSharedPtr(L, msg)` copia o owner real. Introduzir um deleter
no-op nessa rota reduziria a garantia de lifetime, em vez de corrigi-la.

## Inventário de userdata observado

Cada representação abaixo foi conferida nas rotas `push*` e registros locais;
esta tabela não certifica todos os usos possíveis dos objetos depois de reload.

| Tipo exposto | Representação / owner | Finalização existente | Observação |
| --- | --- | --- | --- |
| Condition | `shared_ptr<Condition>`, incluindo clone / getCondition | `luaSharedPtrGC<Condition>`; delete/close resetam | Correção equivalente já presente |
| KV | `shared_ptr<KV>` em scoped / player:kv | `luaSharedPtrGC<KV>` | Não criar traits redundantes |
| NetworkMessage | `shared_ptr<NetworkMessage>`, factory ou callback shared | gc/close resetam | Não expõe método delete; reset do owner libera a referência |
| Combat | `shared_ptr<Combat>` e registro no LuaEnvironment | delete/gc/close resetam | O registro pode manter outro owner legitimamente |
| Guild | `shared_ptr<Guild>` retornado pelo Game | gc reseta | Não é borrowed |
| House | `shared_ptr<House>` | `luaSharedPtrGC<House>` | Já possui teste de GC repetido |
| Imbuement | `shared_ptr<Imbuement>` | `luaSharedPtrGC<Imbuement>` | Tipado existente |
| Zone | `shared_ptr<Zone>` | delete/gc/close resetam | Registro de zonas pode manter owner |
| Item, Container, Teleport | Slot **`shared_ptr<Item>`** também nas metatables derivadas | Mesmo `luaItemGC` | Não especializar GC como `shared_ptr<Container>` / `shared_ptr<Teleport>` |
| Creature, Player, Monster, Npc | Raw slot + weak Creature em uservalue | `luaCreatureGC` destrói weak e remove uservalue | Não estende lifetime; acesso validado antes do uso |
| Tile | Raw slot + weak Tile em uservalue | `luaTileGC` destrói weak e remove uservalue | Owner externo; sem conversão para shared fictício |
| Town, Group, Vocation, ItemType, MonsterType, NpcType | Raw pointers para registros do servidor | Sem delete proprietário no GC | Contrato de lifetime externo; reload exige auditoria específica |
| Party | Raw pointer; owner mantido pelo sistema de parties | Disband invalida o slot usado | Aliases após disband merecem auditoria separada, não refatorados aqui |
| ModalWindow, ChatChannel, Loot, MonsterSpell | `OwnedUserdata<T>` com unique owner | delete/gc/close via `deleteOwnedUserdata` | Não são shared userdata |
| Action, TalkAction, CreatureEvent, MoveEvent, GlobalEvent | Owned na criação; registro pode transferir ownership | `deleteOwnedUserdata` | Game.getTalkActions também expõe borrowed; helper distingue representação |
| Weapon | `OwnedUserdata<Derived, Weapon>` | `deleteOwnedUserdata` | Deleter retém o tipo de construção |
| Spell / CombatSpell | Owned derivado na criação; borrowed em lookup | Spell agora registra GC/close; slots curtos borrowed não são destruídos | Corrigido leak de Spell não registrada; registro continua transferindo o owner |
| OfflinePlayer | `OwnedUserdata<Player>` | `luaOfflinePlayerRemove` em gc/close | Não confundir com Player online borrowed |
| XMLDocument, XMLNode | `OwnedUserdata<T>` | `deleteOwnedUserdata` | Validade de node após descarte do document é outra questão de lifetime |
| Position, Outfit, Variant | Tabelas Lua | Sem objeto shared C++ a destruir | Não converter para shared userdata |

## Mudanças e contrato de lifetime

Implementação restrita a mudanças verificáveis, sem trocar o armazenamento de snapshots:

1. União consumidora de `SpectatorVec`: mover as referências de um snapshot local
   que não será consultado novamente. Manter o overload const para callers que
   precisam preservar a origem, bem como sort/unique, remoção de null e guardas de
   self/empty merge. Testar origem consumida, reutilização, cópia preservada,
   partições e liberação do último owner.
2. `Map::moveCreature`: reservar stack positions pela quantidade de jogadores,
   não por todos os monstros/NPCs. Manter o cálculo individual e a ordem dos
   envios/callbacks exatamente como antes.
3. `luaItemGC`: usar o mesmo helper tipado de Condition para destruir e
   reconstruir um slot vazio. Assim métodos posteriores veem nil e uma chamada
   manual seguida do GC real não acessa um shared_ptr cujo lifetime já terminou.
   Cobrir Item/Container/Teleport armazenando sempre `shared_ptr<Item>`.
   `Lua::getThing` também usa os accessors null-safe ao receber Container/Teleport
   já liberados, sem desreferenciar um slot vazio.
4. `registerSpells`: registrar GC/close para as spells owned que nunca chegam a
   ser registradas. `deleteOwnedUserdata` verifica tipo e tamanho antes de ler o
   header, pois o mesmo metatable é usado em lookups borrowed de apenas um
   ponteiro. O deleter existente retém o tipo derivado real e o slot é invalidado
   depois da transferência de owner. A API de registro não foi modificada.

O teste novo de Spell foi executado **antes do patch de produção**: as três
regressões falharam. A primeira continuou com 100 objetos vivos depois de dois
GCs completos e do fechamento do Lua; as outras detectaram a ausência de
finalizador em borrowed/transfer. As seis suítes existentes selecionadas
passaram no baseline. Isso confirma um defeito de lifecycle, não sua contribuição
para o lag reportado com bots.

Todos os borrows continuam síncronos, na game thread, ancorados pelo snapshot
forte que recebe os owners. Nenhum raw pointer novo é capturado por task, cache,
timer ou callback assíncrono. Não foram removidas checagens de instância,
visibilidade ou remoção, nem modificados AI, colisões, spells, cooldown ou bytes
do protocolo. Não se atribui ganho de capacidade a essas mudanças sem A/B real.

Validação local final: executável Release/LTO compilado; 52/52 entradas CTest
passaram (com o roundtrip de banco deliberadamente pulado dentro de seu
executável); 4/4 executáveis selecionados passaram com ASan/UBSan/LSan. A união
consumidora apresentou medianas menores nos nove casos das duas rodadas do
microbenchmark, com restrições de variância e framework detalhadas no relatório
de validação. Nenhum desses resultados demonstra a causa do lag com 1.000 bots.

## Pendências e critérios de encerramento

### Rastreabilidade do patch

Linhas deste checkout, antes de commit:

| Arquivo / linha | Mudança |
| --- | --- |
| `src/spectators.h:40` | Overload consumidor; normalização comum em `:107` |
| `src/map.cpp:417` | Consumo da origem; reserva de stackpos por jogadores em `:421` |
| `src/combat.cpp:1026`, `src/game.cpp:7922`, `src/monster.cpp:972` | Consumir somente snapshots locais não reutilizados |
| `src/luaitem.cpp:1255` | GC tipado de Item, também usado nas duas metatables derivadas |
| `src/luascript.cpp:1408` | Resolver Container/Teleport liberados sem desreferenciar slot vazio |
| `src/luascript.h:878` | Deleter owned valida tipo/tamanho antes de ler o header |
| `src/luaspells.cpp:1006` | Registrar GC/close de Spell |
| `src/tests/test_spectators.cpp:338` | Cinco regressões novas de união consumidora |
| `src/tests/test_lua_userdata_gc.cpp:57` | Dez regressões novas de GC e ownership |
| `src/benchs/bench_spectators.cpp:129` | Comparação copy/consume com snapshots fortes equivalentes |

`git diff --stat` dos arquivos já rastreados: 11 arquivos, 170 inserções e 20
remoções. Além deles, há três arquivos novos: o teste de userdata e os dois
documentos desta auditoria/validação. Mapas, logs locais e o stash anterior ficam
fora do commit deste lote.

### Validação restante

- `PENDENTE DE TESTE REAL`: 100/250/500/1.000 bots, três repetições por cenário,
  idle, movimento disperso/concentrado e hunt/spells com mesmas ações/s.
- Clientes 8.60 reais: stack alto, bordas de viewport, invisibilidade/ghost,
  instâncias, escadas, teleporte, morte, summons e disconnect em callbacks.
- Heap/RSS após ramp-up, desconexão e estabilização; sanitizers não substituem um
  teste prolongado nem demonstram ausência universal de leaks.
- Perfil CPU por thread, fila e taxa de pacotes antes de escolher o próximo
  hotspot. Não houve remoção global de código ou pointers apenas por aparência.
- Este inventário não certifica todos os inputs ou lifetimes dos bindings.
  Transferências legadas via `releaseOwnedUserdata`, aliases borrowed depois de
  reload/disband e a relação XMLNode/XMLDocument exigem testes específicos antes
  de qualquer afirmação de segurança global. Não foram refatorados neste lote.
