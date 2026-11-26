# Auditoria incremental TFS 1.8 / 8.60 — 21/10/2026

Complemento de 26/10/2026: [lifetime de party, reentrada e concorrência de configuração](TFS18_PR337_COMPLEMENTO_20261026.md).

Baseline confirmado por fetch: `49db3276becc1d60ce35ee3c4d861fdc49385d77`.
SHA final de código/testes: `9ca057c1933cfdf9d55ab8b63a508bf46794716c`.
O commit seguinte registra somente scripts e relatórios, sem alterar produção.
Branch experimental: `perf/tfs18-audit-combat-network-20261021`.
Nenhuma alteração na main, push, PR ou merge faz parte desta execução.
Os mapas, spawns, raids, ofertas e logs locais preexistentes não são incluídos nos commits.

## Escopo e evidência

Foi lido integralmente o prompt mestre fornecido. As duas auditorias originais
citadas nele não estavam anexadas; não foram consideradas relatórios lidos.
Esta entrega separa correções funcionais de experimentos de desempenho.
Não há demonstração de capacidade para 400, 600 ou 1.000 jogadores.

| Achado | Estado | Evidência / decisão |
|---|---|---|
| B3 textos | BUG REPRODUZIDO; correção implementada | Três testes falharam contra a biblioteca original: cura positiva legada, mana shield e mana drain. Atribuir spectatorMessage em cada ramo de observador; manter mensagens do alvo/atacante. |
| B1 andares | BUG REPRODUZIDO; correção implementada | Dois casos novos falharam no baseline, incluindo evento z4/vista z7; mais uma expectativa antiga foi corrigida para a visibilidade real. Consulta é centrada no evento: superfície consulta 0..7, z6 inclui z8 e z7 inclui z8/z9. |
| B2 modificadores | BUG REPRODUZIDO; correção implementada | Quatro casos falharam com callbacks Lua reais contra a biblioteca original. Flags individuais para nível, reset de dano e Avatar preservam valores vistos por Lua e não reaplicam modificadores na reentrada. Inclui mana drain. |
| B2 cura | Não reproduzida a mesma recursão | COMBAT_HEALING já aplica a cura antes de notificar Lua e retorna sem a reentrada do dano. Preservado. Life leech em área reutiliza CombatDamage para curas distintas: não adicionar trava persistente de cura. |
| B4 weak lock/get | CONFIRMADO NO CÓDIGO; hardening implementado | Manter shared_ptr durante o uso de membros/convidados em Party e bindings Lua. Próximo líder retido também fora do loop de seleção. Não alegar UAF reproduzido onde outro dono já protege o objeto. |
| F1 NetworkMessage | CONFIRMADO NO CÓDIGO / MICROBENCH MEDIDO | Buffer de 65.500 bytes, 102 NetworkMessage locais em protocolgame.cpp, cópia completa no parse. Zero-fill e ownership de escrita continuam intactos. Apenas probe de oito bytes fora da produção. |
| F5 onTargetCombat | BUG de contrato REPRODUZIDO e corrigido; perf pendente | Callback vazio retornava true e podia interromper handlers posteriores. Dois testes com o dispatcher Lua real falharam antes e passaram com RETURNVALUE_NOERROR, preservando inclusive rejeição posterior. Registro redundante de charms continua como hipótese; não remover fallback de login/spawn/reload. |
| P4 / F4 pathfinding | HIPÓTESE A MEDIR | Reuso de caminho, throttle e fallback de A* já existem. Falha não atualiza lastFollowRepathTime no ramo examinado; não há prova de que backoff seria correto/benéfico no workload real. Sem patch de IA. |
| P1 área | Instrumentação existente; evidência histórica encontrada | Dumps de server_*.log mostram applyTargets com cerca de 90,2% do tempo inclusivo de 44.704 casts; pior cast de Stress Demon teve 139 alvos e 193,7 ms em applyTargets. Não é A/B do HEAD atual nem tempo exclusivo. |
| P2 condições | Risco de O(k²) confirmado; custo pendente | Snapshot proprietário e buscas preservam reentrância. Sem distribuição de k e perfil de carga, manter implementação e testes de lifetime existentes. |
| P5 espectadores | Ownership preservado | Nenhuma troca por raw pointers e nenhum cache global. Consulta ampliada pode visitar mais espectadores: é correção funcional, não otimização prometida. |
| P6 reactor/buckets | Preservados #332/#333 | Sem novo heap, alteração de frequência de think ou paralelismo do mundo. Logs históricos apontam checkCreatures como tarefa longa e AoE concentrada em aplicação aos alvos; faltam perfil exclusivo e workload atual para decompor seus filhos. |
| P7 tiles | HIPÓTESE A MEDIR | Materialização e criação de tiles de combate não são classificadas como leaks sem evidência de retenção indevida. Nenhum callback ou campo omitido. |
| P8 custos menores | Pendente | summonCounts, autosend swap, logger C++/Lua, distância de reward boss, pool e allocator exigem experimentos separados. Não fazer essas mudanças junto com as correções de gameplay. |

## Compatibilidade e limites das correções

B1 amplia a consulta genérica, que já era um superset da visibilidade do protocolo.
Mantém o range subterrâneo anterior e a geometria X/Y; o envio continua filtrando
canSee e instâncias nos chamadores. `Creature::canSee`, usado pela IA, não foi
reescrito para imitar o cliente. Teste com ProtocolGame real verifica inclusão
de todo viewer visível em z0..15, bordas X/Y, remoção e filtro de instância.
Isso não é teste interativo de teleporte, morte ou todos os pacotes de movimento.

