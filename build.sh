#!/usr/bin/env bash
set -Eeuo pipefail

REPO_URL="https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60.git"

LUA_VERSION="5.5.0"
LUA_TARBALL="lua-${LUA_VERSION}.tar.gz"
LUA_URL="https://www.lua.org/ftp/${LUA_TARBALL}"
LUA_SHA256="57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d"
LUA_PREFIX="/usr/local"

SIMDUTF_DIR="${HOME}/.cache/tfs-build/simdutf"
SIMDUTF_PREFIX="${HOME}/.local"
MIO_DIR="${HOME}/.cache/tfs-build/mio"

BUILD_DIR="build-release"
OUTPUT_BIN=""
UBUNTU_TARGET="${TFS_UBUNTU_TARGET:-auto}"
UI_LANG="${TFS_BUILD_LANG:-}"
JOBS="${JOBS:-}"
HTTP="ON"
USE_MIMALLOC="ON"
CLEAN_BUILD=0
SKIP_DEPS=0
SKIP_BUILD=0
NONINTERACTIVE=0

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APT_UPDATED=0
SUDO=()

if [[ -t 1 ]]; then
  BOLD=$'\033[1m'
  DIM=$'\033[2m'
  RED=$'\033[31m'
  GREEN=$'\033[32m'
  YELLOW=$'\033[33m'
  BLUE=$'\033[34m'
  CYAN=$'\033[36m'
  RESET=$'\033[0m'
else
  BOLD=""
  DIM=""
  RED=""
  GREEN=""
  YELLOW=""
  BLUE=""
  CYAN=""
  RESET=""
fi

declare -A MSG_PT=(
  [banner_title]="Assistente de build TFS 1.8 - 8.60"
  [banner_subtitle]="Ubuntu/WSL, dependencias, Lua 5.5, Asio, mio e CMake"
  [choose_lang]="Escolha o idioma:"
  [language_set]="Idioma: Portugues"
  [detect_system]="Sistema detectado"
  [wsl_detected]="WSL detectado"
  [choose_ubuntu]="Escolha a versao do Ubuntu para preparar o build:"
  [ubuntu_prompt]="Digite 1 ou 2 [detectado: %s]: "
  [invalid_option]="opcao invalida"
  [using_ubuntu]="Usando configuracao para Ubuntu %s"
  [section_preflight]="Verificando ambiente"
  [section_deps]="Verificando dependencias apt"
  [section_lua]="Verificando Lua 5.5"
  [section_simdutf]="Verificando simdutf"
  [section_repo]="Verificando projeto TFS"
  [section_build]="Compilando TFS em Release"
  [need_ubuntu]="Este script foi feito para Ubuntu 22.04/24.04 com apt/dpkg."
  [need_sudo]="sudo nao encontrado. Rode como root ou instale sudo."
  [pkg_present]="pacote ja instalado: %s"
  [pkg_installing]="instalando pacotes ausentes: %s"
  [pkg_all_present]="todos os pacotes apt ja estao instalados"
  [apt_update]="atualizando indices apt"
  [tool_version]="%s: %s"
  [cmake_too_old]="CMake precisa ser >= 3.20. Versao encontrada: %s"
  [project_ok]="CMakeLists.txt encontrado em: %s"
  [project_clone]="CMakeLists.txt nao encontrado. Clonando repositorio..."
  [project_lua_warn]="Aviso: nao encontrei a configuracao do Lua 5.5 no CMakeLists.txt."
  [project_summary]="CMake pede: C++23, Lua 5.5, OpenSSL 3, Asio, mio, simdutf, absl, fmt, spdlog, pugixml e MySQL."
  [lua_ok]="Lua 5.5 ja esta OK em %s"
  [lua_pc_ok]="pkg-config do Lua 5.5 pronto: %s"
  [lua_pc_verify_failed]="pkg-config nao conseguiu validar Lua 5.5: %s"
  [lua_install]="instalando Lua %s manualmente"
  [lua_old]="Lua ausente ou diferente de 5.5. Instalando a versao correta."
  [lua_missing_skip_deps]="Lua 5.5 nao esta pronto em %s. Rode ./build.sh sem --skip-deps para o script instalar o Lua, ou instale o Lua 5.5 manualmente antes do build."
  [simdutf_ok]="simdutf ja esta instalado em %s"
  [simdutf_install]="instalando simdutf em %s"
  [simdutf_pull_warn]="Nao consegui atualizar simdutf por git pull; vou continuar com a copia local."
  [mio_ok]="mio ja esta instalado em %s"
  [mio_install]="instalando mio em %s"
  [mio_pull_warn]="Nao consegui atualizar mio por git pull; vou continuar com a copia local."
  [section_mio]="Verificando mio"
  [clean_build]="limpando pasta de build: %s"
  [configure_retry]="CMake falhou. Limpando cache de build e tentando uma vez novamente."
  [build_done]="Build finalizado."
  [binary_path]="Binario pronto em: %s"
  [ldd_ok]="ldd nao encontrou bibliotecas ausentes."
  [ldd_missing]="ldd encontrou bibliotecas ausentes no binario."
  [run_hint]="Para rodar: cd %s && ./tfs"
  [done]="Tudo pronto."
  [fail_line]="Falha na linha %s com codigo %s."
  [safe_delete_refused]="Recusei remover caminho fora do projeto: %s"
  [skip_deps]="Pulando instalacao/verificacao de dependencias por opcao do usuario."
  [skip_build]="Pulando build por opcao do usuario."
)

