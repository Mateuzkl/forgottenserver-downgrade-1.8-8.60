# Validação — TFS 1.8 / 1.000 stressbots

Base: `a5068b48918254cbf2e13c5aec19cc691f2d2cb8`. Ver o relatório de auditoria
ao lado para escopo, ownership, stash anterior e hipóteses não confirmadas.

## Ambiente local e comandos

Comandos Linux executados pelo WSL Ubuntu 24.04, na raiz do checkout. Dependências
já instaladas; não executar `build.sh` para instalar pacotes durante a auditoria.

```bash
cmake -S . -B build-tests-stressbots-audit -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DENABLE_TEST_LTO=OFF \
  -DBUILD_BENCHMARKING=ON -DSKIP_GIT=ON -DUSE_MIMALLOC=OFF \
  -DLUA_INCLUDE_DIR=/usr/local/include -DLUA_LIBRARY=/usr/local/lib/liblua.a \
  -Dmio_DIR=/home/mateus/.local/share/cmake/mio \
  -Dsimdutf_DIR=/home/mateus/.local/lib/cmake/simdutf

cmake --build build-tests-stressbots-audit --target \
  test_spectators test_item_lifetime test_condition_shared_lifetime \
  test_map_spectators test_reactor test_connection_write_lifetime -j2

ctest --test-dir build-tests-stressbots-audit --output-on-failure \
  -R '^(test_spectators|test_item_lifetime|test_condition_shared_lifetime|test_map_spectators|test_reactor|test_connection_write_lifetime)$'
git diff --check
```

`ENABLE_TEST_LTO=OFF` utiliza a biblioteca de testes sem IPO; a biblioteca e o
executável de produção mantêm a configuração Release/LTO. Resultados desta
toolchain Linux não substituem o gate Windows/MSVC.

## Resultados locais

Baseline: build Release de seis executáveis selecionados concluído com sucesso;
CTest passou em `test_spectators`, `test_item_lifetime`,
`test_condition_shared_lifetime`, `test_map_spectators`, `test_reactor` e
`test_connection_write_lifetime` (6/6).

Em seguida, antes de alterar produção, `test_lua_userdata_gc` com as primeiras
três regressões de Spell foi compilado contra a biblioteca baseline e falhou nas
três: retenção de 100 spells owned descartadas e ausência de GC nas duas rotas
que precisam distingui-lo de ownership externo. Não houve carga nem alteração
do banco de dados nessa reprodução.

Patch:

- Executável `tfs` Release/LTO compilado; nenhum warning novo do compilador nos
  logs do rebuild final. Não foi iniciado contra mapas/banco locais.
- Os 47 executáveis de testes foram compilados, e o CTest concluiu **52/52**
  entradas com sucesso: 47 executáveis + 5 checks estáticos. O roundtrip interno
  de `test_inbox_db_roundtrip` foi **pulado**, sem conexão a um banco de testes.
- `test_lua_userdata_gc`: **10 casos** passaram, incluindo spells owned,
  borrowed, transferidas, layouts Instant/Rune, close, Item/Container/Teleport,
  KV, NetworkMessage shared e Condition clone/delete/close.
- `test_spectators`: **17 casos** passaram, incluindo comparação da ordem de
  copy/move merge, null/duplicatas, partições, origem consumida/reutilizável,
  self merge e preservação/liberação do último owner.
- Build separado Debug com ASan + UBSan: **4/4** executáveis selecionados
  passaram com `detect_leaks=1` e `halt_on_error=1`, sem diagnósticos nessas
  execuções. Isso não é uma certificação global de ausência de leaks/UAF.
- Hashes SHA-256 dos 10 arquivos preservados de mapas, spawns, casas e logs
  continuaram idênticos ao snapshot anterior ao stash/switch.

Build ampliado executado no PowerShell, com um único build por diretório:

```powershell
$testTargets = Get-ChildItem -LiteralPath src/tests -Filter 'test_*.cpp' -File |
    ForEach-Object { $_.BaseName }
wsl --cd '/mnt/c/Users/Mateus/Desktop/DLL and Server/forgottenserver-downgrade-1.8-8.60' `
    --exec cmake --build build-tests-stressbots-audit --target $testTargets --parallel 2
