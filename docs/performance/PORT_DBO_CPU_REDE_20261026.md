# Port das melhorias de CPU e rede do DBO — 26/10/2026

Destino: `TFS18-PR337-Review`, branch
`perf/tfs18-audit-combat-network-20261021`, partindo de
`f05aea4`. Origem dos trechos: commit DBO
`94a7a3db41fef00dfdfe5fb5b793642a5e0e04ee`.

## Alterações aplicadas

| Área | Mudança |
|---|---|
| Efeitos, projéteis, vida, textos de combate e textos animados | Escritores pequenos substituem a construção de um `NetworkMessage` de aproximadamente 65 KB para mensagens curtas. Os corpos continuam sendo copiados para um `OutputMessage` proprietário antes do retorno. |
| Codificação de texto | Preservados UTF-8 para Latin-1, fallback vazio para caracteres inválidos/não representáveis e caminho original para textos maiores que 256 bytes de entrada. |
| Autosend | Dois vetores reutilizáveis conservam a capacidade entre lotes. O `clear` libera as referências aos protocolos. A flag de agendamento acompanha o retorno real do scheduler, permitindo tentar novamente um timer recusado. |
| Imbuements | Removido o par `sendSkills`/`sendStats` de cada item em cada think sem alteração de bônus. O tracker de duração e as atualizações na expiração permanecem. |
| Caminhos de monstros | Reuso limitado a passos normais da própria rota. Deslocamento externo, teleporte, mudança de andar, embriaguez, caminho esgotado e atualização forçada solicitam novo caminho. |
| Market por passo | Sem timers quando o módulo está ausente ou a sessão está fechada. A callback confere também o GUID, evitando agir sobre um runtime ID reaproveitado. |
| Criaturas conhecidas | OTC conserva IDs ainda visíveis mesmo ao exceder o orçamento de poda de 1.300. O limite legado de 250 permanece. Outfit/passagem exigem um ID previamente anunciado. |
| Diagnóstico | Contadores opcionais por opcode/bytes, conclusões de escrita, pico da fila e rejeições. Reutilizam `performanceMetricsEnabled`; não há log por pacote. |

## Adaptações para esta branch

Este é um port de trechos, não uma substituição dos arquivos pela versão DBO.

- O reactor deste destino é mais recente: preserva tarefas críticas, limita a
  ingestão por ciclo, reaproveita buffers e mantém cancelamentos/ordenação.
  Sua implementação e seus testes foram preservados; o reactor antigo do DBO
  não foi importado.
- A branch já reutilizava caminhos e limitava recálculos após passos do alvo.
  Foram preservados `shouldRepathAfterTargetStep`, o endpoint da rota e a
  proteção `walkGeneration`. O port acrescenta a distinção entre passo normal
  e deslocamentos que invalidam a rota.
- O rastreamento opcional de movimento em `writeToOutputBuffer` foi mantido,
  inclusive seus offsets e opcodes.
- A limpeza de `pendingCreatureEmblems` ao podar um ID foi mantida e coberta
  pelas regressões de criaturas conhecidas.
- Os testes de protocolo que escrevem mensagens passam a reter ownership
  compartilhado e a sair do autosend no teardown, como ocorre em produção.
  As asserções de bytes e capacidades negociadas continuam presentes.
- O teste de recusa do autosend usa o scheduler parado/reiniciado pela sua API
  real. Não presume que esta branch descarte timers críticos por saturação.

Não foram importados vocações, spells, mapas, OTB, banco, Ascension, guild buffs
ou interfaces DBO. Os smart pointers e o ownership das escritas assíncronas
continuam preservados. Não foi alterada a configuração do servidor em uso.

## Validação

Build Linux/WSL separado em `/home/mateus/tfs18-pr337-dbo-qa`, Clang 18,
C++23, Debug `-O0 -g1`, AddressSanitizer e UndefinedBehaviorSanitizer, sem LTO.
Compilação Windows fica com o proprietário. Nenhum servidor de jogo foi iniciado
ou reiniciado por esta validação.

