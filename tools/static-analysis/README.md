# Analise estatica do TFS

Este pacote gera diagnosticos para o TFS 1.8 Downgrade sem alterar a logica do
servidor, protocolo, gameplay, scripts Lua ou dados do jogo. Os relatorios vao
para `analysis-reports/`, que nao e versionado. Um aviso e uma pista de revisao,
nao um bug confirmado.

## Ferramentas

- **Cppcheck** procura defeitos, problemas de portabilidade e gargalos comuns
  usando a base de compilacao do CMake quando ela esta disponivel.
- **clang-tidy** aplica checks de `bugprone`, `performance`, `modernize` e
  `readability` com uma configuracao conservadora para codigo legado C++23.
- **Lizard** mede a complexidade ciclomática, tamanho de funcao e quantidade de
  parametros. O relatorio inclui as 30 funcoes mais complexas.
- **IWYU (Include What You Use)** sugere includes para revisao manual. Em uma
  base legada, ele pode gerar bastante ruido; use por pasta ou arquivo e nunca
  aplique as sugestoes em lote.

## Instalacao no Ubuntu ou WSL

```bash
sudo apt update
sudo apt install -y cppcheck clang-tidy iwyu python3-pip
python3 -m pip install --user lizard
```

Garanta que `~/.local/bin` esteja no `PATH` para usar o executavel `lizard`.
Antes de executar a analise completa, conclua tambem os
[pre-requisitos de compilacao do TFS](../../README.md#compilation), incluindo
Lua 5.5, `simdutf`, `mio` e as bibliotecas de desenvolvimento listadas no guia.
Eles sao necessarios para o CMake gerar `build-analysis/compile_commands.json`.
No Ubuntu/WSL, o runner segue os mesmos caminhos do `build.sh`: Lua 5.5 em
`/usr/local` e dependencias locais em `~/.local`. Em uma instalacao diferente,
defina `TFS_LUA_PREFIX` e `TFS_LOCAL_PREFIX` antes de executa-lo.

## Como rodar

Na raiz do repositorio:

```bash
bash tools/static-analysis/run-static-analysis.sh
```

No PowerShell:

```powershell
.\tools\static-analysis\run-static-analysis.ps1
```

O modo padrao cria `build-analysis/` com `ENABLE_UNITY_BUILD=OFF`, exporta a
base de compilacao e executa todas as ferramentas instaladas. Ele nao compila o
servidor, nao remove diretorios existentes e nao aplica correcoes.

### Modos

```bash
# Inclui os achados inconclusivos do Cppcheck.
bash tools/static-analysis/run-static-analysis.sh --deep

# Falha apenas quando os limites do Lizard forem excedidos.
bash tools/static-analysis/run-static-analysis.sh --strict

# Executa uma unica ferramenta.
bash tools/static-analysis/run-static-analysis.sh --cppcheck
bash tools/static-analysis/run-static-analysis.sh --clang-tidy
bash tools/static-analysis/run-static-analysis.sh --lizard
bash tools/static-analysis/run-static-analysis.sh --iwyu

# A unica operacao que pode modificar codigo, sempre em um arquivo explicito.
bash tools/static-analysis/run-static-analysis.sh --fix-file src/example.cpp
```

O equivalente no PowerShell troca `--cppcheck` por `-Cppcheck`, `--deep` por
`-Deep` e `--fix-file` por `-FixFile`. Nunca use `clang-tidy --fix` em todo o
projeto: revise o diff de cada arquivo antes de manter uma correcao.

## Relatorios

- `analysis-reports/configuration.txt`: saida do CMake para a base de
  compilacao.
- `analysis-reports/cppcheck.txt` e `cppcheck.xml`: diagnosticos legiveis e
  estruturados do Cppcheck.
- `analysis-reports/clang-tidy.txt`: achados do clang-tidy sem `--fix`.
- `analysis-reports/lizard.txt`: limites iniciais (CCN maior que 20, mais de
  180 linhas ou mais de 8 parametros) e top 30 por complexidade.
- `analysis-reports/iwyu.txt`: sugestoes do IWYU, quando o driver
  `iwyu_tool.py` estiver instalado.

Revise primeiro os achados com maior risco:

1. ponteiro nulo, use-after-free e referencia pendente;
2. variavel nao inicializada e acesso fora dos limites;
3. vazamento de memoria ou recurso;
4. move/copy suspeito;
5. loops com custo alto ou alocacoes repetidas.

Registre falsos positivos revisados em `.cppcheck-suppressions.txt` de forma
especifica por arquivo e linha. O arquivo aceita apenas regras de supressao
validas do Cppcheck; mantenha a justificativa no commit ou no PR. Nao acrescente
supressoes amplas para silenciar uma categoria inteira.

## CI inicial

O workflow `.github/workflows/static-analysis.yml` publica Cppcheck e Lizard
como artefatos de diagnostico. Ele usa `continue-on-error` enquanto a baseline
e limpa, portanto ainda nao bloqueia pull requests. clang-tidy e IWYU ficam
manuais ate que as dependencias completas e a baseline estejam estaveis.
