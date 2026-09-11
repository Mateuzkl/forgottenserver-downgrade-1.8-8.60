#!/usr/bin/env bash
set -Eeuo pipefail

REPO_URL="https://github.com/Mateuzkl/forgottenserver-downgrade-1.8-8.60.git"

LUA_VERSION="5.5.0"
LUA_SOURCE_DIR="lua-${LUA_VERSION}"
LUA_TARBALL="${LUA_SOURCE_DIR}.tar.gz"
LUA_URL="https://www.lua.org/ftp/${LUA_TARBALL}"
LUA_SHA256="57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d"
LUA_PREFIX="/usr/local"

SIMDUTF_DIR="${HOME}/.cache/tfs-build/simdutf"
SIMDUTF_PREFIX="${HOME}/.local"
MIO_DIR="${HOME}/.cache/tfs-build/mio"

BUILD_DIR="${TFS_BUILD_DIR:-}"
OUTPUT_BIN=""
TARGET_DISTRO="${TFS_DISTRO_TARGET:-auto}"
TARGET_VERSION="auto"
if [[ -n "${TFS_DEBIAN_TARGET:-}" ]]; then
  TARGET_DISTRO="debian"
  TARGET_VERSION="${TFS_DEBIAN_TARGET}"
elif [[ -n "${TFS_UBUNTU_TARGET:-}" ]]; then
  TARGET_DISTRO="ubuntu"
  TARGET_VERSION="${TFS_UBUNTU_TARGET}"
fi
UI_LANG="${TFS_BUILD_LANG:-}"
JOBS="${JOBS:-}"
HTTP="ON"
USE_MIMALLOC="ON"
TFS_CXX_COMPILER=""
CLEAN_BUILD=0
SKIP_DEPS=0
SKIP_BUILD=0
NONINTERACTIVE=0
PORTABLE_DEBIAN=0

ZIG_VERSION="0.15.2"
CMAKE_PORTABLE_VERSION="3.31.12"
VCPKG_BASELINE="9e593bb18ea69cc5095e012465dcd675a822ed0d"
TOOLCHAIN_CACHE="${TFS_TOOLCHAIN_CACHE:-${HOME}/.cache/tfs-build/toolchains}"
VCPKG_ROOT="${TFS_VCPKG_ROOT:-${HOME}/.cache/tfs-build/vcpkg}"
TFS_VCPKG_TRIPLET=""

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LUA_PATCH_FILE="${SCRIPT_DIR}/tools/patches/lua-5.5.0-write-barrier.patch"
APT_UPDATED=0
SUDO=()
APT_OPTIONS=()

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
  [banner_subtitle]="Debian/Ubuntu/WSL, dependencias, Lua 5.5, Asio, mio e CMake"
  [choose_lang]="Escolha o idioma:"
  [language_set]="Idioma: Portugues"
  [detect_system]="Sistema detectado"
  [wsl_detected]="WSL detectado"
  [choose_ubuntu]="Escolha a versao do Ubuntu para preparar o build:"
  [ubuntu_prompt]="Digite 1, 2 ou 3 [detectado: %s]: "
  [invalid_option]="opcao invalida"
  [using_ubuntu]="Usando configuracao para Ubuntu %s"
  [using_platform]="Usando configuracao para %s %s"
  [section_preflight]="Verificando ambiente"
  [section_deps]="Verificando dependencias apt"
  [section_lua]="Verificando Lua 5.5"
  [section_simdutf]="Verificando simdutf"
  [section_repo]="Verificando projeto TFS"
  [section_build]="Compilando TFS em Release"
  [need_ubuntu]="Este script foi feito para Ubuntu 22.04/24.04/26.04 com apt/dpkg."
  [need_platform]="Este script suporta Debian 11/12/13 e Ubuntu 22.04/24.04/26.04 com apt/dpkg."
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
  [compiler_selected]="compilador selecionado para %s: %s"
  [compiler_missing]="Ubuntu 22.04 precisa de %s para C++23/std::move_only_function. Rode ./build.sh sem --skip-deps para instalar as dependencias."
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
  [run_hint]="Para rodar: cd %s && ./%s"
  [done]="Tudo pronto."
  [fail_line]="Falha na linha %s com codigo %s."
  [safe_delete_refused]="Recusei remover caminho fora do projeto: %s"
  [skip_deps]="Pulando instalacao/verificacao de dependencias por opcao do usuario."
  [skip_build]="Pulando build por opcao do usuario."
)

