# Testes da auditoria — 21/10/2026

## Baseline e reprodução

Baseline imutável por git archive de `49db3276`. Configuração, build completo do
servidor e testes feitos antes dos patches funcionais, fora dos mapas locais.

```bash
bash scripts/profiling/run_audit_20261021.sh
```

Resultado: servidor Release GCC compilado; **59/59 CTest**, 4,57 s.
Logs em `/home/mateus/tfs-audit-20261021-0sqVGT/baseline-{configure,build,ctest}.log`.

Os testes novos de B1–B3 foram compilados contra a biblioteca baseline já pronta,
sem alterar suas fontes/ABI. O harness `check_audit_baseline.sh` usa os headers
originais em uma cópia separada e copia apenas os testes novos: não mistura o
layout novo de CombatDamage com a biblioteca antiga. Aliases apenas de teste
reutilizam friends existentes para acesso, sem modificar headers do baseline.
O contrato F5 foi testado carregando os scripts Lua reais antes/depois de
modificar o retorno. Falhas reproduzidas:

- B3: 3 casos (cura positiva legada, mana shield, mana drain), ausência do texto
  correto do observador; alvo/atacante e duas ordens de espectadores cobertos.
- B1: 2 casos novos, viewer aceito por ProtocolGame::canSee ausente da consulta
  e viewer de mesma instância em z7 ausente para evento z4. Na reprodução final,
  um terceiro caso falha porque a expectativa antiga foi corrigida para incluir
  o viewer subterrâneo que realmente vê o evento z7.
- B2: 4 casos falharam: reset PvE duplicado; nível+defesa+Avatar; override Lua
  reprocessado; bônus de nível duplicado após onManaChange. Cura simples passou
  já no baseline, não classificada como a mesma falha.
- F5: 2 casos com biblioteca de callbacks Lua real: callback padrão bloqueava
  o handler seguinte e sua rejeição. Após corrigir o retorno, ambos passaram.
- B4: testes de party não reproduzem um UAF; são regressões de comportamento
  de membros expirados, mudança de liderança, leave/disband e getters Lua.

## Candidato Release GCC

```bash
bash scripts/profiling/build_audit_candidate.sh /home/mateus/tfs-audit-20261021-0sqVGT
cmake --build /home/mateus/tfs-audit-20261021-0sqVGT/candidate-release --target tfs --parallel 2
```

Resultado final em série: **64/64 CTest**, 9,88 s, e executável tfs compilado com LTO.
Log final: `candidate-release-final-ctest.log` no diretório isolado.
Nenhum servidor foi iniciado. Inclui pipeline, write lifetime, limites de
conexão, XTEA, crash regression, condições, lifetime, pathfinding, reactor e
todos os outros testes registrados. O teste opcional de roundtrip em banco
pode encerrar como skip se não houver ambiente de DB configurado; não alegar
um roundtrip SQL real só pela aprovação do executável no CTest.

As regressões de mensagens usam jogadores, protocolo e buffers reais, com
vocation.xml carregado como em um cliente conectado. Os primeiros fixtures
sem vocação/registro de criaturas foram corrigidos durante a escrita dos testes,
não tratados como crash novo de produção. Em cura COMBAT_HEALING, o teste
preserva notificação após ganho de HP e cobre reutilização de CombatDamage por
curas distintas (life leech de área).

## Prova de bytes / Memcheck

`test_networkmessage_wire`: 3 casos, varrendo posições e efeitos min/max,
payload, framing, checksum e XTEA; cópia proprietária após expirar o array
temporário; todos os truncamentos do payload de efeito. Produção continua usando
NetworkMessage inicializado. Comparação de capacidade inteira no teste antigo
permaneceu inalterada.

```bash
valgrind --error-exitcode=99 --track-origins=yes --leak-check=full \
  --show-leak-kinds=definite --log-file=network-memcheck.log \
  /home/mateus/tfs-audit-20261021-0sqVGT/red-tests/test_networkmessage_wire
```

Resultado: **0 erros**, 1.234 alocações/1.234 liberações; 0 bytes em uso no
encerramento. Escopo: esse teste isolado. Não é teste de vazamento do servidor
completo. MSan e TSan completos não executados.

## Validações adicionais concluídas

Fontes finais de código e testes: `9ca057c1933cfdf9d55ab8b63a508bf46794716c`.
O commit seguinte contém somente os scripts e estes relatórios.