declare -A MSG_EN=(
  [banner_title]="TFS 1.8 - 8.60 build assistant"
  [banner_subtitle]="Ubuntu/WSL, dependencies, Lua 5.5, Asio, mio and CMake"
  [choose_lang]="Choose language:"
  [language_set]="Language: English"
  [detect_system]="Detected system"
  [wsl_detected]="WSL detected"
  [choose_ubuntu]="Choose the Ubuntu version to prepare the build:"
  [ubuntu_prompt]="Type 1 or 2 [detected: %s]: "
  [invalid_option]="invalid option"
  [using_ubuntu]="Using Ubuntu %s configuration"
  [section_preflight]="Checking environment"
  [section_deps]="Checking apt dependencies"
  [section_lua]="Checking Lua 5.5"
  [section_simdutf]="Checking simdutf"
  [section_repo]="Checking TFS project"
  [section_build]="Building TFS Release"
  [need_ubuntu]="This script is intended for Ubuntu 22.04/24.04 with apt/dpkg."
  [need_sudo]="sudo was not found. Run as root or install sudo."
  [pkg_present]="package already installed: %s"
  [pkg_installing]="installing missing packages: %s"
  [pkg_all_present]="all apt packages are already installed"
  [apt_update]="updating apt indexes"
  [tool_version]="%s: %s"
  [cmake_too_old]="CMake must be >= 3.20. Found: %s"
  [project_ok]="CMakeLists.txt found at: %s"
  [project_clone]="CMakeLists.txt not found. Cloning repository..."
  [project_lua_warn]="Warning: Lua 5.5 configuration was not found in CMakeLists.txt."
  [project_summary]="CMake requires: C++23, Lua 5.5, OpenSSL 3, Asio, mio, simdutf, absl, fmt, spdlog, pugixml and MySQL."
  [lua_ok]="Lua 5.5 is already OK at %s"
  [lua_pc_ok]="Lua 5.5 pkg-config is ready: %s"
  [lua_pc_verify_failed]="pkg-config could not validate Lua 5.5: %s"
  [lua_install]="installing Lua %s manually"
  [lua_old]="Lua is missing or different from 5.5. Installing the correct version."
  [lua_missing_skip_deps]="Lua 5.5 is not ready at %s. Run ./build.sh without --skip-deps so the script can install Lua, or install Lua 5.5 manually before building."
  [simdutf_ok]="simdutf is already installed at %s"
  [simdutf_install]="installing simdutf at %s"
  [simdutf_pull_warn]="Could not update simdutf with git pull; continuing with local copy."
  [mio_ok]="mio is already installed at %s"
  [mio_install]="installing mio at %s"
  [mio_pull_warn]="Could not update mio with git pull; continuing with local copy."
  [section_mio]="Checking mio"
  [clean_build]="cleaning build directory: %s"
  [configure_retry]="CMake failed. Cleaning the build cache and trying once again."
  [build_done]="Build finished."
  [binary_path]="Binary ready at: %s"
  [ldd_ok]="ldd did not find missing libraries."
  [ldd_missing]="ldd found missing libraries in the binary."
  [run_hint]="To run: cd %s && ./tfs"
  [done]="All done."
  [fail_line]="Failed at line %s with exit code %s."
  [safe_delete_refused]="Refused to remove path outside project: %s"
  [skip_deps]="Skipping dependency install/check by user option."
  [skip_build]="Skipping build by user option."
)