declare -A MSG_EN=(
  [banner_title]="TFS 1.8 - 8.60 build assistant"
  [banner_subtitle]="Debian/Ubuntu/WSL, dependencies, Lua 5.5, Asio, mio and CMake"
  [choose_lang]="Choose language:"
  [language_set]="Language: English"
  [detect_system]="Detected system"
  [wsl_detected]="WSL detected"
  [choose_ubuntu]="Choose the Ubuntu version to prepare the build:"
  [ubuntu_prompt]="Type 1, 2 or 3 [detected: %s]: "
  [invalid_option]="invalid option"
  [using_ubuntu]="Using Ubuntu %s configuration"
  [using_platform]="Using %s %s configuration"
  [section_preflight]="Checking environment"
  [section_deps]="Checking apt dependencies"
  [section_lua]="Checking Lua 5.5"
  [section_simdutf]="Checking simdutf"
  [section_repo]="Checking TFS project"
  [section_build]="Building TFS Release"
  [need_ubuntu]="This script is intended for Ubuntu 22.04/24.04/26.04 with apt/dpkg."
  [need_platform]="This script supports Debian 11/12/13 and Ubuntu 22.04/24.04/26.04 with apt/dpkg."
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
  [compiler_selected]="selected compiler for %s: %s"
  [compiler_missing]="Ubuntu 22.04 requires %s for C++23/std::move_only_function. Run ./build.sh without --skip-deps so dependencies can be installed."
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
  [run_hint]="To run: cd %s && ./%s"
  [done]="All done."
  [fail_line]="Failed at line %s with exit code %s."
  [safe_delete_refused]="Refused to remove path outside project: %s"
  [skip_deps]="Skipping dependency install/check by user option."
  [skip_build]="Skipping build by user option."
)

