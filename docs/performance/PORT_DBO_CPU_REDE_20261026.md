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
cmake --build /home/mateus/tfs18-pr337-dbo-qa --parallel 3
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir /home/mateus/tfs18-pr337-dbo-qa \
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
