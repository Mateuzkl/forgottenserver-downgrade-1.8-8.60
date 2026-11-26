# Medições e limites — TFS 1.8, 21/10/2026

## Ambiente real desta execução

Intel Core i5-10300H (4 cores/8 threads), WSL2 Ubuntu 24.04 x86_64,
kernel 6.18.33.2-microsoft-standard-WSL2, 7,7 GiB de RAM disponíveis à VM e
2 GiB de swap. GCC 13.3.0, Clang 18.1.3, CMake 3.28.3, Ninja, Lua 5.5
local. MSVC 19.51.36260.0 / VS 18 também encontrado no Windows.
Baseline: `49db3276becc1d60ce35ee3c4d861fdc49385d77`.

Release Linux: `-O3 -DNDEBUG`, native optimizations OFF, mimalloc OFF,
stats compiladas, testes sem LTO e servidor com LTO. Stack-clash protection
permanece nas flags do GCC. ASan/UBSan não é configuração de performance.
Fontes e builds separados em `/home/mateus/tfs-audit-20261021-0sqVGT`;
baseline por git archive, não pelo mapa/config sujos do checkout.

## MICROBENCH MEDIDO — não é ganho do servidor

`bench_networkmessage`: mesma saída de oito bytes, mesmo OutputMessage
inicializado/reutilizado e append proprietário. Estratégia baseline constrói
NetworkMessage de capacidade 65.500; probe constrói array inicializado de oito
bytes. DoNotOptimize/ClobberMemory impedem eliminar a construção pelo escape
modelado. O probe NÃO é chamado por nenhum sender de produção.
O buffer de saída é construído fora do loop medido; stack probes de entrada da
função não são medidos por pacote nesse loop. O escape forçado também pode
reter inicializações que LTO eliminaria em alguns call sites reais. Isso reforça
a necessidade de perf do executável de produção, não extrapolação deste teste.

Google Benchmark 1.8.3, calibração automática, mínimo 0,1 s por repetição,
sem fase independente de warmup, cinco repetições por estratégia, CPU-time
em nanossegundos. Primeira execução:

| Repetição | NetworkMessage + append | Array pequeno + append |
|---|---:|---:|
| 1 | 1129,67 | 9,49 |
| 2 | 1170,60 | 10,47 |
| 3 | 1133,88 | 9,38 |
| 4 | 1119,49 | 10,57 |
| 5 | 1152,22 | 9,67 |
| Média | 1141,17 | 9,92 |
| Mediana | 1133,88 | 9,67 |
| Desvio padrão | 20,27 | 0,56 |

Após terminar os builds, houve uma segunda execução, sem compilação concorrente,
com os mesmos parâmetros e binário:

| Repetição | NetworkMessage + append (ns) | Array pequeno + append (ns) |
|---|---:|---:|
| 1 | 1252,05 | 9,46 |
| 2 | 1409,46 | 11,00 |
| 3 | 1338,55 | 8,99 |
| 4 | 1312,18 | 9,78 |
| 5 | 1215,38 | 9,07 |
| Média | 1305,52 | 9,66 |
| Mediana | 1312,18 | 9,46 |
| Desvio padrão | 75,70 | 0,81 |

Dados brutos: `red-tests/bench_networkmessage.json` (primeira execução) e
`bench_networkmessage_idle.json` (segunda), no diretório isolado. A variação
entre execuções reforça que estas não são medidas de capacidade do servidor.

Limitações importantes: compilação concorrente na primeira execução; CPU sem
pinagem/frequência fixa; a biblioteca Google Benchmark do sistema avisa que foi
compilada como DEBUG em ambas. Portanto, é um experimento exploratório, não
critério para aceitar uma otimização de produção. Repetir com biblioteca Release,
warmup isolado e workload real antes de usar os números para decisão. Não
extrapolar a razão entre essas colunas para CPU, p99, FPS ou capacidade de jogadores.

O perf versionado `/usr/lib/linux-tools/6.8.0-147-generic/perf` funciona neste WSL;
o wrapper `/usr/bin/perf` inicialmente falhou por versão de kernel. Um perfil
APENAS do microbench baseline (142 amostras de cycles:u, nenhuma perdida) atribuiu
94,01% exclusivo a `__memset_avx2_unaligned_erms`, 2,97% a memmove/memcpy e
1,49% a addPosition. Isso confirma o custo nesse probe, não sua participação no TFS.
Captura: `network-probe.perf` no diretório isolado. Nenhum perfil do servidor foi coletado.

## Evidência histórica, não A/B

O dispatcher.log local contém 446 janelas e valores entre 0 e 720 jogadores;
as últimas janelas tinham 1 jogador. Em 21 janelas com >=400 online, a soma de
tempos informados por tarefa foi:

| Tarefa | Tempo agregado reportado (ms) | Chamadas |
|---|---:|---:|
| checkCreatures | 277735 | 2955 |
| checkCreatureWalk | 61288 | 57903 |
| checkCreatureAttack | 19463 | 194566 |
| lambda de login | 14198 | 820 |
| OutputMessagePool::sendAll | 5477 | 14837 |
| ProtocolGame::parsePacket | 3969 | 93727 |
| Lua timer callback | 2256 | 48181 |
| onRecvFirstMessage | 1775 | 819 |
| think de spawn | 1086 | 1789 |
| checkDecay | 843 | 4276 |