| Ambiente | Build e escopo | Resultado final |
|---|---|---|
| GCC 13.3 Release | Servidor completo com LTO e suíte integral | 64/64, 9,88 s |
| GCC 13.3 ASan+UBSan | Core e todos os executáveis de teste; execução serial | 64/64, 37,82 s |
| Clang 18.1.3 diagnóstico | Core e 14 executáveis selecionados, -O1 -g1 -DNDEBUG | 14/14, 0,18 s |
| MSVC 19.51, VS 18, x64 Release | Servidor completo e suíte integral | 64/64, 15,48 s |

```bash
bash scripts/profiling/build_audit_candidate.sh "$AUDIT_ROOT" asan
bash scripts/profiling/build_audit_clang.sh "$AUDIT_ROOT"
ctest --test-dir "$AUDIT_ROOT/candidate-release" --output-on-failure -j1
```

Sanitizers: Debug -O1 -g1, ASan+UBSan, mimalloc OFF, Lua local não instrumentada.
Executado com `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` e
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Logs finais:
`candidate-asan-{configure,build,ctest}.log`. Não há supressão adicionada.

Falhas intermediárias, preservadas e não omitidas:

- ASan/UBSan encontrou defeitos nos fixtures novos: envio fora da afinidade do
  dispatcher e serviço global Chat ausente na saída de party. Corrigidos os
  fixtures para rodar no reactor real e manter Chat vivo até o teardown. Não
  foi removido o assert de produção nem adicionado null guard para ocultá-los.
- O teste existente `inbox_collection_stays_linear` falhou em execuções paralelas
  durante outros builds (logs `release-timing-contention.log` e
  `asan-timing-contention.log`). Em Release: 28.557 ns/1.000 itens e 948.980
  ns/10.000, razão 33,23 acima do limite 30. É uma comparação de tempo sensível
  à carga; não prova regressão no algoritmo. As execuções finais seriais de
  Release e ASan/UBSan passaram sem alterar o teste, o algoritmo ou seu limite.
- O build inicial Clang -O3 foi interrompido após mais de dez minutos na unidade
  Lua, sem diagnóstico de erro do compilador. O build Clang concluído usa -O1
  apenas para diagnóstico. **Clang produção -O3 integral não validado**.
- MSVC emitiu avisos não fatais de redefinição de WIN32_LEAN_AND_MEAN e uso de
  diretório temporário para build incremental; não foram tratadas áreas alheias.

O preset ASan do repositório contém caminhos locais de Lua: foram usadas flags
explícitas nos scripts, sem alegar que o preset original funcionou inalterado.
O build inicial ASan também foi interrompido uma vez para atualizar fontes.

Windows: fonte isolada em
`C:\Users\Mateus\AppData\Local\Temp\tfs-audit-win-20261021-e0bb29f9e89f44f5b9c2d30ba3a0e912`,
com Lua 5.5 e dependências do prefixo
`vcpkg_installed/x64-windows/x64-windows`. Logs `windows-build.log`,
`windows-fixtures-build.log` e `windows-ctest.log`. Comandos executados no
Developer PowerShell com CMake/CTest do VS (prefixo e PATH de DLLs configurados):

```powershell
cmake --build "$auditWin/build" --config Release --parallel 2
ctest --test-dir "$auditWin/build" -C Release --output-on-failure -j2
```

`$auditWin` representa o diretório isolado informado acima. Arquivos locais de
mapa, config e logs não foram copiados para os builds. Os scripts Bash requerem
Ninja, rsync, ccache e as dependências C++/Lua instaladas. `bash -n` foi usado
nos quatro scripts novos; os executáveis e dados brutos não entram no Git.

## Não executado e gates restantes

- Matriz de 400/600/1.000 bots, soak, capturas de payload de um cliente real,
  login/logout simultâneo, PvP completo, sockets reais de produção, restart/shutdown.
- Teste interativo de spawn/morte/teleporte com observadores em andares diferentes.
- Simulação de todas as mutações Lua reentrantes de party, charms, summons e reload
  em jogo; a pinagem local não é uma prova de ausência global de UAF.
- Performance real de condições/IA/casts, cache de espectadores, materialização
  de tiles e allocator; não houve mudança estrutural sem essas medições.

Não foram apagados mapas/logs, alteradas frequências de gameplay, desabilitados
efeitos ou removidas proteções de lifetime. Commits são apenas locais nesta branch;
push e PR exigem autorização expressa do usuário.