declare -A MSG_ES=(
  [banner_title]="Asistente de build TFS 1.8 - 8.60"
  [banner_subtitle]="Ubuntu/WSL, dependencias, Lua 5.5, Asio, mio y CMake"
  [choose_lang]="Elige el idioma:"
  [language_set]="Idioma: Espanol"
  [detect_system]="Sistema detectado"
  [wsl_detected]="WSL detectado"
  [choose_ubuntu]="Elige la version de Ubuntu para preparar el build:"
  [ubuntu_prompt]="Escribe 1 o 2 [detectado: %s]: "
  [invalid_option]="opcion invalida"
  [using_ubuntu]="Usando configuracion para Ubuntu %s"
  [section_preflight]="Verificando entorno"
  [section_deps]="Verificando dependencias apt"
  [section_lua]="Verificando Lua 5.5"
  [section_simdutf]="Verificando simdutf"
  [section_repo]="Verificando proyecto TFS"
  [section_build]="Compilando TFS Release"
  [need_ubuntu]="Este script fue hecho para Ubuntu 22.04/24.04 con apt/dpkg."
  [need_sudo]="sudo no fue encontrado. Ejecuta como root o instala sudo."
  [pkg_present]="paquete ya instalado: %s"
  [pkg_installing]="instalando paquetes faltantes: %s"
  [pkg_all_present]="todos los paquetes apt ya estan instalados"
  [apt_update]="actualizando indices apt"
  [tool_version]="%s: %s"
  [cmake_too_old]="CMake debe ser >= 3.20. Version encontrada: %s"
  [project_ok]="CMakeLists.txt encontrado en: %s"
  [project_clone]="CMakeLists.txt no encontrado. Clonando repositorio..."
  [project_lua_warn]="Aviso: no encontre la configuracion de Lua 5.5 en CMakeLists.txt."
  [project_summary]="CMake pide: C++23, Lua 5.5, OpenSSL 3, Asio, mio, simdutf, absl, fmt, spdlog, pugixml y MySQL."
  [lua_ok]="Lua 5.5 ya esta OK en %s"
  [lua_pc_ok]="pkg-config de Lua 5.5 listo: %s"
  [lua_pc_verify_failed]="pkg-config no pudo validar Lua 5.5: %s"
  [lua_install]="instalando Lua %s manualmente"
  [lua_old]="Lua falta o es diferente de 5.5. Instalando la version correcta."
  [lua_missing_skip_deps]="Lua 5.5 no esta listo en %s. Ejecuta ./build.sh sin --skip-deps para que el script instale Lua, o instala Lua 5.5 manualmente antes del build."
  [simdutf_ok]="simdutf ya esta instalado en %s"
  [simdutf_install]="instalando simdutf en %s"
  [simdutf_pull_warn]="No pude actualizar simdutf con git pull; continuo con la copia local."
  [mio_ok]="mio ya esta instalado en %s"
  [mio_install]="instalando mio en %s"
  [mio_pull_warn]="No pude actualizar mio con git pull; continuo con la copia local."
  [section_mio]="Verificando mio"
  [clean_build]="limpiando carpeta de build: %s"
  [configure_retry]="CMake fallo. Limpiando cache de build e intentando una vez mas."
  [build_done]="Build finalizado."
  [binary_path]="Binario listo en: %s"
  [ldd_ok]="ldd no encontro bibliotecas faltantes."
  [ldd_missing]="ldd encontro bibliotecas faltantes en el binario."
  [run_hint]="Para ejecutar: cd %s && ./tfs"
  [done]="Todo listo."
  [fail_line]="Fallo en la linea %s con codigo %s."
  [safe_delete_refused]="Rechace remover una ruta fuera del proyecto: %s"
  [skip_deps]="Saltando instalacion/verificacion de dependencias por opcion del usuario."
  [skip_build]="Saltando build por opcion del usuario."
)

msg() {
  local key="$1"
  local value=""

  case "${UI_LANG:-en}" in
    pt) value="${MSG_PT[$key]:-}" ;;
    es) value="${MSG_ES[$key]:-}" ;;
    *) value="${MSG_EN[$key]:-}" ;;
  esac

  if [[ -z "${value}" ]]; then
    value="${MSG_EN[$key]:-$key}"
  fi

  printf '%s' "${value}"
}

say() {
  printf '%s\n' "$(msg "$1")"
}

sayf() {
  local key="$1"
  shift
  printf "$(msg "${key}")\n" "$@"
}

info() {
  printf '%b[INFO]%b %s\n' "${BLUE}" "${RESET}" "$*"
}

ok() {
  printf '%b[OK]%b %s\n' "${GREEN}" "${RESET}" "$*"
}

warn() {
  printf '%b[WARN]%b %s\n' "${YELLOW}" "${RESET}" "$*"
}

fail() {
  printf '%b[ERROR]%b %s\n' "${RED}" "${RESET}" "$*" >&2
}

die() {
  fail "$*"
  exit 1
}

section() {
  local key="$1"
  printf '\n%b============================================================%b\n' "${CYAN}" "${RESET}"
  printf '%b%s%b\n' "${BOLD}" "$(msg "${key}")" "${RESET}"
  printf '%b============================================================%b\n' "${CYAN}" "${RESET}"
}

on_error() {
  local code=$?
  local line="${BASH_LINENO[0]:-?}"
  set +e
  fail "$(printf "$(msg fail_line)" "${line}" "${code}")"
  exit "${code}"
}

trap on_error ERR

usage() {
  cat <<'EOF'
Usage: ./build.sh [options]

Options:
  --lang pt|en|es        Select language without prompt
  --ubuntu 22.04|24.04   Select Ubuntu dependency strategy
  --jobs N               Parallel build jobs
  --clean                Remove build-release before configuring
  --output PATH          Copy final binary to PATH (default: ./tfs)
  --http on|off          Configure CMake HTTP option (default: on)
  --no-mimalloc          Disable mimalloc in CMake
  --skip-deps            Do not install/check dependencies
  --skip-build           Install/check dependencies only
  --non-interactive      Use detected/default choices
  -h, --help             Show this help

Environment:
  TFS_BUILD_LANG=pt|en|es
  TFS_UBUNTU_TARGET=22.04|24.04
  JOBS=N
EOF
}

