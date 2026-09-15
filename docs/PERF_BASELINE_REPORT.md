# perf/profile-baseline — relatório

Branch: `perf/profile-baseline` · base: `main` @ `63b758a7` · referência: [Black-Tek/BlackTek-Server](https://github.com/Black-Tek/BlackTek-Server) @ `61d23b5`

Escopo desta branch, conforme o plano: **descobrir onde o CPU está antes de portar qualquer linha.**
Nenhuma mudança de comportamento. Toda instrumentação adicionada é opcional e compilada fora por padrão.

---

## Ambiente da medição

| | |
|---|---|
| CPU | Intel Core i5-10300H @ 2.50 GHz, 4 cores / 8 threads |
| ISA | SSE2, SSE4.2, **AVX2** — sem AVX512 |
| RAM | 7.7 GiB |
| OS | Ubuntu 24.04.4 LTS (WSL2) |
| Compilador | g++ 13.3.0 |
| CMake | 3.28.3 |
| Build | Release |

> A tabela do plano foi medida num **Xeon @ 2.80 GHz com AVX512F**. Esta máquina não tem AVX512.
> Os números abaixo foram medidos aqui e **divergem do plano em pontos importantes** — está sinalizado onde.

---

## 1. As flags de Release são as esperadas?

**Sim, e com um extra que o plano não mencionava.**

`CMakeCache.txt` mostra apenas o valor que o usuário passou:

```text
CMAKE_BUILD_TYPE:STRING=Release
CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG
ENABLE_NATIVE_OPTIMIZATIONS:BOOL=ON
```

A linha de compilação real, lida de `build-release/src/CMakeFiles/tfslib.dir/flags.make`:

```text
-O3 -fomit-frame-pointer -DNDEBUG -march=native -mtune=native -std=c++23
-flto=auto -fno-fat-lto-objects -Wall -Wextra ... -fno-strict-aliasing
```

Confirmado: `-O3 -march=native -mtune=native`. **Mais `-flto=auto -fno-fat-lto-objects`**, que o plano
não citava e que muda como se verifica qualquer coisa neste projeto — ver o item 2.

**Ponto em aberto para você responder:** o binário de produção é compilado na mesma máquina onde roda?
Com `-march=native` isso não é detalhe — é a pré-condição (A) da branch 5. Se o binário for distribuído
para outra máquina, `-march=native` não pode ser usado e a conclusão sobre SIMD XTEA muda.

---

## 2. O GCC vetoriza o XTEA?

**A verificação proposta no plano não funciona neste projeto, e o motivo importa.**

O plano manda rodar:

```bash
g++ -O3 -march=native -fopt-info-vec-optimized -c src/xtea.cpp -o /dev/null
```

Rodando exatamente isso, a saída vem vazia — parece "não vetorizou". **É um falso negativo.**
O projeto compila com `-flto=auto -fno-fat-lto-objects`: o `.o` contém só bytecode GIMPLE, nenhuma
instrução de máquina. Comprovado:

```text
compile rc=0
ymm (AVX2 256-bit) no TU : 0
xmm (SSE  128-bit) no TU : 0
total de instruções      : 0     <- objeto sem código, geração adiada para o link
```

A geração de código — e portanto a vetorização — acontece **no link**. E como o projeto usa
`-fvisibility=hidden` + LTO, `xtea::encrypt` nem existe como símbolo no binário final:

```text
nm -C build-release/tfs | grep xtea::   ->  (vazio, inlinado pelo LTO)
```

Isolando o mesmo laço num TU sem `otpch.h`, o GCC **vetoriza**:

```text
/tmp/xtea_iso.cpp:9:49: optimized: loop vectorized using 32 byte vectors
/tmp/xtea_iso.cpp:9:49: optimized: loop versioned for vectorization because of possible aliasing
/tmp/xtea_iso.cpp:9:49: optimized: loop vectorized using 16 byte vectors
```

32 bytes = AVX2. **O Fato 1 do plano está correto na conclusão** (o laço atual vetoriza sozinho),
mas o método de verificação proposto não serve aqui. Para responder isso neste projeto é preciso
medir, não inspecionar — que é o que `benchmarks/perf_baseline/` passa a fazer.

---

## 3. Onde XTEA e Adler32 são realmente chamados

```text
src/protocol.cpp:24   xtea::encrypt(buffer, msg.getLength(), key)        <- todo pacote de saída
src/protocol.cpp:34   xtea::decrypt(buffer, msg.getLength() - 6, key)    <- todo pacote de entrada

src/connection.cpp:282    adlerChecksum(...)
src/outputmessage.h:34    adlerChecksum(...)
src/protocolgame.cpp:1317 adlerChecksum(..., 8)                          <- sempre 8 bytes
```

Detalhe relevante para a branch 5: `XTEA_encrypt` faz padding para múltiplo de 8 **antes** de cifrar,
então o comprimento entregue ao kernel é sempre múltiplo de 8. E `protocolgame.cpp:1317` chama o
Adler com **8 bytes fixos** — exatamente o tamanho onde a zlib **perde** para o código atual.

---

## 4. Verificação dos fatos do plano contra o código

| Fato | Afirmação do plano | Medido aqui | Veredito |
|---|---|---|---|
| 1 | XTEA atual já vetoriza | vetoriza (32-byte vectors), mas só verificável por medição por causa do LTO | **confirmado, método corrigido** |
| 3 | `detect()` cai em Scalar no GCC/Clang | `simd_dispatch.h:62` → `#else g_level = Level::Scalar;` | **confirmado** |
| 4 | Adler32 zlib ~1,4x | 1,37–1,44x acima de 64 B; **perde** abaixo de 32 B | **confirmado** |
| 5 | Pathfinding já é o mesmo código | `FIB_MULT = 2654435761u`, `MAX_NODES = 512`, `ASTAR_HASH_BITS = 10u` idênticos nos dois | **confirmado** |
| 6 | Target strategy do TFS é superior | TFS tem `case TARGETSEARCH_HEALTH:` (monster.cpp:1166) e `case TARGETSEARCH_DAMAGE:` (1198); BlackTek só trata `NEAREST` (monster.cpp:655) | **confirmado** |
| 7 | `getSpectators` é o caminho quente | **103** call sites no TFS (112 com testes), **63** no BlackTek | **confirmado** (plano dizia 107/60) |

Estruturas que o TFS **já tem** e que não devem ser reimplementadas:

```text
QTree + cachedLeaf            src/map.cpp        (5 ocorrências)
SpectatorVec::partitionByType src/spectators.h
thread_local AStarWorkspace   src/map.cpp        (13 ocorrências)
```

---

## 5. Adler32 — medido

`-O3 -march=native`, melhor de 5, ns por chamada. Hash **byte-idêntico** em 14 tamanhos × 4 padrões
(zeros, 0xFF, sequencial, aleatório), incluindo o caso `> NETWORKMESSAGE_MAXSIZE` que retorna 0.

| bytes | manual (atual) | zlib `adler32_z` | speedup | vencedor |
|------:|---------------:|-----------------:|--------:|----------|
| 1 | **2,0** | 3,0 | 0,68x | atual |
| 8 | **4,3** | 5,6 | 0,77x | atual |
| 16 | **7,4** | 8,3 | 0,89x | atual |
| 32 | 13,3 | **12,3** | 1,08x | zlib |
| 64 | 25,0 | **20,4** | 1,23x | zlib |
| 128 | 53,2 | **37,2** | 1,43x | zlib |
| 256 | 99,5 | **70,7** | 1,41x | zlib |
| 512 | 191,9 | **137,1** | 1,40x | zlib |
| 1024 | 380,6 | **271,3** | 1,40x | zlib |
| 2048 | 759,5 | **546,2** | 1,39x | zlib |
| 4096 | 1511,2 | **1109,4** | 1,36x | zlib |
| 24590 | 9407,5 | **6730,2** | 1,40x | zlib |

O ponto de virada fica entre **16 e 32 bytes**, batendo com o plano. E a chamada de
`protocolgame.cpp:1317` é de 8 bytes fixos — essa especificamente **ficaria mais lenta** com zlib.
A branch 1 precisa tratar isso, não trocar tudo cegamente.

zlib já é dependência obrigatória (`CMakeLists.txt:84 find_package(ZLIB REQUIRED)`, linkada na
linha 333, declarada em `vcpkg.json`). Portar não adiciona dependência.

---

## 6. XTEA — medido

`-O3 -march=native`, melhor de 5, ns por chamada, máquina ociosa. As 4 implementações produzem
**cifra idêntica** em 14 tamanhos e `decrypt(encrypt(x)) == x` — o harness recusa reportar tempo se a
equivalência falhar.

| bytes | tfs (atual) | bt-scalar | bt-sse2 | bt-avx2 | vencedor | passa o gate de 20%? |
|------:|------------:|----------:|--------:|--------:|----------|----|
| 8 | 96,4 | **74,7** | 75,7 | 75,6 | bt-scalar 1,29x | sim |
| 16 | **106,2** | 136,0 | 135,5 | 135,8 | atual | não |
| 24 | **153,0** | 220,5 | 221,8 | 225,1 | atual | não |
| 32 | 157,5 | 299,2 | 89,2 | **88,1** | bt-avx2 1,79x | **sim** |
| 40 | 160,3 | 393,2 | 160,3 | **156,5** | bt-avx2 1,02x | não |
| 48 | **174,9** | 448,5 | 225,4 | 232,8 | atual | não |
| 56 | **215,6** | 523,0 | 300,8 | 297,2 | atual | não |
| 64 | 206,6 | 611,7 | 166,5 | **85,4** | bt-avx2 2,42x | **sim** |
| 128 | 253,7 | 1187,0 | 332,2 | **150,5** | bt-avx2 1,69x | **sim** |
| 256 | **303,1** | 2297,9 | 644,0 | 305,0 | atual | não |
| 512 | 643,1 | 4599,9 | 1300,8 | **605,8** | bt-avx2 1,06x | não |
| 1024 | **1109,2** | 9281,5 | 2586,1 | 1234,2 | atual | não |
| 4096 | 4980,3 | 37098,0 | 10397,9 | **4971,6** | bt-avx2 1,00x | não |
| 16384 | 20650,3 | 149548,0 | 41247,2 | **19972,2** | bt-avx2 1,03x | não |

### O que esta tabela diz

**1. O caminho scalar do BlackTek é uma regressão brutal, e é o caminho que ele realmente toma no Linux.**
`simd_dispatch.h:62` força `g_level = Level::Scalar` fora de MSVC/ICC. Portando como está, o servidor
roda a coluna `bt-scalar`: **2,9x mais lento** em 64 B, **7,2x** em 16 KB. O plano estimava 1,75x;
aqui é muito pior. Se algo for portado, o fallback scalar **tem que continuar sendo o laço atual do TFS**.

**2. O vencedor troca com o tamanho, e não há resposta única.** O AVX2 ganha forte em 32/64/128 B
(1,79x / 2,42x / 1,69x) e empata ou perde de 256 B para cima. O atual ganha em 16, 24, 48, 56, 256 e 1024.

**3. Isso diverge do plano.** A tabela do documento (Xeon com AVX512) dava o atual vencendo de 256 B
para cima por 16–32%. Aqui de 512 B para cima o AVX2 ganha por 3–6% — margem irrelevante, mas o sinal
inverteu. Confirma a tese central do documento: **a conclusão depende da máquina e das flags**, então
cada servidor precisa rodar `benchmarks/perf_baseline/run.sh` no próprio hardware.

**4. O gate de 20% só é batido em 32, 64 e 128 bytes.** Portanto:

> **A branch 5 continua CONDICIONAL e não pode ser decidida ainda.** Se a distribuição real de tamanho
> de pacote se concentrar em 32–128 B, o port ganha de 1,7x a 2,4x e vale. Se se concentrar em 256 B ou
> mais, o ganho é ≤6% e o port é **REJEITADO** pelo gate. Sem o histograma da seção 8, qualquer decisão
> aqui é chute.

**5. Pré-condição (A) — metade resolvida pelo `--sweep`.** Em `-O3` puro (sem `-march=native`, que é o
que se usa num binário distribuído), o AVX2 do BlackTek ganha em **praticamente todo tamanho**:

| bytes | tfs @ -O3 | bt-avx2 @ -O3 | ganho |
|------:|----------:|--------------:|------:|
| 64 | 164,7 | **81,8** | 2,01x |
| 128 | 242,4 | **152,1** | 1,59x |
| 256 | 451,8 | **306,6** | 1,47x |
| 512 | 872,4 | **613,6** | 1,42x |
| 1024 | 1925,5 | **1217,6** | 1,58x |
| 4096 | 7102,0 | **4889,7** | 1,45x |
| 16384 | 27879,0 | **19542,1** | 1,43x |

Todos passam o gate de 20% com folga. **Se o binário for distribuído para terceiros, o port vale.**
Se for compilado na mesma máquina onde roda, ver o item 6. Ainda preciso da sua resposta sobre qual
dos dois cenários é o real.

### AVISO — variância acima de 256 bytes invalida o gate com `-march=native`

Duas execuções do **mesmo commit, mesma máquina, mesmas flags de baseline**:

| bytes | tfs (execução 1) | tfs (execução 2) | variação | bt-avx2 (1) | bt-avx2 (2) | variação |
|------:|-----------------:|-----------------:|---------:|------------:|------------:|---------:|
| 256 | 305,1 | 380,0 | +25% | 307,3 | 315,0 | +3% |
| 512 | 652,3 | 738,3 | +13% | 607,3 | 618,7 | +2% |
| 1024 | 1103,8 | 1291,3 | +17% | 1209,4 | 1246,5 | +3% |
| 4096 | 4917,8 | 6580,6 | **+34%** | 4845,1 | 5004,0 | +3% |
| 16384 | 20575,3 | 26343,7 | +28% | 19689,7 | 22684,1 | +15% |

A coluna do código atual variou até **34%**; a do AVX2 ficou dentro de 3% na maior parte. A causa mais
provável é **thermal throttling** — a máquina é um i5-10300H de notebook e a segunda execução veio no
fim de um `--sweep` de quatro builds. O laço auto-vetorizado é mais sensível à queda de clock que o
kernel AVX2.

**Consequência:** de 256 bytes para cima, com `-march=native`, o gate de 20% cai **dentro da margem de
ruído**. Esses números não decidem nada. Para decidir seria preciso repetir num servidor dedicado, sem
throttling, com a máquina fria e repetições intercaladas.

### O que é estável em todas as 8 execuções

Independente de flags, ordem e temperatura:

```text
64 B     -> AVX2 ganha 2,01x a 2,66x     (sempre)
128 B    -> AVX2 ganha 1,42x a 1,69x     (sempre)
16/24/48/56 B -> o código atual ganha    (sempre)
bt-scalar-> perde em tudo acima de 8 B, até 7,2x   (sempre)
```

---

## 7. O que NÃO foi medido, e por quê

| Item | Status | Motivo |
|---|---|---|
| `perf record` com carga real | **não feito** | exige servidor de pé com MySQL e jogadores/bots. Não disponível nesta máquina. |
| % de CPU por função | **não obtido** | depende do item acima |
| Distribuição real de tamanho de pacote | **instrumentado, não coletado** | precisa de servidor sob carga — ver seção 8 |

### Build verificado

| Configuração | configure | build | instrumentação no binário | tamanho |
|---|---|---|---|---|
| Release padrão | rc=0 | rc=0 | **ausente** | 6.714.432 B |
| Release + `ENABLE_PACKET_SIZE_HISTOGRAM=ON` | rc=0 | rc=0 | presente | 6.725.048 B |

Diferença de 10.616 bytes, só no binário instrumentado. Com a opção desligada as macros expandem para
nada e o binário de produção não muda.

### Achado de infraestrutura (registrar para as branches seguintes)

Um `cmake -B build-novo -DCMAKE_BUILD_TYPE=Release` **limpo não configura** nesta máquina. Duas
dependências estão instaladas à mão e só são encontradas via cache de uma árvore já configurada:

```text
mio             -> /home/mateus/.local/share/cmake/mio     (precisa de CMAKE_PREFIX_PATH)
Lua 5.5         -> LUA_INCLUDE_DIR não é achado sozinho
simdutf         -> /home/mateus/.local/lib/cmake/simdutf
```

Reconfigurar a árvore existente (`cmake -B build-release -D<opção>`) funciona porque reaproveita o
cache. As branches seguintes devem contar com isso, ou o `build.sh` do projeto precisa documentar as
variáveis exigidas.

**Portanto a pergunta central do plano — "XTEA + Adler32 somam menos de 3% do CPU?" — continua sem
resposta.** Não vou inventar um número. O que esta branch entrega é o instrumental para você
responder, rodando o servidor de verdade.

---

## 8. Como coletar o que falta

### Distribuição de tamanho de pacote

```bash
cmake -B build-histogram -DCMAKE_BUILD_TYPE=Release -DENABLE_PACKET_SIZE_HISTOGRAM=ON
cmake --build build-histogram -j"$(nproc)"
./build-histogram/tfs
```

Reporta as duas direções no log a cada 100 mil pacotes de saída. Desligado por padrão: sem a
opção, as macros expandem para nada e o binário de produção não muda.

### Profile de CPU

```bash
perf record -F 999 -g --call-graph dwarf -- ./build-release/tfs
perf report --stdio --sort=dso,symbol | head -60
```

---

## 9. Arquivos desta branch

| Arquivo | O que é |
|---|---|
| `benchmarks/perf_baseline/bench_xtea.cpp` | compara TFS × BlackTek scalar/SSE2/AVX2, valida equivalência antes de medir |
| `benchmarks/perf_baseline/bench_adler.cpp` | manual × `adler32_z`, valida equivalência antes de medir |
| `benchmarks/perf_baseline/run.sh` | roda no baseline; `--sweep` mostra como as flags mudam a resposta |
| `benchmarks/perf_baseline/README.md` | como rodar e como ler |
| `src/packet_size_histogram.h/.cpp` | instrumentação opcional, compilada fora por padrão |
| `src/protocol.cpp` | duas macros nos call sites do XTEA (no-op sem a opção) |
| `CMakeLists.txt`, `src/CMakeLists.txt` | opção `ENABLE_PACKET_SIZE_HISTOGRAM` (OFF) e o novo .cpp |
| `docs/PERF_BASELINE_REPORT.md` | este relatório |