declare -A MSG_ES=(
  [banner_title]="Asistente de build TFS 1.8 - 8.60"
  [banner_subtitle]="Debian/Ubuntu/WSL, dependencias, Lua 5.5, Asio, mio y CMake"
  [choose_lang]="Elige el idioma:"
  [language_set]="Idioma: Espanol"
  [detect_system]="Sistema detectado"
  [wsl_detected]="WSL detectado"
  [choose_ubuntu]="Elige la version de Ubuntu para preparar el build:"
  [ubuntu_prompt]="Escribe 1, 2 o 3 [detectado: %s]: "
  [invalid_option]="opcion invalida"
  [using_ubuntu]="Usando configuracion para Ubuntu %s"
  [using_platform]="Usando configuracion para %s %s"
  [section_preflight]="Verificando entorno"
  [section_deps]="Verificando dependencias apt"
  [section_lua]="Verificando Lua 5.5"
  [section_simdutf]="Verificando simdutf"
  [section_repo]="Verificando proyecto TFS"
  [section_build]="Compilando TFS Release"
  [need_ubuntu]="Este script fue hecho para Ubuntu 22.04/24.04/26.04 con apt/dpkg."
  [need_platform]="Este script soporta Debian 11/12/13 y Ubuntu 22.04/24.04/26.04 con apt/dpkg."
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
  [compiler_selected]="compilador seleccionado para %s: %s"
  [compiler_missing]="Ubuntu 22.04 necesita %s para C++23/std::move_only_function. Ejecuta ./build.sh sin --skip-deps para instalar las dependencias."
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
  [run_hint]="Para ejecutar: cd %s && ./%s"
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
  --debian 11|12|13      Select Debian dependency strategy
  --ubuntu 22.04|24.04|26.04
                          Select Ubuntu dependency strategy
  --jobs N               Parallel build jobs
  --build-dir PATH       CMake build directory
  --clean                Remove the selected build directory before configuring
  --output PATH          Copy the final binary to PATH (default: target-specific)
  --http on|off          Configure CMake HTTP option (default: on)
  --no-mimalloc          Disable mimalloc in CMake
  --skip-deps            Do not install/check dependencies
  --skip-build           Install/check dependencies only
  --non-interactive      Use detected/default choices
  -h, --help             Show this help

Environment:
  TFS_BUILD_LANG=pt|en|es
  TFS_DISTRO_TARGET=debian|ubuntu
  TFS_DEBIAN_TARGET=11|12|13
  TFS_UBUNTU_TARGET=22.04|24.04|26.04
  TFS_BUILD_DIR=PATH
  TFS_TOOLCHAIN_CACHE=PATH
  TFS_VCPKG_ROOT=PATH
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
        TARGET_DISTRO="ubuntu"
        TARGET_VERSION="$2"
        shift 2
        ;;
      --debian)
        [[ $# -ge 2 ]] || die "--debian requires a value"
        TARGET_DISTRO="debian"
        TARGET_VERSION="$2"
        shift 2
        ;;
      --jobs)
        [[ $# -ge 2 ]] || die "--jobs requires a value"
        JOBS="$2"
        shift 2
        ;;
      --build-dir)
        [[ $# -ge 2 ]] || die "--build-dir requires a value"
        BUILD_DIR="$2"
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

detect_os_version() {
  if [[ -f /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    printf '%s' "${VERSION_ID:-unknown}"
  else
    printf 'unknown'
  fi
}

choose_platform() {
  local detected_id detected_version
  detected_id="$(detect_os_id)"
  detected_version="$(detect_os_version)"

  info "$(msg detect_system): ${detected_id} ${detected_version}"
  info "$(msg wsl_detected): $(detect_wsl)"

  if [[ "${TARGET_DISTRO}" == "auto" ]]; then
    TARGET_DISTRO="${detected_id}"
  fi
  if [[ "${TARGET_VERSION}" == "auto" ]]; then
    TARGET_VERSION="${detected_version}"
  fi

  case "${TARGET_DISTRO}:${TARGET_VERSION}" in
    debian:11|debian:12|debian:13|ubuntu:22.04|ubuntu:24.04|ubuntu:26.04) ;;
    *)
      die "Unsupported target ${TARGET_DISTRO} ${TARGET_VERSION} on detected host ${detected_id} ${detected_version}. $(msg need_platform)"
      ;;
  esac

  if [[ "${TARGET_DISTRO}" != "${detected_id}" || "${TARGET_VERSION}" != "${detected_version}" ]]; then
    die "Selected ${TARGET_DISTRO} ${TARGET_VERSION}, but this host is ${detected_id} ${detected_version}. Run each target inside its matching WSL distribution or container."
  fi

  if [[ -z "${BUILD_DIR}" ]]; then
    if [[ "${TARGET_DISTRO}" == "debian" ]]; then
      BUILD_DIR="build-release-debian-${TARGET_VERSION}"
    else
      BUILD_DIR="build-release"
    fi
  fi

  if [[ "${TARGET_DISTRO}:${TARGET_VERSION}" == "debian:11" || "${TARGET_DISTRO}:${TARGET_VERSION}" == "debian:12" ]]; then
    PORTABLE_DEBIAN=1
  fi

  if [[ "${TARGET_DISTRO}:${TARGET_VERSION}" == "debian:11" ]]; then
    APT_OPTIONS=(
      -o "Dir::Etc::sourcelist=${SCRIPT_DIR}/tools/apt/debian-11-archive.list"
      -o "Dir::Etc::sourceparts=-"
      -o "Acquire::Check-Valid-Until=false"
    )
  fi

  sayf using_platform "${TARGET_DISTRO^}" "${TARGET_VERSION}"
}

init_sudo() {
  if [[ "${EUID}" -eq 0 ]]; then
    SUDO=()
  else
    command -v sudo >/dev/null 2>&1 || die "$(msg need_sudo)"
    SUDO=(sudo)
  fi
}

require_apt_tools() {
  command -v apt-get >/dev/null 2>&1 || die "$(msg need_platform)"
  command -v dpkg-query >/dev/null 2>&1 || die "$(msg need_ubuntu)"
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
  require_apt_tools
  init_sudo

  local cmake_version gcc_version gxx_version
  cmake_version="$(tool_version cmake)"
  gcc_version="$(tool_version gcc)"
  gxx_version="$(tool_version g++)"

  sayf tool_version "cmake" "${cmake_version}"
  sayf tool_version "gcc" "${gcc_version}"
  sayf tool_version "g++" "${gxx_version}"

  if [[ "${PORTABLE_DEBIAN}" -eq 0 && "${cmake_version}" != "missing" ]] && ! version_ge "${cmake_version}" "3.20"; then
    die "$(printf "$(msg cmake_too_old)" "${cmake_version}")"
  fi
}

apt_update_once() {
  if [[ "${APT_UPDATED}" -eq 0 ]]; then
    info "$(msg apt_update)"
    "${SUDO[@]}" apt-get "${APT_OPTIONS[@]}" update
    APT_UPDATED=1
  fi
}

apt_package_installed() {
  local pkg="$1"
  dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "install ok installed"
}

mysql_client_dev_installed() {
  apt_package_installed default-libmysqlclient-dev ||
    apt_package_installed libmysqlclient-dev ||
    { apt_package_installed libmariadb-dev && apt_package_installed libmariadb-dev-compat; }
}

ensure_mysql_client_dev() {
  if mysql_client_dev_installed; then
    ok "$(printf "$(msg pkg_present)" "mysql/mariadb client dev")"
    return
  fi

  apt_install_missing libmariadb-dev libmariadb-dev-compat
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
  "${SUDO[@]}" apt-get "${APT_OPTIONS[@]}" install -y "${missing[@]}"
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
    patch
    pkg-config
    make
    gcc
    g++
    libpugixml-dev
    libfmt-dev
    libssl-dev
    libspdlog-dev
    libmimalloc-dev
    libabsl-dev
    zlib1g-dev
    libasio-dev
  )

  if [[ "${TARGET_DISTRO}:${TARGET_VERSION}" == "ubuntu:22.04" ]]; then
    packages+=(
      gcc-12
      g++-12
    )
  fi

  apt_install_missing "${packages[@]}"
  ensure_mysql_client_dev
}

install_portable_debian_bootstrap() {
  section section_deps
  apt_install_missing \
    autoconf autoconf-archive automake bison build-essential ca-certificates curl file flex \
    git gzip libtool linux-libc-dev make ninja-build patch perl pkg-config python3 tar unzip xz-utils zip
}

download_verified() {
  local url="$1"
  local sha256="$2"
  local destination="$3"

  mkdir -p "$(dirname "${destination}")"
  if [[ -f "${destination}" ]] && printf '%s  %s\n' "${sha256}" "${destination}" | sha256sum -c - >/dev/null 2>&1; then
    return
  fi

  rm -f -- "${destination}"
  curl --fail --show-error --location --retry 5 --retry-all-errors --retry-delay 3 \
    --connect-timeout 30 --max-time 900 --output "${destination}" "${url}"
  printf '%s  %s\n' "${sha256}" "${destination}" | sha256sum -c -
}

portable_arch() {
  case "$(uname -m)" in
    x86_64|amd64) printf 'x86_64' ;;
    aarch64|arm64) printf 'aarch64' ;;
    *) die "Portable Debian builds support x86_64 and aarch64; found $(uname -m)." ;;
  esac
}

