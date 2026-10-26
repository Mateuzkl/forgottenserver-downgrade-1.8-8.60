# PR 337 — correções complementares, 26/10/2026

Revisão da branch `perf/tfs18-audit-combat-network-20261021`, começando em
`49e791f51f3713ec2d257e2407480e80a560cca9`.

PR: https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/pull/337

O PR já continha correções de callbacks, espectadores e ownership de membros
de party. Esta rodada completa os casos de reentrada/lifetime encontrados na
revisão e corrige a concorrência de configurações escalares. Não incorpora os
sistemas DBO nem o trabalho posterior de autosend/métricas de rede.

## Falhas reproduzidas nesta branch antes das correções

1. **Party destruída acessada por um alias Lua.** Depois de dissolver o grupo e
   liberar seus donos, um segundo userdata ainda chamava `Party::getLeader()`.
   ASan confirmou `heap-use-after-free` em `luaPartyGetLeader`.
2. **Bônus perdido ao converter ganho de mana em dreno.** O callback recebia
   ganho de 100 e retornava dreno de 100. Com monstro de nível 2 e bônus de 10%
   por nível, o resultado esperado era -120; a flag antecipada impedia o bônus.
3. **Data race no ConfigManager.** Um leitor e um escritor concorrentes das APIs
   reais de configuração produziram diagnóstico do ThreadSanitizer na array
   `integers`, entre `getInteger` e `setInteger`. Esse é o mesmo armazenamento
   envolvido no alerta anteriormente registrado para `test_runtime_stats_gate`.

Os testes de reprodução foram executados contra o código-fonte ainda sem estes
fixes. Os logs originais foram preservados localmente.

## Alterações

| Área | Comportamento corrigido |
|---|---|
| Userdata Lua de party | Token fraco próprio valida a vida do objeto antes do acesso. Cada binding retém um dono forte durante a chamada; aliases expirados retornam `nil`. |
| GC e metatable | Finalizador do token é idempotente. O helper de metatable resolve o índice absoluto antes de alterar a pilha Lua, preservando também o caminho de Tile. |
| Dissolução de party | Retorna o resultado real. Veto preserva o userdata e o grupo; proteção de reentrada evita dissolução recursiva. |
| Outras operações de party | Ownership retido durante callbacks. A operação para se o callback dissolver o grupo. |
| Convites pelo Game | A party permanece ancorada em `onInvite`; líder, participantes e vínculos são revalidados depois do callback. |
| XP compartilhada | Snapshot estável de membros/líder evita duplicar/perder cotas quando um callback troca a liderança. Entrega para ao dissolver o grupo e confere a elegibilidade atual de cada destinatário. |
| Bônus de combate | Flags de reset, nível de monstro e Avatar são consumidas quando o modificador efetivamente se aplica. Bônus ativados durante callback e ganho convertido em dreno funcionam uma vez. |
| Cura | A branch `COMBAT_HEALING` passa a enviar o texto correto aos observadores, além dos ramos legados já corrigidos pelo PR. |
| Proximidade de NPCs/monstros | Consultas ampliadas de espectadores são filtradas pelas regras nativas de proximidade e instância, preservando alcance horizontal e distinguindo atividade de conversa. |
| Configurações escalares | Arrays de inteiros, booleanos e floats tornam-se atômicas. Getters/setters usam acesso relaxado para evitar a corrida sem mutex em cada consulta de configuração. |

Os recursos já existentes da branch foram preservados, inclusive cache de preços
de itens de NPC, modificador de reset por spell, Prey, imunidades e negociação
do protocolo. Não houve troca dos arquivos inteiros de Game/NPC por versões DBO.
Os arquivos de party equivalentes foram comparados antes de portar seus fixes.

### Limite da correção de configuração

Os escalares são opções independentes; o acesso relaxado não publica outros
objetos e não torna uma atualização de várias opções uma transação atômica.
Strings, containers e tabelas de stages mantêm seus contratos anteriores.
Esta mudança não prova que toda recarga de configuração ou todo o motor é
livre de races.

## Regressões

- Party: aliases expirados, veto, dissolução recursiva, dissolução dentro de
  `onLeave`/`onInvite`, liderança alterada durante XP e interrupção da entrega
  quando o grupo é dissolvido.
- Metatable com índice absoluto e preservação da pilha; finalizador invocado
  explicitamente duas vezes e depois pelo GC; 1.000 ciclos de dissolução + GC.
- Combate: callback sem alteração, override de dano, bônus ativado durante
  callback, mana convertida em dreno e reutilização legítima de cura.