```

```bash
cmake --build build-tests-stressbots-audit --target tfs bench_spectators -j2
env -u TFS_TEST_DB_HOST -u TFS_TEST_DB_USER -u TFS_TEST_DB_PASS \
  -u TFS_TEST_DB_NAME -u TFS_TEST_DB_PORT \
  ctest --test-dir build-tests-stressbots-audit --output-on-failure \
  --timeout 60 --output-log full-tests.log -R '^(test_|check_)'
```

Durante a preparação houve um relink interrompido, seguido de biblioteca
temporária ausente. O archive e os executáveis foram regenerados com sucesso
antes da validação final. Isso foi uma falha local do processo de build, não
uma falha de compilação do código C++.

Logs locais ignorados pelo Git: `build-tests-stressbots-audit/baseline-build.log`,
`patched-rebuild.log`, `final-production-build.log`, `all-tests-build.log`,
`asan-build.log` (esses últimos também dentro do diretório de build),
`full-tests.log` e `asan-tests.log` na raiz. Os logs detalhados de cada caso ficam
em `Testing/Temporary/LastTest.log` de cada build.

Não executados: build Windows/MSVC, TSan, testes com clientes reais, RSS do
servidor sob carga prolongada, profiling do servidor e A/B com bots.
Uma primeira configuração sem os prefixes locais de mio/simdutf falhou por falta
de mio, antes de compilar. A configuração acima usa os paths já instalados,
sem downloads ou instalação de dependências.

## Microbenchmark isolado de união de snapshots

`bench_addSpectators_snapshots<false>` usa a união const/cópia equivalente ao
baseline; `<true>` consome o segundo snapshot. Ambos constroem os mesmos dois
snapshots fortes e executam a mesma ordenação, deduplicação e partição. Objetos
e shuffle usam os mesmos tamanhos/seeds. Uma thread é criada/joinada **antes** do
loop medido para exercitar o caminho de refcount de uma aplicação multithread
também no libstdc++/glibc; nenhum worker permanece concorrendo com a medição.

Primeira rodada: 5 repetições, mínimo 0,1 s e warmup 0,05 s, interleaving
aleatório. Houve CV de CPU de aproximadamente 21% em uma combinação. Segunda
rodada, sem builds concorrentes: 7 repetições, mínimo 0,3 s e warmup 0,1 s:

```bash
./build-tests-stressbots-audit/src/benchs/bench_spectators \
  --benchmark_filter=bench_addSpectators_snapshots \
  --benchmark_min_time=0.3s --benchmark_min_warmup_time=0.1 \
  --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=build-tests-stressbots-audit/spectator-merge-repeat.json \
  --benchmark_out_format=json
```

Medianas de CPU da segunda rodada, em ns. Refere-se ao tamanho de **cada**
snapshot, não ao número de bots. CV máximo é o maior CV de CPU das duas variantes.

| Refs/snapshot | Sobreposição | Cópia (ns) | Consumo (ns) | Redução da mediana | CV máximo |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 0% | 269,11 | 232,44 | 13,63% | 7,25% |
| 5 | 50% | 267,44 | 227,49 | 14,94% | 13,90% |
| 5 | 90% | 260,75 | 215,41 | 17,39% | 3,89% |
| 50 | 0% | 2672,65 | 2204,48 | 17,52% | 13,34% |
| 50 | 50% | 2578,23 | 2126,40 | 17,52% | 7,42% |
| 50 | 90% | 2508,84 | 2022,23 | 19,40% | 6,51% |
| 200 | 0% | 10992,65 | 9040,29 | 17,76% | 4,44% |
| 200 | 50% | 10986,44 | 8805,02 | 19,86% | 6,95% |
| 200 | 90% | 10258,27 | 8423,57 | 17,89% | 5,57% |

**Limitação importante:** o TFS/benchmark foi compilado em Release/LTO, mas a
biblioteca Google Benchmark 1.8.3 instalada no SO informa build **DEBUG** e emite
warning sobre os timings. Há variação de até 13,90% na segunda rodada. As duas
rodadas tiveram medianas menores com consumo nos nove casos, mas não foram
calculados intervalos de confiança nem comprovada significância estatística.
É evidência local limitada, não um percentual de ganho do servidor, nem prova de
capacidade para 1.000 jogadores. A remoção das cópias de owners descartáveis é
também verificável no código; o custo total do servidor permanece por medir.

Resultados brutos e console das duas rodadas estão em `spectator-merge.json`,
`spectator-merge-repeat.json`, `microbenchmark.log` e `microbenchmark-repeat.log`
dentro de `build-tests-stressbots-audit` (ignorados pelo Git).

## Sanitizers e memória

Usar um diretório separado e não comparar seus timings aos de Release:

```bash
cmake -S . -B build-asan-stressbots-audit -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DENABLE_ASAN=ON \
  -DENABLE_UNITY_BUILD=ON -DSKIP_GIT=ON -DUSE_MIMALLOC=OFF \
  -DCMAKE_CXX_FLAGS=-fsanitize=undefined \
  -DLUA_INCLUDE_DIR=/usr/local/include -DLUA_LIBRARY=/usr/local/lib/liblua.a \
  -Dmio_DIR=/home/mateus/.local/share/cmake/mio \
  -Dsimdutf_DIR=/home/mateus/.local/lib/cmake/simdutf