ensure_portable_cmake() {
  local arch cmake_arch sha256 archive install_dir
  arch="$(portable_arch)"
  install_dir="${TOOLCHAIN_CACHE}/cmake-${CMAKE_PORTABLE_VERSION}-${arch}"

  if [[ "${arch}" == "x86_64" ]]; then
    cmake_arch="x86_64"
    sha256="0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c"
  else
    cmake_arch="aarch64"
    sha256="83f8fd91d2038a56556e1400390fcfe42f79602940c494f6c6f1cdae7f9e7f40"
  fi

  if [[ ! -x "${install_dir}/bin/cmake" ]]; then
    [[ ! -e "${install_dir}" ]] || die "Incomplete CMake toolchain at ${install_dir}; remove that directory and retry."
    archive="${TOOLCHAIN_CACHE}/downloads/cmake-${CMAKE_PORTABLE_VERSION}-linux-${cmake_arch}.tar.gz"
    download_verified \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_PORTABLE_VERSION}/cmake-${CMAKE_PORTABLE_VERSION}-linux-${cmake_arch}.tar.gz" \
      "${sha256}" "${archive}"
    mkdir -p "${install_dir}"
    tar -xzf "${archive}" --strip-components=1 -C "${install_dir}"
  fi

  export PATH="${install_dir}/bin:${PATH}"
}