- Observadores: texto de `COMBAT_HEALING`, mana shield e mana drain, com a
  ordem de espectadores invertida, fonte nula, autocura e filtro de instância.
- Proximidade: andares e instâncias de NPCs/monstros e preservação dos alcances
  horizontais existentes.
- ConfigManager: leitura/escrita simultânea de inteiros, booleanos e floats,
  reutilizando as APIs de produção e restaurando os valores ao terminar.
- Compatibilidade do contador de alocações com o TSan do Clang, com um controle
  positivo que confirma que alocações reais continuam sendo observadas.

As asserções da suíte existente foram mantidas. Os testes que exercitam métodos
Lua registram as criaturas no ownership do jogo, como ocorre em produção.

## Validação desta rodada

- **Antes:** reproduções de ASan, dreno de mana e ThreadSanitizer descritas acima.
- **Depois, probe TSan de configurações:** aprovado, sem diagnóstico. O probe
  compila os arquivos reais `configmanager.cpp` e `logger.cpp` com o novo teste;
  não é um mock do armazenamento.
- **Build completo + suíte ASan/UBSan:** servidor e executáveis compilados;
  **65/65 alvos CTest aprovados, zero falhas, 43,91 s** na execução final.
  Sem diagnósticos dos sanitizers no conjunto exercitado. O teste SQL opcional
  executou seu caminho de skip; o round trip com banco não foi validado.
- **TSan com engine e `test_runtime_stats_gate`/`test_configmanager_concurrency`:**
  **2/2 alvos aprovados, zero falhas, 1,45 s**. O primeiro contém 14 casos e o
  segundo um caso. Sem diagnóstico de corrida nessa seleção. Não é uma execução
  da suíte inteira com TSan.
- `test_party_lifetime`: 12 casos, incluindo 1.000 ciclos de dissolução + GC.
- `test_combat_reentry`: nove casos, incluindo bônus ativados durante callbacks
  e ganho de mana transformado em dreno.

A primeira execução da suíte apontou um `Events` ausente na fixture nova do
teste de Avatar; o teste agora inicializa e restaura o hook real da engine.
Nenhum guard de produção foi desativado para contornar o problema.