normalize_lang() {
  local value="${1,,}"
  case "${value}" in
    pt|pt-br|br|1|portugues|portuguese) printf 'pt' ;;
    en|en-us|english|2) printf 'en' ;;
    es|es-es|espanol|spanish|3) printf 'es' ;;
    *) return 1 ;;
  esac
}

parse_args() {
  while (($#)); do
    case "$1" in
      --lang)
        [[ $# -ge 2 ]] || die "--lang requires a value"
        UI_LANG="$(normalize_lang "$2")" || die "invalid language: $2"
        shift 2
        ;;
      --ubuntu)
        [[ $# -ge 2 ]] || die "--ubuntu requires a value"
        UBUNTU_TARGET="$2"
        shift 2
        ;;
      --jobs)
        [[ $# -ge 2 ]] || die "--jobs requires a value"
        JOBS="$2"
        shift 2
        ;;
      --clean)
        CLEAN_BUILD=1
        shift
        ;;
      --output)
        [[ $# -ge 2 ]] || die "--output requires a value"
        OUTPUT_BIN="$2"
        shift 2
        ;;
      --http)
        [[ $# -ge 2 ]] || die "--http requires a value"
        case "${2,,}" in
          on|yes|true|1) HTTP="ON" ;;
          off|no|false|0) HTTP="OFF" ;;
          *) die "--http must be on or off" ;;
        esac
        shift 2
        ;;
      --no-mimalloc)
        USE_MIMALLOC="OFF"
        shift
        ;;
      --skip-deps)
        SKIP_DEPS=1
        shift
        ;;
      --skip-build)
        SKIP_BUILD=1
        shift
        ;;
      --non-interactive)
        NONINTERACTIVE=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        die "unknown option: $1"
        ;;
    esac
  done
}

choose_language() {
  if [[ -n "${UI_LANG}" ]]; then
    UI_LANG="$(normalize_lang "${UI_LANG}")" || die "invalid language: ${UI_LANG}"
    return
  fi

  if [[ "${NONINTERACTIVE}" -eq 1 || ! -t 0 ]]; then
    UI_LANG="pt"
    return
  fi

  printf '\n%s\n' "Select language / Escolha o idioma / Elige el idioma:"
  printf '  1) Portugues\n'
  printf '  2) English\n'
  printf '  3) Espanol\n\n'

  local choice=""
  read -r -p "> " choice || choice=""
  choice="${choice:-1}"
  UI_LANG="$(normalize_lang "${choice}")" || die "invalid language: ${choice}"
}

banner() {
  printf '\n%b%s%b\n' "${BOLD}${CYAN}" "$(msg banner_title)" "${RESET}"
  printf '%s\n' "$(msg banner_subtitle)"
  printf '%b%s%b\n\n' "${DIM}" "Repo: ${REPO_URL}" "${RESET}"
  say language_set
}

detect_wsl() {
  if grep -qi microsoft /proc/version 2>/dev/null; then
    printf 'sim'
  else
    printf 'nao'
  fi
}

detect_os_id() {
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    printf '%s' "${ID:-unknown}"
  else
    printf 'unknown'
  fi
}

detect_ubuntu_version() {
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    printf '%s' "${VERSION_ID:-unknown}"
  else
    printf 'unknown'
  fi
}

choose_ubuntu() {
  local detected default_choice choice
  detected="$(detect_ubuntu_version)"

  info "$(msg detect_system): Ubuntu ${detected}"
  info "$(msg wsl_detected): $(detect_wsl)"

  if [[ "${UBUNTU_TARGET}" == "22.04" || "${UBUNTU_TARGET}" == "24.04" ]]; then
    sayf using_ubuntu "${UBUNTU_TARGET}"
    return
  fi

  if [[ "${detected}" == "22.04" ]]; then
    default_choice="1"
  elif [[ "${detected}" == "24.04" ]]; then
    default_choice="2"
  else
    default_choice=""
  fi

  if [[ "${NONINTERACTIVE}" -eq 1 || ! -t 0 ]]; then
    [[ -n "${default_choice}" ]] || die "$(msg need_ubuntu)"
    choice="${default_choice}"
  else
    printf '\n%s\n' "$(msg choose_ubuntu)"
    printf '  1) Ubuntu 22.04\n'
    printf '  2) Ubuntu 24.04\n\n'
    printf "$(msg ubuntu_prompt)" "${default_choice:-nenhum}"
    read -r choice || choice=""
    choice="${choice:-$default_choice}"
  fi

  case "${choice}" in
    1) UBUNTU_TARGET="22.04" ;;
    2) UBUNTU_TARGET="24.04" ;;
    *) die "$(msg invalid_option)" ;;
  esac

  sayf using_ubuntu "${UBUNTU_TARGET}"
}

init_sudo() {
  if [[ "${EUID}" -eq 0 ]]; then
    SUDO=()
  else
    command -v sudo >/dev/null 2>&1 || die "$(msg need_sudo)"
    SUDO=(sudo)
  fi
}

require_ubuntu_tools() {
  command -v apt >/dev/null 2>&1 || die "$(msg need_ubuntu)"
  command -v dpkg-query >/dev/null 2>&1 || die "$(msg need_ubuntu)"

  local os_id
  os_id="$(detect_os_id)"
  [[ "${os_id}" == "ubuntu" ]] || warn "$(msg need_ubuntu)"
}