ensure_portable_zig() {
  local arch zig_arch sha256 archive install_dir
  arch="$(portable_arch)"
  install_dir="${TOOLCHAIN_CACHE}/zig-${ZIG_VERSION}-${arch}"

  if [[ "${arch}" == "x86_64" ]]; then
    zig_arch="x86_64"
    sha256="02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239"
    TFS_VCPKG_TRIPLET="x64-linux-tfs-zig"
  else
    zig_arch="aarch64"
    sha256="958ed7d1e00d0ea76590d27666efbf7a932281b3d7ba0c6b01b0ff26498f667f"
    TFS_VCPKG_TRIPLET="arm64-linux-tfs-zig"
  fi

  if [[ ! -x "${install_dir}/zig" ]]; then
    [[ ! -e "${install_dir}" ]] || die "Incomplete Zig toolchain at ${install_dir}; remove that directory and retry."
    archive="${TOOLCHAIN_CACHE}/downloads/zig-${zig_arch}-linux-${ZIG_VERSION}.tar.xz"
    download_verified "https://ziglang.org/download/${ZIG_VERSION}/zig-${zig_arch}-linux-${ZIG_VERSION}.tar.xz" \
      "${sha256}" "${archive}"
    mkdir -p "${install_dir}"
    tar -xJf "${archive}" --strip-components=1 -C "${install_dir}"
  fi

  export TFS_ZIG_BIN="${install_dir}/zig"
  chmod +x "${SCRIPT_DIR}"/tools/toolchains/zig-*
}

ensure_vcpkg() {
  mkdir -p "$(dirname "${VCPKG_ROOT}")"
  if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
    [[ ! -e "${VCPKG_ROOT}" ]] || die "${VCPKG_ROOT} exists but is not a vcpkg checkout."
    git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
  fi

  git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}"
  git -C "${VCPKG_ROOT}" checkout --detach "${VCPKG_BASELINE}"
  "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
}

ensure_portable_debian_toolchain() {
  ensure_portable_cmake
  ensure_portable_zig
  ensure_vcpkg
  cmake --version | head -n 1
  "${TFS_ZIG_BIN}" version
  "${VCPKG_ROOT}/vcpkg" version | head -n 1
}