**Resultado final: 72/72 alvos CTest aprovados, zero falhas, 41,02 segundos.**
São cinco verificações de fonte/CMake, 66 executáveis nativos e um teste Lua.
O servidor e todos os executáveis de teste foram compilados com sucesso.
Sem diagnósticos de ASan, LeakSanitizer ou UBSan na execução final.

O teste opcional de round trip SQL executou seu caminho de skip, pois
`TFS_TEST_DB_*` não estava configurado. Não houve acesso ao banco do jogo nem
nova carga de stressbots. ThreadSanitizer não foi executado nesta rodada.

O probe independente do escritor pequeno passou por **50.000 entradas**
aleatórias/válidas/inválidas e limites de capacidade, com ASan/UBSan e sem
diagnósticos. Ele não abre rede nem acessa banco.

Regressões incluídas: equivalência de bytes dos senders reais, visibilidade,
textos/acentos/limites, 1.501 criaturas visíveis, poda/limpeza de emblemas,
imbuements inalterados/expiração, contadores paralelos, ownership e reutilização
de lotes, recusa/recuperação do timer, movimento real de monstros e callbacks
de passos do Market. As demais regressões existentes também foram executadas.

O teste de alvo dos monstros passou em 25 casos. Seu cenário de 100 monstros
executou 500 passos normais com **zero pedidos redundantes de caminho**. Isso
confirma que o reuso existente foi preservado; não é um ganho A/B medido aqui.

A primeira suíte apontou duas adaptações necessárias nos testes: a fixture de
criaturas conhecidas usava um tile sem ownership compartilhado, recusado pelo
assert de lifetime deste destino; e a regressão antiga de emblemas ainda
presumia o orçamento OTC de 250. A fixture agora retém um tile compartilhado e
a regressão verifica tanto 250 (legado) quanto 1.300 (OTC), mantendo todas as
asserções de poda, limpeza e refresh. O assert de produção foi preservado.

## Repetir

```sh
QA_BUILD_DIR="${QA_BUILD_DIR:-$HOME/tfs18-pr337-dbo-qa}"
cmake --build "$QA_BUILD_DIR" --parallel 3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir "$QA_BUILD_DIR" \
  --output-on-failure --timeout 120 -j 1
```

Logs locais: `/home/mateus/tfs18-pr337-dbo-configure.log`,
`/home/mateus/tfs18-pr337-dbo-build.log`,
`/home/mateus/tfs18-pr337-dbo-small-writer.log` e
`/home/mateus/tfs18-pr337-dbo-tests.log`.

## Uso e alcance

Depois de compilar o executável Windows, reiniciar normalmente e relogar os
clientes, as correções passam a valer sem comandos especiais. O cliente não
precisa de alteração por este port.

Para diagnóstico temporário, `performanceMetricsEnabled = true` no startup
ativa os relatórios agregados de tráfego. Nenhuma configuração foi ativada aqui.

Os aproximadamente 10%/17% de CPU observados antes pertencem aos testes DBO,
com outro mapa, scripts, distribuição e gerador de carga. Não são uma medição
desta base. Este port precisa de nova comparação de carga para quantificar seu
efeito no servidor oficial; não estabelece capacidade para determinado número
de jogadores. As melhorias de battle list/renderização foram feitas no cliente
DBOBR e não fazem parte deste repositório de servidor.

## Correções de revisão e CI do PR 337

Revisão do head `7bcf673`, incluindo os logs reais dos jobs
[Windows](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/actions/runs/40437145584/job/121968434651) e
[GCC Release](https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60/actions/runs/40437145620/job/121968434620).

