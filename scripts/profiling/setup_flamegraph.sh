#!/usr/bin/env bash
set -euo pipefail

warn() {
  printf 'warning: %s\n' "$*" >&2
}

info() {
  printf '%s\n' "$*"
}

sudo_cmd=()
if [[ "${EUID}" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    sudo_cmd=(sudo)
  else
    warn "sudo nao encontrado; pulando instalacao apt. Rode como root ou instale sudo."
  fi
fi

if command -v apt-get >/dev/null 2>&1; then
  if ((${#sudo_cmd[@]} > 0)) || [[ "${EUID}" -eq 0 ]]; then
    info "Atualizando apt..."
    "${sudo_cmd[@]}" apt-get update

    info "Instalando dependencias basicas..."
    "${sudo_cmd[@]}" apt-get install -y git perl linux-tools-common linux-tools-generic || \
      warn "Nao consegui instalar todos os pacotes basicos."

    if ! command -v perf >/dev/null 2>&1; then
      kernel_tools="linux-tools-$(uname -r)"
      info "Tentando instalar ${kernel_tools}..."
      "${sudo_cmd[@]}" apt-get install -y "${kernel_tools}" || \
        warn "${kernel_tools} nao existe para este kernel. Em WSL, use linux-tools-generic ou uma distro/kernel com perf disponivel."
    fi
  fi
else
  warn "apt-get nao encontrado; instale git, perl e perf manualmente."
fi

if ! command -v git >/dev/null 2>&1; then
  warn "git nao encontrado. Instale git e rode este script novamente."
  exit 1
fi

flamegraph_dir="${HOME}/FlameGraph"
if [[ ! -d "${flamegraph_dir}/.git" ]]; then
  info "Clonando FlameGraph em ${flamegraph_dir}..."
  git clone https://github.com/brendangregg/FlameGraph "${flamegraph_dir}"
else
  info "Atualizando FlameGraph em ${flamegraph_dir}..."
  git -C "${flamegraph_dir}" pull --ff-only || \
    warn "Nao consegui atualizar FlameGraph com git pull; mantendo copia local."
fi

if ! command -v perf >/dev/null 2>&1; then
  warn "perf ainda nao esta no PATH. Verifique linux-tools do kernel ou suporte do WSL."
else
  info "perf encontrado: $(command -v perf)"
fi

info "FlameGraph pronto em ${flamegraph_dir}"
info "Se o perf falhar por permissao, veja docs/performance/flamegraph.md."