B2 preserva arredondamento e posição dos modificadores antes do callback.
Os eventos continuam vendo os valores previamente modificados. O retorno Lua
é aplicado sem novo bônus/redução. Dano primário/secundário, origens condition/
reflect/spell, flags critical/fatal/leeched, mana shield, mana drain e override
numérico Lua têm regressões. As flags critical/fatal/leeched nesse teste não
substituem um cast completo com RNG/charms/leech; este exige cenário de jogo.

Na busca completa por `lock().get()` em src, comparações que terminam na mesma
expressão e argumentos cuja referência temporária dura até o retorno da chamada
não foram alterados mecanicamente. Getters raw de Creature/Player/Tile pertencem
a contratos de ownership maiores e não foram substituídos globalmente. A mudança
de B4 é focal em usos após o término da expressão, não uma prova de segurança de
todos esses getters nem de toda reentrada possível em Party.

## Auditoria de segurança do buffer

- Construção e cópia: NetworkMessage mantém array inicializado; allocate_shared
  e pooling mantêm construtor, destructor e controle de ownership. O teste que
  compara a capacidade inteira foi mantido.
- Escrita: add/get usam cursor iniciado em 8; setters e skips tornam um redesign
  de intervalos inicializados maior que uma remoção de `= {}`. Os bindings Lua
  permitem buscar posições, inclusive áreas ainda não escritas.
- Recepção: Connection lê header/body, ajusta comprimento e cursor; checksum,
  XTEA e RSA usam ranges distintos. O clone do parse mantém estado e buffer inteiro.
- Append copia os bytes, não guarda span para stack. OutputMessage permanece
  proprietário durante async_write; filas e handlers de Connection não mudam.
- Padding escreve 0x33 e aumenta length sem avançar position. Não foi alterado
  incidentalmente; framing do probe é comparado ao original incluindo padding.
- Nenhuma flag de stack-clash/checkstk desabilitada. Não há teste MSan completo
  do servidor; Memcheck do probe e ASan/UBSan têm alcance distinto.

## Ordem proposta para revisão humana

1. Textos de observadores e regressões.
2. Floor range com propriedade de inclusão e instância.
3. Reentrada de modificadores com Lua real.
4. Pinagem de party e seus testes.
5. Contrato do callback padrão de targeting, sem desabilitar handlers reais.
6. Ferramentas, probe de rede e relatórios (sem mudança do emissor de produção).

Commits locais, em ordem:

1. `288b6c76` — B3, mensagens e regressões.
2. `ce16e6aa` — B1, andares e inclusão de espectadores.
3. `81950330` — B2, reentrada de modificadores.
4. `51f1f950` — B4, lifetime de party.
5. `bbf155f4` — contrato F5 de callbacks Lua.
6. `4a617c2b` — probe inicializado e equivalência de bytes, sem produção.
7. `9ca057c1` — fixtures com afinidade de dispatcher e lifecycle de Chat corretos.

Validação final: GCC Release 64/64, ASan+UBSan 64/64, MSVC Release 64/64,
Clang diagnóstico 14/14 e Memcheck do teste de bytes com zero erros.
Os limites e falhas intermediárias estão detalhados em
[TFS18_REGRESSION_TESTS_20261021.md](TFS18_REGRESSION_TESTS_20261021.md).
Não houve build Clang -O3 completo nem carga de bots nesta execução.

### Diff de código e testes contra a main

Contagem de linhas adicionadas/removidas até `9ca057c1`:

| Arquivo | + | - |
|---|---:|---:|
| data/scripts/eventcallbacks/creature/default_onTargetCombat.lua | 3 | 3 |
| src/benchs/bench_networkmessage.cpp | 47 | 0 |
| src/benchs/small_packet_probe.h | 13 | 0 |
| src/enums.h | 3 | 0 |
| src/game.cpp | 24 | 9 |
| src/game.h | 1 | 0 |
| src/luaparty.cpp | 2 | 2 |
| src/map.cpp | 4 | 3 |
| src/party.cpp | 23 | 21 |
| src/protocolgame.h | 2 | 0 |
| src/tests/protocolgame_audit_support.h | 17 | 0 |
| src/tests/test_combat_messages.cpp | 158 | 0 |
| src/tests/test_combat_reentry.cpp | 209 | 0 |
| src/tests/test_lua_callback_contract.cpp | 34 | 0 |
| src/tests/test_map_spectators.cpp | 62 | 3 |
| src/tests/test_networkmessage_wire.cpp | 81 | 0 |
| src/tests/test_party_lifetime.cpp | 123 | 0 |

Há ainda quatro scripts novos em scripts/profiling: run_audit_20261021.sh,
build_audit_candidate.sh, build_audit_clang.sh e check_audit_baseline.sh;
e os três relatórios exigidos em docs/performance. Para o diff completo com
hunks e números de linha, incluindo esses arquivos após o commit de documentação:

```bash
git log --reverse --oneline main..HEAD
git diff --stat main..HEAD
git diff --numstat main..HEAD
git diff main..HEAD
```

Após esses commits, executar jogo real antes de autorizar PR/merge. Para perf,
capturar no servidor de TESTE a matriz A–G com mesmo mapa/config/bots, três ou
mais repetições por cenário e métricas por thread. Priorizar decomposição de
checkCreatures em IA, condições, casts e rede. Otimizações estruturais permanecem
fora desta entrega enquanto esse gate não for satisfeito.
