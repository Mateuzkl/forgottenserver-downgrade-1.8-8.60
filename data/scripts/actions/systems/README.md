# Boss room com instancia

Este README explica o exemplo de boss room em `boss_room.lua`.

O script serve para testar e demonstrar o sistema de instancia do servidor. Ele nao e uma boss room generica pronta para copiar sem revisar as posicoes.

## O que esta sala faz

Esta boss room cria uma instancia separada para cada entrada pela alavanca.

Isso quer dizer:

- jogadores dentro da mesma `instanceId` conseguem se ver;
- jogadores em `instanceId` diferente nao conseguem se ver;
- cada uso valido da alavanca cria uma nova instancia;
- o boss nasce dentro da instancia criada;
- o teleport de saida com o `EXIT_AID` correto limpa a instancia e zera o `instanceId` do player.

Se um jogador nao ve outro jogador que esta em outra instancia, isso nao e bug. Esse e o comportamento correto do sistema.

## Como funciona a entrada

Para entrar, o player precisa estar em um dos `PLAYER_TILES` e usar a alavanca com o `LEVER_AID`.

Fluxo da entrada:

- o script confere se o player que usou a alavanca esta em um dos `PLAYER_TILES`;
- bloqueia a entrada se o player ja estiver dentro de alguma instancia;
- pega todos os players que estao nos `PLAYER_TILES`;
- confere cooldown de boss para cada player;
- cria uma nova `instanceId` usando `instanceCounter`;
- registra a area da instancia com `Game.registerInstanceArea(instanceId, INSTANCE_FROM, INSTANCE_TO)`;
- seta a mesma `instanceId` em todos os players encontrados nos tiles;
- teleporta os players para `PLAYER_DEST`;
- cria o boss em `BOSS_POS` usando a mesma `instanceId`;
- registra o evento de morte `BossRoomBossDeath` no boss criado.

O boss so pertence aquela instancia. Players fora dela ou em outra instancia nao devem ver esse boss.

## Como funciona a saida

O teleport de saida precisa ter o `EXIT_AID` configurado no mapa.

Ao pisar no teleport de saida:

- o script pega o `instanceId` atual do player;
- chama `cleanupPlayerInstance(player)`;
- remove monstros e summons daquela instancia;
- chama `Game.unregisterInstanceArea(instanceId)`;
- teleporta o player para `Position(1054, 1000, 7)`;
- seta `instanceId` do player para `0`.

Esse fluxo e importante. Se o `EXIT_AID` estiver errado, o player pode sair visualmente da sala sem limpar a instancia corretamente.

## Logout e login dentro da area

O script tambem registra eventos de logout e login.

Se o player deslogar dentro da area da instancia, o logout:

- limpa a instancia do player;
- teleporta o player para o templo da town;
- seta `instanceId` para `0`.

Se o player logar dentro da area da instancia, o login:

- registra novamente o evento `BossRoomLogout`;
- manda o player para o templo;
- garante que o `instanceId` volte para `0`.

Isso evita player preso dentro de uma instancia antiga.

## Como criar outra boss room

Nao e so mudar `LEVER_AID`, `EXIT_AID` e `instanceCounter`.

Para outro boss, revise todos estes pontos:

- `BOSS_NAME`: nome exato do monstro;
- `BOSS_POS`: posicao onde o boss vai nascer;
- `PLAYER_DEST`: posicao onde os players vao cair dentro da sala;
- `INSTANCE_FROM`: canto inicial da area da instancia;
- `INSTANCE_TO`: canto final da area da instancia;
- `PLAYER_TILES`: posicoes dos tiles antes da alavanca;
- `LEVER_AID`: action id da alavanca no mapa;
- `EXIT_AID`: action id do teleport de saida no mapa;
- destino do teleport de saida dentro do script;
- `COOLDOWN_TIME`, se quiser outro tempo de cooldown;
- `instanceCounter` ou range inicial unico para nao conflitar com outra boss room.