O teste de estatísticas substituía os operadores globais de alocação, o que
colidia com as definições fortes do runtime TSan do Clang. Nesse modo o contador
agora usa o hook público
[`__sanitizer_malloc_hook` do LLVM 18](https://github.com/llvm/llvm-project/blob/release/18.x/compiler-rt/include/sanitizer/allocator_interface.h).
O hook só atualiza contadores locais à thread e não é instrumentado para evitar
reentrada no runtime. A engine e o restante do teste continuam instrumentados,
as asserções de alocação foram mantidas e há um controle positivo do contador.

Ambiente: Ubuntu 24.04 no WSL, Clang 18, C++23, Lua 5.5 instalada localmente.
Build ASan/UBSan Debug `-O0 -g1`, sem unity/LTO/native optimizations, com
`detect_leaks=1` e `halt_on_error=1`. Build TSan separado usa unity com lotes
de oito arquivos; ASan e TSan não são combinados.

A compilação Windows permanece com o proprietário. Não houve reinicialização
do servidor em uso nem teste de carga contra ele. Não há medição que permita
declarar redução de CPU ou capacidade para determinada quantidade de jogadores.

### Repetir nos builds locais configurados

```sh
ASAN_BUILD_DIR="${ASAN_BUILD_DIR:-$HOME/tfs18-pr337-review-asan}"
TSAN_BUILD_DIR="${TSAN_BUILD_DIR:-$HOME/tfs18-pr337-review-tsan}"

cmake --build "$ASAN_BUILD_DIR" --parallel 3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir "$ASAN_BUILD_DIR" \
  --output-on-failure --timeout 120 -j 1

cmake --build "$TSAN_BUILD_DIR" \
  --target test_runtime_stats_gate test_configmanager_concurrency --parallel 1
TSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir "$TSAN_BUILD_DIR" \
  --output-on-failure --timeout 180 \
  -R 'test_(runtime_stats_gate|configmanager_concurrency)$'
```

### Evidências locais

Em `%TEMP%` do Windows:

- `tfs18-pr337-review-repro-tests.log`: UAF e falha de mana antes dos fixes.
- `tfs18-pr337-tsan-baseline.log`: corrida antes do acesso atômico.
- `tfs18-pr337-tsan-fixed.log`: probe depois do fix.
- `tfs18-pr337-review-final-build.log` e `tfs18-pr337-review-final-tests.log`:
  compilação e suíte ASan/UBSan finais.
- `tfs18-pr337-full-tsan-build.log` e `tfs18-pr337-full-tsan-tests.log`:
  compilação e testes da engine instrumentada com TSan.

## Rodada de revisão e integração com main

Rodada seguinte ao commit `5317383`, incorporando `main` em
`432f8afefae91cdd0bb8a3167e11ea0b0f1ae62b`. O único conflito textual estava em
`src/protocolgame.h`: foram preservados tanto `ProtocolGameAuditTestAccess`
quanto `ProtocolGameMovementTestAccess`. As mudanças aprovadas de movimento/
stack e login por e-mail foram mantidas.

### Comentários verificados

| Achado | Tratamento |
|---|---|
| CodeRabbit: ccache obrigatório nos scripts | Os builds candidate e Clang detectam a ferramenta; sem ela, limpam o launcher em vez de depender de um executável ausente ou de um valor antigo no cache. |
| CodeRabbit: compatibilidade de unpack/pack | Bootstrap conserva `unpack` existente e fornece fallback de `table.pack`. Testes cobrem contagem de argumentos com nil e as recusas dos callbacks. |
| CodeRabbit: disband durante transferência forçada de liderança | Reproduzido retorno indevido de sucesso em `leaveParty`. Após `passPartyLeadership`, a função agora verifica `disbanding` e para antes de emitir sucesso/continuar a limpeza. |
| Greptile: fixtures incompatíveis com baseline | O accessor de combate reutiliza o friend já existente através de alias exclusivo do teste. Party usa uma fixture específica compatível com `void disband()` e sem acesso privado a Events. Headers/layout de produção do baseline não são alterados. |
| CodeRabbit: comandos com caminho pessoal | Os comandos de reprodução usam `ASAN_BUILD_DIR` e `TSAN_BUILD_DIR`, configuráveis, com defaults em `$HOME`. |
| CodeRabbit: redação do relatório | A referência ao estado anterior foi esclarecida como “código-fonte”. |
| Aviso da configuração CodeRabbit | Removidos os blocos raiz `build` e `tools`, que o bot já ignorava por não serem reconhecidos. A configuração válida de idioma foi preservada. |
| Comentário de espectadores já resolvido | Mantidas as correções do commit anterior; não se acrescentou outra filtragem ao mesmo caminho. |

Referências de compatibilidade:
[LuaJIT — extensões](https://luajit.org/extensions.html) e
[configuração CodeRabbit](https://docs.coderabbit.ai/reference/configuration).

### Validação específica dos scripts e LuaJIT

- Quatro fontes de regressão (mensagens, mapa, reentrada e a fixture de party)
  passaram pela compilação sintática estrita com os headers originais de
  `49db3276`. As verificações de hash confirmaram que `game.h`, `party.h` e
  `events.h` do baseline permaneceram intactos.
- Essa verificação não recompilou nem executou a biblioteca antiga do baseline.
  O script continua exigindo seu build imutável para executar os testes vermelhos.
- `bash -n` aprovado nos três scripts alterados.
- Harness de argumentos com comandos simulados validou seis combinações:
  candidate Release, candidate ASan e Clang, cada um com e sem ccache. As flags
  de ASan também foram conferidas. Isso valida a seleção do launcher; não é um
  novo benchmark ou seis builds completos da engine.
- **LuaJIT 2.1.1703358377 real: quatro casos aprovados**, com `table.unpack` e
  `table.pack` ausentes na biblioteca original. O teste foi compilado e ligado
  diretamente contra a biblioteca LuaJIT instalada.

### Validação integrada após o merge

A primeira suíte encontrou a nova fixture de movimento executando senders fora
do dispatcher. O runner desse teste agora usa o dispatcher/reactor reais,
mantendo a asserção de afinidade e as conferências de bytes/stack da produção.

- Suíte ASan/UBSan completa após esse ajuste: **66/66 alvos CTest aprovados,
  zero falhas, 40,01 s**. Servidor e executáveis de teste compilados; sem
  diagnósticos dos sanitizers no conjunto exercitado. O cenário SQL opcional
  continua sem execução do round trip com banco.
- Engine instrumentada com TSan: **2/2 alvos aprovados, 1,31 s**, incluindo
  `test_runtime_stats_gate` e `test_configmanager_concurrency`.

Logs locais da rodada em `%TEMP%`: `tfs18-pr337-review2-repro-test.log`,
`tfs18-pr337-review2-probes.log`, `tfs18-pr337-review2-final-build.log`,
`tfs18-pr337-review2-final-tests.log` e `tfs18-pr337-review2-tsan-tests.log`.