activate_portable_debian_toolchain() {
  local arch
  arch="$(portable_arch)"
  export TFS_ZIG_BIN="${TOOLCHAIN_CACHE}/zig-${ZIG_VERSION}-${arch}/zig"
  export PATH="${TOOLCHAIN_CACHE}/cmake-${CMAKE_PORTABLE_VERSION}-${arch}/bin:${PATH}"
  if [[ "${arch}" == "x86_64" ]]; then
    TFS_VCPKG_TRIPLET="x64-linux-tfs-zig"
  else
    TFS_VCPKG_TRIPLET="arm64-linux-tfs-zig"
  fi

  [[ -x "${TFS_ZIG_BIN}" && -x "${VCPKG_ROOT}/vcpkg" ]] ||
    die "Portable Debian dependencies are missing. Run without --skip-deps once."
  command -v cmake >/dev/null 2>&1 || die "Portable CMake is missing. Run without --skip-deps once."
  chmod +x "${SCRIPT_DIR}"/tools/toolchains/zig-*
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

lua_runtime_has_required_fixes() {
  local lua_bin="${LUA_PREFIX}/bin/lua"

  [[ -x "${lua_bin}" ]] || return 1
  "${lua_bin}" -e '
local parent = {}
parent.__newindex = parent
collectgarbage()
local child = setmetatable({}, parent)
child.__newindex = {x = "hello"}
collectgarbage("step")
assert(parent.__newindex.x == "hello")
' >/dev/null 2>&1
}

lua_local_is_55() {
  local header="${LUA_PREFIX}/include/lua.h"
  local library="${LUA_PREFIX}/lib/liblua.a"

  [[ -f "${header}" && -f "${library}" ]] || return 1
  lua_header_declares_55 "${header}" || return 1
  lua_binary_is_55 || return 1
  lua_runtime_has_required_fixes
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
  local current_lua=""
  local current_luac=""
  local wanted_lua=""
  local wanted_luac=""

  current_lua="$(readlink -f /usr/bin/lua 2>/dev/null || true)"
  current_luac="$(readlink -f /usr/bin/luac 2>/dev/null || true)"
  wanted_lua="$(readlink -f "${LUA_PREFIX}/bin/lua" 2>/dev/null || true)"
  wanted_luac="$(readlink -f "${LUA_PREFIX}/bin/luac" 2>/dev/null || true)"

  if [[ -n "${wanted_lua}" && "${current_lua}" != "${wanted_lua}" ]]; then
    "${SUDO[@]}" update-alternatives --install /usr/bin/lua lua "${LUA_PREFIX}/bin/lua" 100 >/dev/null 2>&1 || true
  fi

  if [[ -n "${wanted_luac}" && "${current_luac}" != "${wanted_luac}" ]]; then
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
    "${SUDO[@]}" apt-get "${APT_OPTIONS[@]}" remove -y "${old_pkgs[@]}" || true
    "${SUDO[@]}" apt-get "${APT_OPTIONS[@]}" autoremove -y || true
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
  rm -rf "${LUA_SOURCE_DIR}" "${LUA_TARBALL}"

  wget -O "${LUA_TARBALL}" "${LUA_URL}"
  printf '%s  %s\n' "${LUA_SHA256}" "${LUA_TARBALL}" | sha256sum -c -

  tar -xzf "${LUA_TARBALL}"
  cd "${LUA_SOURCE_DIR}"

  patch -d src -p1 < "${LUA_PATCH_FILE}"
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
    -DBUILD_SHARED_LIBS=OFF \
    -DSIMDUTF_TESTS=OFF \
    -DSIMDUTF_TOOLS=OFF

  cmake --build build --parallel "${JOBS}"
  cmake --install build
}

mio_config_exists() {
  [[ -f "${SIMDUTF_PREFIX}/lib/cmake/mio/mio-config.cmake" ]] || \
    [[ -f "${SIMDUTF_PREFIX}/lib/cmake/mio/mioConfig.cmake" ]] || \
    [[ -f "${SIMDUTF_PREFIX}/share/cmake/mio/mio-config.cmake" ]] || \
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
    -DCMAKE_INSTALL_PREFIX="${SIMDUTF_PREFIX}" \
    -DBUILD_TESTING=OFF

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

select_cxx_compiler() {
  TFS_CXX_COMPILER=""

  if [[ "${TARGET_DISTRO}:${TARGET_VERSION}" != "ubuntu:22.04" ]]; then
    return
  fi

  TFS_CXX_COMPILER="$(command -v g++-12 || true)"
  [[ -n "${TFS_CXX_COMPILER}" ]] || die "$(printf "$(msg compiler_missing)" "g++-12")"

  info "$(printf "$(msg compiler_selected)" "Ubuntu 22.04" "${TFS_CXX_COMPILER}")"
}

configure_portable_tfs() {
  local -a args=(
    -S .
    -B "${BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_CXX_COMPILER="${SCRIPT_DIR}/tools/toolchains/zig-cxx"
    -DCMAKE_AR="${SCRIPT_DIR}/tools/toolchains/zig-ar"
    -DCMAKE_RANLIB="${SCRIPT_DIR}/tools/toolchains/zig-ranlib"
    -DCMAKE_CXX_COMPILER_AR="${SCRIPT_DIR}/tools/toolchains/zig-ar"
    -DCMAKE_CXX_COMPILER_RANLIB="${SCRIPT_DIR}/tools/toolchains/zig-ranlib"
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_OVERLAY_TRIPLETS="${SCRIPT_DIR}/tools/triplets"
    -DVCPKG_TARGET_TRIPLET="${TFS_VCPKG_TRIPLET}"
    -DHTTP="${HTTP}"
    -DDISABLE_STATS=1
    -DENABLE_NATIVE_OPTIMIZATIONS=OFF
    -DENABLE_SLOW_TASK_DETECTION=OFF
    -DUSE_MIMALLOC=OFF
  )

  TFS_CXX_COMPILER="${SCRIPT_DIR}/tools/toolchains/zig-cxx"
  reset_build_dir_if_compiler_changed

  if ! cmake -Wno-dev "${args[@]}"; then
    warn "$(msg configure_retry)"
    safe_remove_build_dir "${BUILD_DIR}"
    cmake -Wno-dev "${args[@]}"
  fi
}

reset_build_dir_if_compiler_changed() {
  local cache_file="${BUILD_DIR}/CMakeCache.txt"
  local cached_compiler=""

  [[ -n "${TFS_CXX_COMPILER}" && -f "${cache_file}" ]] || return 0

  cached_compiler="$(awk -F= '$1 ~ /^CMAKE_CXX_COMPILER:/ { print $2; exit }' "${cache_file}")"
  if [[ -n "${cached_compiler}" && "${cached_compiler}" != "${TFS_CXX_COMPILER}" ]]; then
    safe_remove_build_dir "${BUILD_DIR}"
  fi
}

configure_tfs() {
  local prefix_path
  local -a compiler_args=()

  if [[ "${PORTABLE_DEBIAN}" -eq 1 ]]; then
    configure_portable_tfs
    return
  fi

  require_lua_for_configure
  prefix_path="$(cmake_prefix_path)"
  select_cxx_compiler
  reset_build_dir_if_compiler_changed

  if [[ -n "${TFS_CXX_COMPILER}" ]]; then
    compiler_args+=("-DCMAKE_CXX_COMPILER=${TFS_CXX_COMPILER}")
  fi

  local -a args=(
    -S .
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    "${compiler_args[@]}"
    -DHTTP="${HTTP}"
    -DDISABLE_STATS=1
    -DENABLE_SLOW_TASK_DETECTION=OFF
    -DUSE_MIMALLOC="${USE_MIMALLOC}"
    -DLUA_INCLUDE_DIR="${LUA_PREFIX}/include"
    -DLUA_LIBRARY="${LUA_PREFIX}/lib/liblua.a"
    -DLUA_LIBRARIES="${LUA_PREFIX}/lib/liblua.a;m;dl"
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
    if [[ "${TARGET_DISTRO}" == "debian" ]]; then
      OUTPUT_BIN="./tfs-debian-${TARGET_VERSION}"
    else
      OUTPUT_BIN="./tfs"
    fi
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
  sayf run_hint "$(dirname "${OUTPUT_BIN}")" "$(basename "${OUTPUT_BIN}")"
}

main() {
  parse_args "$@"
  choose_language

  if [[ -z "${JOBS}" ]]; then
    JOBS="$(nproc 2>/dev/null || printf '2')"
  fi

  banner
  choose_platform
  preflight
  prepare_repo

  if [[ "${SKIP_DEPS}" -eq 1 ]]; then
    warn "$(msg skip_deps)"
    if [[ "${PORTABLE_DEBIAN}" -eq 1 && "${SKIP_BUILD}" -eq 0 ]]; then
      activate_portable_debian_toolchain
    fi
  elif [[ "${PORTABLE_DEBIAN}" -eq 1 ]]; then
    install_portable_debian_bootstrap
    ensure_portable_debian_toolchain
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