Se copiar o script e esquecer alguma posicao antiga, a boss room pode parecer bugada mesmo com o sistema de instancia funcionando corretamente.

## Cuidados no Map Editor

Checklist para revisar no mapa:

- [ ] colocar o `LEVER_AID` correto na alavanca;
- [ ] colocar o `EXIT_AID` correto no teleport de saida;
- [ ] conferir o destino do teleport de saida;
- [ ] nao colocar o destino em cima de outro teleport ou movement;
- [ ] nao duplicar action id em outra alavanca ou outro script;
- [ ] conferir se `INSTANCE_FROM` e `INSTANCE_TO` cobrem toda a sala;
- [ ] conferir se `BOSS_POS` esta dentro da area da instancia;
- [ ] conferir se `PLAYER_DEST` esta dentro da area da instancia;
- [ ] conferir se `PLAYER_TILES` estao fora da sala ou na area correta de entrada;
- [ ] nao reaproveitar o mesmo `instanceCounter` ou range em outro script copiado.

## Falsos bugs comuns

Alguns comportamentos parecem bug, mas normalmente sao configuracao ou efeito esperado da instancia:

- se o player sair pelo teleport de saida correto, a instancia sera resetada;
- se o player deslogar dentro da area da instancia, ele sera enviado ao templo no logout/login;
- se o dev copiar o script e esquecer posicoes antigas, o player pode voltar para o lugar errado;
- se o `EXIT_AID` estiver errado, a instancia pode nao limpar;
- se o destino do teleport estiver errado, o player pode parecer preso ou bugado;
- se dois scripts usam o mesmo action id, o servidor pode chamar o fluxo errado;
- se dois scripts usam o mesmo range de `instanceCounter`, pode gerar conflito de `instanceId`;
- se `INSTANCE_FROM` e `INSTANCE_TO` nao cobrirem toda a sala, parte da boss room pode ficar fora da instancia;
- se `BOSS_POS` ou `PLAYER_DEST` ficarem fora da area registrada, o comportamento visual pode confundir o teste.

## Configuracao atual do exemplo

Valores atuais em `boss_room.lua`:

```lua
local LEVER_AID = 8890
local EXIT_AID  = 1004

local BOSS_NAME = "Gaz'Haragoth"
local BOSS_POS  = Position(1058, 955, 13)

local PLAYER_DEST = Position(1064, 955, 13)

local INSTANCE_FROM = Position(1045, 947, 13)
local INSTANCE_TO   = Position(1068, 965, 13)

local COOLDOWN_TIME = 3600
local instanceCounter = 30000
```

Tiles atuais de entrada:

```lua
local PLAYER_TILES = {
    Position(1091, 955, 13),
    Position(1092, 955, 13),
    Position(1093, 955, 13),
    Position(1094, 955, 13),
    Position(1095, 955, 13),
    Position(1096, 955, 13),
    Position(1097, 955, 13),
    Position(1098, 955, 13),
    Position(1099, 955, 13),
    Position(1100, 955, 13),
}
```

Destino atual do teleport de saida:

```lua
local dest = Position(1054, 1000, 7)
```

## Checklist final para novo boss

- [ ] Copiei o script com outro nome
- [ ] Troquei `BOSS_NAME`
- [ ] Troquei `BOSS_POS`
- [ ] Troquei `PLAYER_DEST`
- [ ] Troquei `INSTANCE_FROM` e `INSTANCE_TO`
- [ ] Troquei `PLAYER_TILES`
- [ ] Troquei `LEVER_AID`
- [ ] Troquei `EXIT_AID`
- [ ] Configurei a alavanca no mapa
- [ ] Configurei o teleport de saida no mapa
- [ ] Usei `instanceCounter` ou range diferente
- [ ] Testei entrada, saida, logout e cooldown
- [ ] Verifiquei o console e `data/logs/server.log`