| Achado confirmado | Correção |
|---|---|
| CodeRabbit: um callback de piso pode mover o monstro novamente enquanto a flag de passo está ativa. | A autorização de reuso guarda origem/destino, confere a posição atual e é consumida na primeira notificação do próprio movimento, antes de callbacks. Movimentos aninhados e repetição posterior das mesmas coordenadas provocam atualização. |
| Windows: `far` é um macro dos headers Windows e invalida a declaração usada no teste. | Variável renomeada para `outsideViewport`. |
| GCC 13/O3: diagnóstico `free-nonheap-object` ao otimizar snapshots vazios das fixtures de combate e imbuements. | Cópia por comprimento validado, com vetor dimensionado e `memcpy` somente para comprimento não nulo. As comparações de todos os bytes foram mantidas; nenhum warning foi desativado. |
| Greptile: o candidato isolado não contém os dois arquivos do novo teste Lua. | Copiados explicitamente `tools/test_step_tile_callbacks.lua` e `data/events/scripts/player.lua`, inclusive ao atualizar uma árvore candidata existente. |
| CodeRabbit: falha de `bench_*` era impressa e ignorada pelo script de baseline. | Após imprimir o log, o script retorna o status do benchmark. Testes vermelhos continuam podendo retornar não zero sem interromper a auditoria. |
| O helper de protocolo cresceu e passou a referenciar membros ausentes do baseline. | A auditoria vermelha usa uma fixture mínima própria, sem alterar headers ou layout da engine antiga. |
| Windows: `WIN32_LEAN_AND_MEAN` redefinido quando fornecido pelo build. | Definição protegida por `#ifndef`. |

Os comentários antigos sobre ccache opcional, compatibilidade de `unpack`/`pack`
com LuaJIT e redação/comandos do relatório já estavam corrigidos em `f05aea4`;
foram conferidos sem repetir alterações.

### Validação desta revisão

- Falha de movimento aninhado reproduzida contra a engine de `7bcf673`, antes
  da correção. Após o fix, `test_monster_target_state` passa em **27 casos**,
  incluindo passos aninhados ao norte/leste/oeste, retorno pelas mesmas
  coordenadas e o cenário de 100 monstros/500 passos sem recálculos redundantes.
- Servidor e testes compilados com Clang 18, ASan/UBSan: **72/72 alvos CTest,
  zero falhas, 41,51 s**, sem diagnósticos dos sanitizers.
  Após ajustar também o snapshot da fixture de imbuements, seu alvo foi
  recompilado e repetido com sanitizers: quatro casos aprovados, 0,07 s.
- GCC 13 com `-O3 -Werror`: a unidade que falhava no CI compila sem avisos.
  Engine e 66 executáveis nativos compilados em Release; **72/72 alvos CTest
  aprovados, zero falhas, 15,63 s**.
- O conflito do macro `far` também foi reproduzido com a definição equivalente
  na compilação Linux; o teste corrigido passa nessa configuração. Isso não
  substitui o job MSVC real do GitHub.
- `python3 tools/test_audit_scripts.py`: **cinco testes aprovados**, incluindo
  candidato novo/existente, Release/ASan com e sem ccache, execução dos dois
  arquivos Lua reais, status de benchmark e preservação dos headers do baseline.
  CMake, CTest e compilador são simulados nesse harness; não são quatro builds.
- Quatro fontes de regressão passaram pela compilação sintática estrita com
  headers reais de `49db3276`; hashes de `game.h`, `party.h` e `events.h` foram
  preservados. A biblioteca antiga não foi recompilada nem executada aqui.
- `bash -n` aprovado nos dois scripts alterados. Round trip SQL opcional sem
  execução, pois `TFS_TEST_DB_*` não foi fornecido. Nenhum banco vivo foi usado.

Logs locais desta rodada em `/home/mateus/tfs18-pr337-review3-*`: reprodução de
reentrada, compilação e CTest ASan, compilação/CTest GCC, sintaxe das fixtures de
baseline e reprodução/correção do macro Windows. O build com sanitizers reutiliza
`/home/mateus/tfs18-pr337-dbo-qa`; o build GCC reutiliza
`/home/mateus/tfs18-stress-release`. Nenhum servidor em uso foi reiniciado.