São categorias agregadas de tarefas, com código/configuração histórica não
identificados por SHA. Não são o top 10 exclusivo de um flamegraph, nem medição
do patch. O log de tarefas muito lentas inclui checkCreatures de 1706 ms em
15/10. Só isso não distingue IA, condições, Lua, geometria de área ou efeitos.
Não somar esses totais com os logs Lua/special internos: isso duplicaria tempo.
Não é possível derivar p95/p99 dos agregados recebidos.

### Métricas históricas de escopos e AoE

Uma busca posterior nos `server_*.log` também encontrou dumps PerformanceMetrics.
São versões históricas, misturando startup, login e jogo. Há escopos aninhados:
os totais abaixo NÃO são tempos exclusivos, NÃO podem ser somados entre si e
NÃO são o ranking de CPU do build atual. O p99 mostrado é o maior limite de
bucket p99 informado por uma janela, não o percentil de toda a amostra.

| Escopo | Total inclusivo reportado (ms) | Chamadas | Maior chamada (ms) | Maior p99 de janela (ms) |
|---|---:|---:|---:|---:|
| combatChangeHealth | 482285,07 | 1340774 | 95,14 | 4,19 |
| checkCreatures | 282955,22 | 114813 | 995,10 | 1073,74 |
| CombatSpell::castSpell | 258528,06 | 119093 | 197,32 | 268,44 |
| Combat::doCombat | 158516,43 | 88578 | 197,32 | 268,44 |
| Combat::doAreaCombat | 149612,43 | 44704 | 197,31 | 268,44 |
| area.applyTargets | 134922,84 | 44704 | 193,70 | 268,44 |
| executeConditions | 112897,99 | 490422 | 88,43 | 16,78 |
| area.processTiles+collectTargets | 13962,49 | 44704 | 77,51 | 33,55 |
| Map::getSpectators | 8836,19 | 1807352 | 10,02 | 0,13 |
| Player::onThink | 7982,51 | 276718 | 68,88 | 16,78 |

Nos 44.704 casts com métricas de fase, applyTargets respondeu por aproximadamente
90,2% da duração inclusiva de doAreaCombat. A coleta inicial de espectadores
somou 155,06 ms, construção 491,93 ms e preparo 11,48 ms. Isso aponta para investigar
o trabalho por alvo, não para assumir que o build inicial da área seja dominante.

Exemplo mais lento em `server_20261015_210901.log`: Stress Demon, spell condition,
posição (1030,1019,7), geometria 13x13, 37 células ativas, 139 alvos, zero tiles
criados, duração 197303,7 us; fases: build 18,4 us, preparo 0,5 us, espectadores
15,0 us, coleta de alvos/tiles 3567,4 us, aplicação 193702,2 us. As fases são do
mesmo cast. Nessa ocorrência específica, aplicação consumiu cerca de 98,2%.

Map::getPathMatching acumulou 491,34 ms em 27.044 chamadas, máxima 5,47 ms.
Essa comparação histórica não sustenta atribuir todo o lag ao A*.
QueueLatency registrou máxima de 14,005 s e limite p99 de janela de 17,180 s;
latência em fila é espera, NÃO CPU. Callbacks longos bloqueiam o dispatcher,
e melhorar heap/admissão não preempta uma aplicação de combate em andamento.
É necessário separar startup/login de carga estável antes de comparar o patch.

## Matriz A–G solicitada

| Cenário | Resultado |
|---|---|
| A: 400 bots, sem monstros | NÃO EXECUTADO |
| B: 400 bots + 10 Demons | NÃO EXECUTADO |
| C: 400 bots + 10 Stress Demons | NÃO EXECUTADO |
| D: C com FX diagnóstico reduzido | NÃO EXECUTADO; build de gameplay normal não reduz efeitos |
| E: C com spells diagnóstico desligadas | NÃO EXECUTADO; spells normais não desligadas |
| F: C com 600 bots | NÃO EXECUTADO |
| G: C com 1.000 bots | NÃO EXECUTADO |

Não foi iniciado servidor de teste nem carga contra o servidor do usuário.
Não há comparação válida baseline/patch de CPU por processo/thread, RSS,
alocações, pacotes/s, bytes/s, ReactorQueueLatency ou path requests sob bots.
Logo, o ranking de custos exclusivos do servidor atual e os ganhos em jogo
continuam INDISPONÍVEIS. Há evidência histórica inclusiva acima, não um novo
perfil A/B. O gate de otimização estrutural permanece fechado.

## Repetição do experimento

Executar os scripts Bash via WSL a partir do repositório:

```bash
bash scripts/profiling/run_audit_20261021.sh
# Use AUDIT_ROOT impresso pelo script:
bash scripts/profiling/check_audit_baseline.sh "$AUDIT_ROOT" test_networkmessage_wire bench_networkmessage
"$AUDIT_ROOT/red-tests/bench_networkmessage" --benchmark_repetitions=5 --benchmark_min_time=0.1s
```

`AUDIT_ROOT` acima deve ser atribuído pelo operador ao diretório impresso; não é
uma variável automaticamente exportada entre processos. Os arquivos JSON/logs
brutos permanecem no diretório isolado. As correções B1–B4 são funcionais ou de
lifetime, não apresentadas como otimizações de CPU.
