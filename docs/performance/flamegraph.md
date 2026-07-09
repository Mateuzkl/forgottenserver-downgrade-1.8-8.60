# Profiling com Linux perf e FlameGraph

Este guia mostra como medir CPU e stacks do TFS com `perf` e gerar SVGs com
Brendan Gregg FlameGraph. Use isto para comparar antes/depois em caminhos
quentes como `Map::getSpectators`, `Map::getSpectatorsInternal`,
`Map::moveCreature`, `SpectatorVec::addSpectators`, `SpectatorVec::partitionByType`,
`std::shared_ptr`, `std::sort`, `std::unique` e, em branches antigas,
`boost::container::flat_set`.

## O que cada ferramenta faz

- `perf record` coleta amostras de CPU e call stacks do processo.
- `perf script` converte `perf.data` para texto.
- `stackcollapse-perf.pl` converte o texto do perf para folded stacks.
- `flamegraph.pl` gera o SVG final.

## Instalar dependencias

No Ubuntu ou WSL:

```bash
sudo apt update
sudo apt install -y git perl linux-tools-common linux-tools-generic linux-tools-$(uname -r)
```

Em alguns kernels de WSL, `linux-tools-$(uname -r)` pode nao existir no apt.
Nesse caso, instale `linux-tools-common` e `linux-tools-generic`, confira se
`perf` ficou disponivel, e use uma distro/kernel com suporte a perf se ainda
faltar.

Instale ou atualize o FlameGraph:

```bash
git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
```

Se a pasta ja existir:

```bash
git -C ~/FlameGraph pull --ff-only
```

Tambem ha um helper no repo:

```bash
./scripts/profiling/setup_flamegraph.sh
```

## Compilar com simbolos

Para flamegraphs uteis, compile com simbolos e frame pointers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -fno-omit-frame-pointer"

cmake --build build -j$(nproc)
```

Se voce usar o build helper do projeto, confirme que o binario foi compilado
com `-g` e sem omitir frame pointer quando for fazer comparacao fina.

## Capturar usando PID

Descubra o PID:

```bash
pidof crystalserver
```

Capture por 60 segundos:

```bash
sudo perf record -F 99 -p $(pidof crystalserver) -g -- sleep 60
```

Gere os arquivos intermediarios e o SVG:

```bash
mkdir -p profiling-output
sudo perf script > profiling-output/tfs-before.perf

~/FlameGraph/stackcollapse-perf.pl profiling-output/tfs-before.perf \
  > profiling-output/tfs-before.folded

~/FlameGraph/flamegraph.pl --title="TFS before getSpectators" \
  profiling-output/tfs-before.folded > profiling-output/tfs-before.svg
```

## Captura com script

Captura completa por nome de processo:

```bash
./scripts/profiling/capture_flamegraph.sh \
  --process crystalserver \
  --duration 60 \
  --title "TFS before getSpectators" \
  --output profiling-output/tfs-before.svg
```

Captura por PID:

```bash
./scripts/profiling/capture_flamegraph.sh \
  --pid 12345 \
  --duration 60 \
  --title "TFS after getSpectators" \
  --output profiling-output/tfs-after.svg
```

## Flamegraph filtrado para spectators

Depois de uma captura, voce pode gerar um SVG somente com stacks relevantes:

```bash
grep -Ei "getSpectators|moveCreature|SpectatorVec|shared_ptr|sort|unique|flat_set" \
  profiling-output/tfs-before.folded \
  | ~/FlameGraph/flamegraph.pl --title="TFS spectators only" \
  > profiling-output/spectators-only.svg
```

Ou use o script diretamente:

```bash
./scripts/profiling/capture_flamegraph.sh \
  --process crystalserver \
  --duration 60 \
  --title "TFS spectators only" \
  --output profiling-output/spectators-only.svg \
  --filter "getSpectators|moveCreature|SpectatorVec|shared_ptr|sort|unique|flat_set"
```

## Comparar before/after

1. Compile o branch base com simbolos.
2. Rode um cenario reproduzivel: muitos monstros andando, spells em area,
   teleportes e players se movendo perto de muitos spectators.
3. Gere `profiling-output/tfs-before.svg`.
4. Troque para o branch otimizado, recompile do mesmo jeito.
5. Rode o mesmo cenario e gere `profiling-output/tfs-after.svg`.
6. Compare visualmente a largura dos frames.

Funcoes para procurar no SVG:

- `Map::getSpectators`
- `Map::getSpectatorsInternal`
- `Map::moveCreature`
- `SpectatorVec::addSpectators`
- `SpectatorVec::partitionByType`
- `std::shared_ptr`
- `std::sort`
- `std::unique`
- `boost::container::flat_set`

Se `std::sort` ou `std::unique` aparecerem maiores que o custo removido, a
otimizacao deve ser reavaliada.

## Permissao do perf

Se o `perf` falhar com erro de permissao:

```bash
sudo sysctl kernel.perf_event_paranoid=1
sudo sysctl kernel.kptr_restrict=0
```

Essas alteracoes duram ate reiniciar. Para persistir, configure em
`/etc/sysctl.d/`, mas faca isso so em ambiente de desenvolvimento.

## Arquivos gerados

Os arquivos de profiling devem ficar fora do git:

- `profiling-output/`
- `perf.data`
- `perf.data.*`
- `*.perf`
- `*.folded`
- `*.collapsed`

Nao commite SVGs de teste, logs grandes, caches ou binarios de build.