version_ge() {
  dpkg --compare-versions "$1" ge "$2"
}

tool_version() {
  local tool="$1"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    printf 'missing'
    return
  fi

  case "${tool}" in
    cmake) cmake --version | awk 'NR == 1 {print $3}' ;;
    g++|gcc) "${tool}" -dumpfullversion -dumpversion ;;
    *) "${tool}" --version 2>/dev/null | awk 'NR == 1 {print $NF}' ;;
  esac
}

preflight() {
  section section_preflight
  require_ubuntu_tools
  init_sudo

  local cmake_version gcc_version gxx_version
  cmake_version="$(tool_version cmake)"
  gcc_version="$(tool_version gcc)"
  gxx_version="$(tool_version g++)"

  sayf tool_version "cmake" "${cmake_version}"
  sayf tool_version "gcc" "${gcc_version}"
  sayf tool_version "g++" "${gxx_version}"

  if [[ "${cmake_version}" != "missing" ]] && ! version_ge "${cmake_version}" "3.20"; then
    die "$(printf "$(msg cmake_too_old)" "${cmake_version}")"
  fi
}

apt_update_once() {
  if [[ "${APT_UPDATED}" -eq 0 ]]; then
    info "$(msg apt_update)"
    "${SUDO[@]}" apt update
    APT_UPDATED=1
  fi
}

apt_package_installed() {
  local pkg="$1"
  dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "install ok installed"
}

join_by_space() {
  local first=1 item
  for item in "$@"; do
    if [[ "${first}" -eq 1 ]]; then
      printf '%s' "${item}"
      first=0
    else
      printf ' %s' "${item}"
    fi
  done
}

apt_install_missing() {
  local pkg
  local -a missing=()

  for pkg in "$@"; do
    if apt_package_installed "${pkg}"; then
      ok "$(printf "$(msg pkg_present)" "${pkg}")"
    else
      missing+=("${pkg}")
    fi
  done

  if ((${#missing[@]} == 0)); then
    ok "$(msg pkg_all_present)"
    return
  fi

  apt_update_once
  info "$(printf "$(msg pkg_installing)" "$(join_by_space "${missing[@]}")")"
  "${SUDO[@]}" apt install -y "${missing[@]}"
}

install_common_deps() {
  section section_deps

  local -a packages=(
    git
    wget
    curl
    ca-certificates
    tar
    gzip
    xz-utils
    cmake
    build-essential
    pkg-config
    make
    gcc
    g++
    default-libmysqlclient-dev
    libpugixml-dev
    libfmt-dev
    libssl-dev
    libspdlog-dev
    libmimalloc-dev
    libabsl-dev
    zlib1g-dev
    libasio-dev
  )

  apt_install_missing "${packages[@]}"
}

lua_header_declares_55() {
  local header="$1"

  awk '
    $1 == "#define" && $2 == "LUA_VERSION_MAJOR_N" { major = $3 }
    $1 == "#define" && $2 == "LUA_VERSION_MINOR_N" { minor = $3 }
    $1 == "#define" && $2 == "LUA_VERSION_NUM" { version_num = $3 }
    function leading_digits(value) {
      sub(/[^0-9].*$/, "", value)
      return value
    }
    END {
      major = leading_digits(major)
      minor = leading_digits(minor)
      version_num = leading_digits(version_num)

      if (major == "5" && minor == "5") {
        exit 0
      }
      if (version_num == "505") {
        exit 0
      }
      exit 1
    }
  ' "${header}"
}

lua_binary_is_55() {
  local lua_bin="${LUA_PREFIX}/bin/lua"
  local version=""

  [[ -x "${lua_bin}" ]] || return 1
  version="$("${lua_bin}" -v 2>&1 || true)"
  [[ "${version}" =~ ^Lua[[:space:]]5\.5(\.|[[:space:]]|$) ]]
}

lua_local_is_55() {
  local header="${LUA_PREFIX}/include/lua.h"
  local library="${LUA_PREFIX}/lib/liblua.a"

  [[ -f "${header}" && -f "${library}" ]] || return 1
  lua_header_declares_55 "${header}" || return 1
  lua_binary_is_55
}

lua_pkgconfig_dirs() {
  local multiarch=""

  printf '%s\n' "${LUA_PREFIX}/lib/pkgconfig"

  if command -v dpkg-architecture >/dev/null 2>&1; then
    multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || true)"
    if [[ -n "${multiarch}" ]]; then
      printf '%s\n' "${LUA_PREFIX}/lib/${multiarch}/pkgconfig"
    fi
  fi
}

prepend_lua_pkgconfig_path() {
  local dir new_path=""

  while IFS= read -r dir; do
    [[ -n "${dir}" ]] || continue
    case ":${PKG_CONFIG_PATH:-}:" in
      *":${dir}:"*) ;;
      *) new_path="${new_path:+${new_path}:}${dir}" ;;
    esac
  done < <(lua_pkgconfig_dirs)

  if [[ -n "${new_path}" ]]; then
    export PKG_CONFIG_PATH="${new_path}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  fi
}