cmake --build build-asan-stressbots-audit --target \
  test_spectators test_lua_userdata_gc test_item_lifetime test_condition_shared_lifetime -j1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan-stressbots-audit --output-on-failure \
  --timeout 120 --output-log asan-tests.log \
  -R '^test_(spectators|lua_userdata_gc|item_lifetime|condition_shared_lifetime)$'
```

Bibliotecas externas não são recompiladas automaticamente com instrumentação.
TSan é uma execução separada (`ENABLE_TSAN=ON`, ASan desligado); não foi usado
como requisito de uma mudança exclusivamente síncrona no game thread.

## Replay real — PENDENTE DE TESTE REAL

Não iniciar servidor usando mapas/banco de produção. Preparar ambiente privado,
contas de teste e cópias do datapack/banco; verificar listeners, IP e portas antes
de conectar bots. Conferir maxPlayers, limites por IP e packets/s apenas nessa
configuração isolada. Não reduzir proteções na configuração de produção.

1. Compilar baseline e patch com a mesma toolchain/flags/dependências.
   Para profiling por linha, usar um build separado `RelWithDebInfo` com
   `-DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O2 -g -fno-omit-frame-pointer'`; esse build
   de profiling não foi executado aqui e não deve ser comparado ao Debug/ASan.
2. Registrar CPU, RAM, commit, config, mapa e sua hash, DB snapshot, versão/script
   dos bots, taxa de passos, ataques e spells. Manter as mesmas ações/s.
3. Fazer rampa 100, 250, 500, 1.000; aquecer antes da janela de medição. Repetir
   três vezes idle, movimento disperso, movimento concentrado e hunt/spells.
4. Ativar temporariamente `performanceMetricsEnabled = true` e
   `slowTaskWarning = true` na configuração privada. Não alterar os budgets.
5. Coletar `[Perf]`, métricas de bots/packets, RSS/heap e CPU por thread. Repetir
   também sem telemetria para quantificar o overhead.
6. Fazer ciclos login/logout, desconexão abrupta, shutdown com tasks pendentes,
   GC Lua, death/respawn e cliente com leitura lenta.

```bash
# Substituir pelo PID do servidor privado, não um PID de produção.
ps -L -p <PID> -o pid,tid,psr,pcpu,stat,comm
pidstat -t -u -r -p <PID> 1 30
perf record -F 99 -g -p <PID> -- sleep 60
perf report --stdio --no-children
```

O wrapper `tools/run-flamegraph.sh` pode iniciar um servidor se não encontrar um;
não chamá-lo indiscriminadamente contra o checkout com mapas do usuário.

| Cenário | Bots online | Ações/s | CPU game thread | CPU total | RSS | Queue max | Reactor p95/p99 | MoveCreature total | GetSpectators total | Drops/crash/ghost |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Baseline — idle | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Patch — idle | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Baseline — movimento disperso | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Patch — movimento disperso | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Baseline — movimento concentrado | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Patch — movimento concentrado | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Baseline — hunt/spells | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |
| Patch — hunt/spells | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE | PENDENTE |

Não somar métricas aninhadas como se fossem tempo exclusivo de CPU. Percentis
internos são estimativas por buckets. Enviar logs completos `[Perf]`, ações/s,
distribuição espacial, mapa/config e identificação de cada repetição para
escolher o próximo hotspot sem prometer capacidade ou ganho percentual fictício.