ensure_lua_pkgconfig() {
  local pc_name="lua5.5.pc"
  local primary_pc_dir="${LUA_PREFIX}/lib/pkgconfig"
  local primary_pc_file="${primary_pc_dir}/${pc_name}"
  local dir pc_version

  prepend_lua_pkgconfig_path

  if [[ -f "${primary_pc_file}" && -e "${primary_pc_dir}/lua-5.5.pc" && -e "${primary_pc_dir}/lua.pc" ]] && command -v pkg-config >/dev/null 2>&1; then
    pc_version="$(pkg-config --modversion lua5.5 2>/dev/null || true)"
    if [[ "${pc_version}" == 5.5* ]]; then
      ok "$(printf "$(msg lua_pc_ok)" "${primary_pc_file}")"
      return
    fi
  fi

  "${SUDO[@]}" mkdir -p "${primary_pc_dir}"

  {
    printf 'prefix=%s\n' "${LUA_PREFIX}"
    printf 'exec_prefix=${prefix}\n'
    printf 'libdir=${exec_prefix}/lib\n'
    printf 'includedir=${prefix}/include\n'
    printf '\n'
    printf 'Name: Lua\n'
    printf 'Description: Lua language engine\n'
    printf 'Version: %s\n' "${LUA_VERSION}"
    printf 'Libs: -L${libdir} -llua -lm -ldl\n'
    printf 'Cflags: -I${includedir}\n'
  } | "${SUDO[@]}" tee "${primary_pc_file}" >/dev/null

  "${SUDO[@]}" ln -sf "${pc_name}" "${primary_pc_dir}/lua-5.5.pc"
  "${SUDO[@]}" ln -sf "${pc_name}" "${primary_pc_dir}/lua.pc"

  while IFS= read -r dir; do
    [[ -n "${dir}" && "${dir}" != "${primary_pc_dir}" ]] || continue
    "${SUDO[@]}" mkdir -p "${dir}"
    "${SUDO[@]}" ln -sf "${primary_pc_file}" "${dir}/${pc_name}"
    "${SUDO[@]}" ln -sf "${pc_name}" "${dir}/lua-5.5.pc"
    "${SUDO[@]}" ln -sf "${pc_name}" "${dir}/lua.pc"
  done < <(lua_pkgconfig_dirs)

  if command -v pkg-config >/dev/null 2>&1; then
    pc_version="$(pkg-config --modversion lua5.5 2>/dev/null || true)"
    [[ "${pc_version}" == 5.5* ]] || die "$(printf "$(msg lua_pc_verify_failed)" "${pc_version:-not found}")"
  fi

  ok "$(printf "$(msg lua_pc_ok)" "${primary_pc_file}")"
}

ensure_lua_alternatives() {
  if [[ -x "${LUA_PREFIX}/bin/lua" ]]; then
    "${SUDO[@]}" update-alternatives --install /usr/bin/lua lua "${LUA_PREFIX}/bin/lua" 100 >/dev/null 2>&1 || true
  fi

  if [[ -x "${LUA_PREFIX}/bin/luac" ]]; then
    "${SUDO[@]}" update-alternatives --install /usr/bin/luac luac "${LUA_PREFIX}/bin/luac" 100 >/dev/null 2>&1 || true
  fi
}

remove_old_lua_versions() {
  # Remove apt-managed Lua packages for versions other than 5.5
  local old_ver pkg
  local -a old_pkgs=()

  for old_ver in 5.1 5.2 5.3 5.4; do
    for pkg in \
      "lua${old_ver}" \
      "liblua${old_ver}-dev" \
      "liblua${old_ver}" \
      "lua${old_ver}-dev" \
      "lua-${old_ver}" \
      "liblua-${old_ver}-0" \
      "liblua-${old_ver}-0-dev"; do
      if apt_package_installed "${pkg}"; then
        old_pkgs+=("${pkg}")
      fi
    done
  done

  if ((${#old_pkgs[@]} > 0)); then
    info "Removendo versoes antigas de Lua: $(join_by_space "${old_pkgs[@]}")"
    apt_update_once
    "${SUDO[@]}" apt remove -y "${old_pkgs[@]}" || true
    "${SUDO[@]}" apt autoremove -y || true
  fi

  # Remove manually-installed Lua binaries for versions other than 5.5
  local bin_dir="${LUA_PREFIX}/bin"
  local bin_path
  for old_ver in 5.1 5.2 5.3 5.4; do
    for bin_path in "${bin_dir}/lua${old_ver}" "${bin_dir}/luac${old_ver}"; do
      if [[ -f "${bin_path}" ]]; then
        info "Removendo binario Lua antigo: ${bin_path}"
        "${SUDO[@]}" rm -f "${bin_path}"
      fi
    done
  done

  # Remove old alternatives pointing to non-5.5 binaries
  if command -v update-alternatives >/dev/null 2>&1; then
    local alt_path
    for old_ver in 5.1 5.2 5.3 5.4; do
      alt_path="${LUA_PREFIX}/bin/lua${old_ver}"
      "${SUDO[@]}" update-alternatives --remove lua "${alt_path}" >/dev/null 2>&1 || true
      alt_path="${LUA_PREFIX}/bin/luac${old_ver}"
      "${SUDO[@]}" update-alternatives --remove luac "${alt_path}" >/dev/null 2>&1 || true
      # also check /usr/bin paths
      "${SUDO[@]}" update-alternatives --remove lua "/usr/bin/lua${old_ver}" >/dev/null 2>&1 || true
      "${SUDO[@]}" update-alternatives --remove luac "/usr/bin/luac${old_ver}" >/dev/null 2>&1 || true
    done
  fi
}

install_lua_55() {
  sayf lua_install "${LUA_VERSION}"

  cd /tmp
  rm -rf "lua-${LUA_VERSION}" "${LUA_TARBALL}"

  wget -O "${LUA_TARBALL}" "${LUA_URL}"
  printf '%s  %s\n' "${LUA_SHA256}" "${LUA_TARBALL}" | sha256sum -c -

  tar -xzf "${LUA_TARBALL}"
  cd "lua-${LUA_VERSION}"

  make linux MYCFLAGS="-fPIC"
  "${SUDO[@]}" make install
  "${SUDO[@]}" ldconfig
  ensure_lua_pkgconfig
  ensure_lua_alternatives
}

ensure_lua_55() {
  section section_lua

  if lua_local_is_55; then
    ensure_lua_pkgconfig
    ensure_lua_alternatives
    ok "$(printf "$(msg lua_ok)" "${LUA_PREFIX}")"
    "${LUA_PREFIX}/bin/lua" -v || true
    return
  fi

  warn "$(msg lua_old)"
  remove_old_lua_versions
  install_lua_55

  if ! lua_local_is_55; then
    die "Lua ${LUA_VERSION} install verification failed"
  fi

  "${LUA_PREFIX}/bin/lua" -v || true
}

require_lua_for_configure() {
  if lua_local_is_55; then
    prepend_lua_pkgconfig_path
    return
  fi

  if [[ "${SKIP_DEPS}" -eq 1 ]]; then
    die "$(printf "$(msg lua_missing_skip_deps)" "${LUA_PREFIX}")"
  fi

  ensure_lua_55
}

simdutf_config_exists() {
  [[ -f "${SIMDUTF_PREFIX}/lib/cmake/simdutf/simdutf-config.cmake" ]] || \
    [[ -f "${SIMDUTF_PREFIX}/lib/cmake/simdutf/simdutfConfig.cmake" ]]
}

ensure_simdutf() {
  section section_simdutf

  if simdutf_config_exists; then
    ok "$(printf "$(msg simdutf_ok)" "${SIMDUTF_PREFIX}")"
    return
  fi

  sayf simdutf_install "${SIMDUTF_PREFIX}"
  mkdir -p "$(dirname "${SIMDUTF_DIR}")"

  if [[ -d "${SIMDUTF_DIR}/.git" ]]; then
    cd "${SIMDUTF_DIR}"
    git pull --ff-only || warn "$(msg simdutf_pull_warn)"
  else
    rm -rf "${SIMDUTF_DIR}"
    git clone https://github.com/simdutf/simdutf.git "${SIMDUTF_DIR}"
    cd "${SIMDUTF_DIR}"
  fi

  cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SIMDUTF_PREFIX}" \
    -DBUILD_SHARED_LIBS=OFF

  cmake --build build --parallel "${JOBS}"
  cmake --install build
}

mio_config_exists() {
  [[ -f "${SIMDUTF_PREFIX}/lib/cmake/mio/mio-config.cmake" ]] || \
    [[ -f "${SIMDUTF_PREFIX}/lib/cmake/mio/mioConfig.cmake" ]] || \
    [[ -f "${SIMDUTF_PREFIX}/share/cmake/mio/mioConfig.cmake" ]]
}

ensure_mio() {
  section section_mio

  if mio_config_exists; then
    ok "$(printf "$(msg mio_ok)" "${SIMDUTF_PREFIX}")"
    return
  fi

  sayf mio_install "${SIMDUTF_PREFIX}"
  mkdir -p "$(dirname "${MIO_DIR}")"

  if [[ -d "${MIO_DIR}/.git" ]]; then
    cd "${MIO_DIR}"
    git pull --ff-only || warn "$(msg mio_pull_warn)"
  else
    rm -rf "${MIO_DIR}"
    git clone https://github.com/mandreyel/mio.git "${MIO_DIR}"
    cd "${MIO_DIR}"
  fi

  cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SIMDUTF_PREFIX}"

  cmake --build build --parallel "${JOBS}"
  cmake --install build
}

prepare_repo() {
  section section_repo

  cd "${SCRIPT_DIR}"

  if [[ -f "CMakeLists.txt" ]]; then
    ok "$(printf "$(msg project_ok)" "$(pwd)")"
  else
    say project_clone
    git clone "${REPO_URL}"
    cd forgottenserver-downgrade-1.8-8.60
  fi

  if grep -q 'set(TFS_LUA_REQUIRED_VERSION "5.5")' CMakeLists.txt &&
    grep -q 'find_package(Lua "${TFS_LUA_REQUIRED_VERSION}" REQUIRED)' CMakeLists.txt; then
    info "$(msg project_summary)"
  else
    warn "$(msg project_lua_warn)"
  fi
}

absolute_path() {
  local path="$1"
  local dir base

  dir="$(dirname "${path}")"
  base="$(basename "${path}")"

  if [[ "${path}" = /* ]]; then
    if [[ -d "${dir}" ]]; then
      dir="$(cd "${dir}" && pwd -P)"
    fi
  else
    if [[ -d "${dir}" ]]; then
      dir="$(cd "${dir}" && pwd -P)"
    else
      dir="$(pwd -P)/${dir}"
    fi
  fi

  printf '%s/%s' "${dir}" "${base}"
}

safe_remove_build_dir() {
  local target="$1"
  local project_root target_abs
  project_root="$(pwd -P)"

  if [[ -d "${target}" ]]; then
    target_abs="$(cd "${target}" && pwd -P)"
  else
    mkdir -p "${target}"
    target_abs="$(cd "${target}" && pwd -P)"
    rmdir "${target}"
  fi

  case "${target_abs}" in
    "${project_root}"/*)
      sayf clean_build "${target_abs}"
      rm -rf -- "${target_abs}"
      ;;
    *)
      die "$(printf "$(msg safe_delete_refused)" "${target_abs}")"
      ;;
  esac
}

cmake_prefix_path() {
  local -a prefixes=("${LUA_PREFIX}" "${SIMDUTF_PREFIX}")

  local IFS=';'
  printf '%s' "${prefixes[*]}"
}

configure_tfs() {
  local prefix_path
  require_lua_for_configure
  prefix_path="$(cmake_prefix_path)"

  local -a args=(
    -S .
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DHTTP="${HTTP}"
    -DDISABLE_STATS=1
    -DENABLE_SLOW_TASK_DETECTION=OFF
    -DUSE_MIMALLOC="${USE_MIMALLOC}"
    -DLUA_INCLUDE_DIR="${LUA_PREFIX}/include"
    -DLUA_LIBRARY="${LUA_PREFIX}/lib/liblua.a"
    -DLUA_LIBRARIES="${LUA_PREFIX}/lib/liblua.a;m;dl"
    -DLUA_VERSION_STRING="${LUA_VERSION}"
    -DCMAKE_PREFIX_PATH="${prefix_path}"
  )

  if ! cmake -Wno-dev "${args[@]}"; then
    warn "$(msg configure_retry)"
    safe_remove_build_dir "${BUILD_DIR}"
    cmake -Wno-dev "${args[@]}"
  fi
}

verify_binary_links() {
  local bin="$1"
  if command -v ldd >/dev/null 2>&1; then
    if ldd "${bin}" | grep -q "not found"; then
      ldd "${bin}" || true
      die "$(msg ldd_missing)"
    fi
    ok "$(msg ldd_ok)"
  fi
}

build_tfs() {
  section section_build

  cd "${SCRIPT_DIR}"
  [[ -f "CMakeLists.txt" ]] || die "CMakeLists.txt not found"

  if [[ -z "${OUTPUT_BIN}" ]]; then
    OUTPUT_BIN="./tfs"
  fi
  OUTPUT_BIN="$(absolute_path "${OUTPUT_BIN}")"

  if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    safe_remove_build_dir "${BUILD_DIR}"
  fi

  configure_tfs
  cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

  if [[ ! -f "${BUILD_DIR}/tfs" ]]; then
    die "Build finished, but ${BUILD_DIR}/tfs was not found"
  fi

  local built_binary
  built_binary="$(absolute_path "${BUILD_DIR}/tfs")"

  mkdir -p "$(dirname "${OUTPUT_BIN}")"
  if [[ "${OUTPUT_BIN}" != "${built_binary}" ]]; then
    rm -f "${OUTPUT_BIN}"
    cp -f "${built_binary}" "${OUTPUT_BIN}"
  fi
  chmod +x "${OUTPUT_BIN}"

  verify_binary_links "${OUTPUT_BIN}"
  say build_done
  sayf binary_path "${OUTPUT_BIN}"
  sayf run_hint "$(dirname "${OUTPUT_BIN}")"
}

main() {
  parse_args "$@"
  choose_language

  if [[ -z "${JOBS}" ]]; then
    JOBS="$(nproc 2>/dev/null || printf '2')"
  fi

  banner
  choose_ubuntu
  preflight
  prepare_repo

  if [[ "${SKIP_DEPS}" -eq 1 ]]; then
    warn "$(msg skip_deps)"
  else
    install_common_deps
    ensure_lua_55
    ensure_simdutf
    ensure_mio
  fi

  if [[ "${SKIP_BUILD}" -eq 1 ]]; then
    warn "$(msg skip_build)"
  else
    build_tfs
  fi

  printf '\n%b%s%b\n' "${GREEN}${BOLD}" "$(msg done)" "${RESET}"
}

main "$@"
