#!/usr/bin/env bash
set -Eeuo pipefail

# Instalador de MyAAC para TFS 1.8 Downgrade, protocolo 8.60.
# Sistemas suportados: Ubuntu 22.04, 24.04 e 26.04.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TFS_DIR="${TFS_DIR:-${SCRIPT_DIR}}"
WEB_ROOT="${MYAAC_WEB_ROOT:-/var/www/myaac}"
MYAAC_RELEASE="${MYAAC_VERSION:-latest}"
DOMAIN="${MYAAC_DOMAIN:-_}"
ADMIN_IP="${MYAAC_ADMIN_IP:-}"
DB_NAME="${TFS_DB_NAME:-forgottenserver}"
DB_USER="${TFS_DB_USER:-forgottenserver}"
DB_PASSWORD="${TFS_DB_PASSWORD:-}"
MARIADB_ROOT_PASSWORD="${MARIADB_ROOT_PASSWORD:-}"
DB_NAME_EXPLICIT=0
DB_USER_EXPLICIT=0
DB_PASSWORD_EXPLICIT=0
[[ -z "${TFS_DB_NAME:-}" ]] || DB_NAME_EXPLICIT=1
[[ -z "${TFS_DB_USER:-}" ]] || DB_USER_EXPLICIT=1
[[ -z "${TFS_DB_PASSWORD:-}" ]] || DB_PASSWORD_EXPLICIT=1
SITE_NAME="tfs18-myaac"
LOG_FILE="${TMPDIR:-/tmp}/tfs18-myaac-install-${EUID}.log"

MODE="menu"
ASSUME_YES=0
FORCE_DEPLOY=0
WITH_PHPMYADMIN=0
PHP_VERSION=""
PHP_FPM_SERVICE=""
PHP_FPM_SOCKET=""
MYAAC_TAG=""
MYAAC_URL=""
MYAAC_SHA256=""
BACKUP_PATH=""
TMP_DIR=""
MARIADB_ADMIN_CONFIG=""
MARIADB_ADMIN_MODE=""
SUDO=()

if [[ -t 1 ]]; then
  RED=$'\033[31m'
  GREEN=$'\033[32m'
  YELLOW=$'\033[33m'
  BLUE=$'\033[34m'
  BOLD=$'\033[1m'
  RESET=$'\033[0m'
else
  RED=""
  GREEN=""
  YELLOW=""
  BLUE=""
  BOLD=""
  RESET=""
fi

info() { printf '%b[INFO]%b %s\n' "${BLUE}" "${RESET}" "$*"; }
ok() { printf '%b[OK]%b %s\n' "${GREEN}" "${RESET}" "$*"; }
warn() { printf '%b[AVISO]%b %s\n' "${YELLOW}" "${RESET}" "$*"; }
fail() { printf '%b[ERRO]%b %s\n' "${RED}" "${RESET}" "$*" >&2; }
die() { fail "$*"; exit 1; }

log() {
  printf '%s %s\n' "$(date -Is)" "$*" >>"${LOG_FILE}" 2>/dev/null || true
}

section() {
  printf '\n%b%s%b\n' "${BOLD}${BLUE}" "$*" "${RESET}"
  printf '%s\n' '------------------------------------------------------------'
}

cleanup() {
  if [[ -n "${TMP_DIR}" && -d "${TMP_DIR}" ]]; then
    case "${TMP_DIR}" in
      /tmp/tfs18-myaac.*|"${TMPDIR:-/tmp}"/tfs18-myaac.*)
        rm -rf -- "${TMP_DIR}"
        ;;
    esac
  fi
}

on_error() {
  local code=$?
  local line="${BASH_LINENO[0]:-?}"
  fail "Falha na linha ${line}, código ${code}. Log: ${LOG_FILE}"
  log "Falha na linha ${line}, código ${code}"
  exit "${code}"
}

trap cleanup EXIT
trap on_error ERR

usage() {
  cat <<'EOF'
Uso: ./install-tfs18-myaac.sh [opções]

Sem opções, mostra o diagnóstico e inicia o assistente interativo.

Modos:
  --check                 Apenas diagnosticar; não altera a máquina
  --install               Instalar/configurar a pilha completa
  --lock-installer        Bloquear /install após concluir o MyAAC no navegador

Opções:
  --tfs-dir CAMINHO       Diretório do TFS 1.8 (padrão: diretório do script)
  --domain DOMÍNIO        server_name do Nginx (padrão: _)
  --admin-ip IP           IP autorizado a abrir /install
  --db-name NOME          Banco do TFS (padrão: forgottenserver)
  --db-user USUÁRIO       Usuário do banco (padrão: forgottenserver)
  --with-phpmyadmin       Instalar phpMyAdmin apenas em 127.0.0.1:2344
  --force                 Atualizar MyAAC mesmo se a mesma versão existir
  -y, --yes               Confirmar ações automaticamente
  -h, --help              Mostrar esta ajuda

Variáveis opcionais:
  TFS_DB_PASSWORD         Senha do banco; se vazia, gera uma senha segura
  MARIADB_ROOT_PASSWORD   Senha administrativa, usada somente se o acesso
                          root por unix_socket não estiver disponível
  MYAAC_VERSION           Tag específica, por exemplo v1.9.1 (padrão: latest)
  MYAAC_WEB_ROOT          Diretório web (padrão: /var/www/myaac)
EOF
}

need_value() {
  [[ $# -ge 2 && -n "$2" ]] || die "$1 exige um valor"
}

parse_args() {
  while (($#)); do
    case "$1" in
      --check) MODE="check"; shift ;;
      --install) MODE="install"; shift ;;
      --lock-installer) MODE="lock"; shift ;;
      --tfs-dir) need_value "$@"; TFS_DIR="$2"; shift 2 ;;
      --domain) need_value "$@"; DOMAIN="$2"; shift 2 ;;
      --admin-ip) need_value "$@"; ADMIN_IP="$2"; shift 2 ;;
      --db-name) need_value "$@"; DB_NAME="$2"; DB_NAME_EXPLICIT=1; shift 2 ;;
      --db-user) need_value "$@"; DB_USER="$2"; DB_USER_EXPLICIT=1; shift 2 ;;
      --with-phpmyadmin) WITH_PHPMYADMIN=1; shift ;;
      --force) FORCE_DEPLOY=1; shift ;;
      -y|--yes) ASSUME_YES=1; shift ;;
      -h|--help) usage; exit 0 ;;
      *) die "Opção desconhecida: $1" ;;
    esac
  done
}

banner() {
  printf '\n%bTFS 1.8 + MyAAC%b\n' "${BOLD}${BLUE}" "${RESET}"
  printf 'Ubuntu 22.04/24.04/26.04 | PHP-FPM | Nginx | MariaDB\n'
  printf 'Projeto: TFS 1.8 Downgrade, protocolo 8.60\n'
}

command_exists() { command -v "$1" >/dev/null 2>&1; }

detect_ubuntu() {
  [[ -r /etc/os-release ]] || die "/etc/os-release não encontrado"
  # shellcheck disable=SC1091
  source /etc/os-release
  [[ "${ID:-}" == "ubuntu" ]] || die "Sistema não suportado: ${ID:-desconhecido}. Use Ubuntu."

  case "${VERSION_ID:-}" in
    22.04|24.04|26.04) ;;
    *) die "Ubuntu ${VERSION_ID:-desconhecido} não suportado. Use 22.04, 24.04 ou 26.04." ;;
  esac

  UBUNTU_NAME="${PRETTY_NAME:-Ubuntu ${VERSION_ID}}"
}

resolve_tfs_dir() {
  [[ -d "${TFS_DIR}" ]] || die "Diretório do TFS não existe: ${TFS_DIR}"
  TFS_DIR="$(cd -- "${TFS_DIR}" && pwd -P)"
}

validate_tfs() {
  local definitions="${TFS_DIR}/src/definitions.h"

  [[ -f "${TFS_DIR}/CMakeLists.txt" ]] || die "CMakeLists.txt não encontrado em ${TFS_DIR}"
  [[ -f "${TFS_DIR}/schema.sql" ]] || die "schema.sql não encontrado em ${TFS_DIR}"
  [[ -f "${TFS_DIR}/config.lua" || -f "${TFS_DIR}/config.lua.dist" ]] ||
    die "config.lua/config.lua.dist não encontrado em ${TFS_DIR}"
  [[ -f "${definitions}" ]] || die "src/definitions.h não encontrado"

  grep -Eq 'STATUS_SERVER_VERSION[[:space:]]*=[[:space:]]*"1\.8"' "${definitions}" ||
    die "O diretório informado não foi identificado como TFS 1.8"
  grep -Eq 'CLIENT_VERSION_(MIN|MAX)[[:space:]]*=[[:space:]]*860' "${definitions}" ||
    die "O TFS informado não está configurado para o protocolo 8.60"
}

validate_settings() {
  [[ "${DB_NAME}" =~ ^[A-Za-z0-9_]+$ ]] || die "Nome de banco inválido: ${DB_NAME}"
  [[ "${DB_USER}" =~ ^[A-Za-z0-9_]+$ ]] || die "Usuário de banco inválido: ${DB_USER}"
  [[ "${DOMAIN}" == "_" || "${DOMAIN}" =~ ^[A-Za-z0-9.-]+$ ]] || die "Domínio inválido: ${DOMAIN}"

  [[ "${WEB_ROOT}" == /* ]] || die "MYAAC_WEB_ROOT precisa ser um caminho absoluto"
  case "${WEB_ROOT}" in
    /|/var|/var/www|/srv|/srv/www) die "Diretório web inseguro: ${WEB_ROOT}" ;;
  esac

  if [[ -n "${ADMIN_IP}" ]]; then
    [[ "${ADMIN_IP}" =~ ^[0-9A-Fa-f:.]+$ ]] || die "IP administrativo inválido: ${ADMIN_IP}"
  fi

  [[ "${MARIADB_ROOT_PASSWORD}" != *$'\n'* && "${MARIADB_ROOT_PASSWORD}" != *$'\r'* ]] ||
    die "MARIADB_ROOT_PASSWORD não pode conter quebra de linha"
}

detect_admin_ip() {
  if [[ -n "${ADMIN_IP}" ]]; then
    return
  fi

  if [[ -n "${SSH_CONNECTION:-}" ]]; then
    ADMIN_IP="${SSH_CONNECTION%% *}"
  elif [[ -n "${SSH_CLIENT:-}" ]]; then
    ADMIN_IP="${SSH_CLIENT%% *}"
  else
    ADMIN_IP="127.0.0.1"
  fi

  [[ "${ADMIN_IP}" =~ ^[0-9A-Fa-f:.]+$ ]] || die "Não consegui detectar um IP administrativo válido"
}

init_sudo() {
  if [[ "${EUID}" -eq 0 ]]; then
    SUDO=()
    return
  fi

  command_exists sudo || die "sudo não está instalado"
  info "Validando acesso sudo..."
  sudo -v
  SUDO=(sudo)
}

confirm() {
  local prompt="$1"
  local answer=""

  if [[ "${ASSUME_YES}" -eq 1 ]]; then
    return 0
  fi

  [[ -t 0 ]] || die "Confirmação necessária. Execute em um terminal ou use --yes."
  read -r -p "${prompt} [s/N]: " answer
  [[ "${answer,,}" == "s" || "${answer,,}" == "sim" ]]
}

package_status() {
  if dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'; then
    printf 'instalado'
  else
    printf 'ausente'
  fi
}

package_candidate() {
  apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2}'
}

latest_myaac_tag() {
  if ! command_exists curl || ! command_exists python3; then
    printf 'não consultada'
    return
  fi

  local response=""
  local tag=""

  if response="$(curl -fsSL --max-time 10 -H 'Accept: application/vnd.github+json' \
    https://api.github.com/repos/slawkens/myaac/releases/latest 2>/dev/null)"; then
    if ! tag="$(python3 -c 'import json, sys; print(json.load(sys.stdin).get("tag_name", ""))' \
      <<<"${response}" 2>/dev/null)"; then
      tag=""
    fi
  fi
  printf '%s' "${tag:-não consultada}"
}

service_status() {
  if systemctl is-active --quiet "$1" 2>/dev/null; then
    printf 'ativo'
  else
    printf 'inativo/ausente'
  fi
}

detect_php() {
  PHP_VERSION=""
  PHP_FPM_SERVICE=""
  PHP_FPM_SOCKET=""

  if ! command_exists php; then
    return 1
  fi

  PHP_VERSION="$(php -r 'echo PHP_MAJOR_VERSION, ".", PHP_MINOR_VERSION;')"
  PHP_FPM_SERVICE="php${PHP_VERSION}-fpm"
  PHP_FPM_SOCKET="/run/php/php${PHP_VERSION}-fpm.sock"
  return 0
}

php_version_supported() {
  detect_php || return 1
  dpkg --compare-versions "${PHP_VERSION}" ge "8.1"
}

verify_php_modules() {
  local module
  local modules
  local -a required=(PDO pdo_mysql json xml dom curl gd mbstring zip intl)

  modules="$(php -m)"
  for module in "${required[@]}"; do
    grep -qi "^${module}$" <<<"${modules}" || die "Extensão PHP ausente: ${module}"
  done
}

environment_report() {
  section "Diagnóstico da máquina"
  printf '%-24s %s\n' "Sistema:" "${UBUNTU_NAME}"
  printf '%-24s %s\n' "Arquitetura:" "$(dpkg --print-architecture 2>/dev/null || uname -m)"
  printf '%-24s %s\n' "TFS:" "1.8 / protocolo 8.60"
  printf '%-24s %s\n' "Diretório TFS:" "${TFS_DIR}"
  printf '%-24s %s\n' "Nginx:" "$(package_status nginx) / $(service_status nginx)"
  printf '%-24s %s\n' "MariaDB Server:" "$(package_status mariadb-server) / $(service_status mariadb)"

  if detect_php; then
    printf '%-24s %s\n' "PHP:" "${PHP_VERSION}"
    printf '%-24s %s\n' "PHP-FPM:" "$(service_status "${PHP_FPM_SERVICE}") (${PHP_FPM_SOCKET})"
  else
    printf '%-24s %s\n' "PHP:" "ausente (candidata: $(package_candidate php-fpm))"
  fi

  printf '%-24s %s\n' "MyAAC oficial:" "$(latest_myaac_tag)"
  printf '%-24s %s\n' "Diretório MyAAC:" "${WEB_ROOT}"
  printf '%-24s %s\n' "Domínio Nginx:" "${DOMAIN}"
  printf '%-24s %s\n' "IP de instalação:" "${ADMIN_IP}"
  printf '%-24s %s\n' "Banco:" "${DB_NAME} / usuário ${DB_USER}"

  if command_exists free; then
    printf '%-24s %s\n' "Memória:" "$(free -h | awk '/^Mem:/ {print $2 " total, " $7 " disponível"}')"
  fi
  printf '%-24s %s\n' "Disco disponível:" "$(df -h "${TFS_DIR}" | awk 'NR == 2 {print $4}')"
}

ask_database_settings() {
  local value=""

  if [[ "${ASSUME_YES}" -eq 0 && -t 0 ]]; then
    read -r -p "Nome do banco [${DB_NAME}]: " value
    DB_NAME="${value:-${DB_NAME}}"
    read -r -p "Usuário do banco [${DB_USER}]: " value
    DB_USER="${value:-${DB_USER}}"

    if [[ -z "${DB_PASSWORD}" ]]; then
      read -r -s -p "Senha do banco [Enter para gerar automaticamente]: " value
      printf '\n'
      DB_PASSWORD="${value}"
    fi
  fi

  if [[ -z "${DB_PASSWORD}" ]]; then
    DB_PASSWORD="$(openssl rand -hex 24)"
  fi

  validate_settings
}

read_lua_string() {
  local key="$1"
  local file="$2"
  local line=""
  local value=""
  local char=""
  local escaped=0
  local i=0

  LUA_STRING_VALUE=""
  line="$(grep -m1 -E "^[[:space:]]*${key}[[:space:]]*=[[:space:]]*\"" "${file}" || true)"
  [[ -n "${line}" ]] || return 1
  value="${line#*\"}"

  for ((i = 0; i < ${#value}; ++i)); do
    char="${value:i:1}"
    if [[ "${escaped}" -eq 1 ]]; then
      case "${char}" in
        a) LUA_STRING_VALUE+=$'\a' ;;
        b) LUA_STRING_VALUE+=$'\b' ;;
        f) LUA_STRING_VALUE+=$'\f' ;;
        n) LUA_STRING_VALUE+=$'\n' ;;
        r) LUA_STRING_VALUE+=$'\r' ;;
        t) LUA_STRING_VALUE+=$'\t' ;;
        v) LUA_STRING_VALUE+=$'\v' ;;
        \\) LUA_STRING_VALUE+='\' ;;
        '"') LUA_STRING_VALUE+='"' ;;
        *) return 1 ;;
      esac
      escaped=0
    elif [[ "${char}" == "\\" ]]; then
      escaped=1
    elif [[ "${char}" == '"' ]]; then
      return 0
    else
      LUA_STRING_VALUE+="${char}"
    fi
  done

  return 1
}

load_existing_database_settings() {
  local config="${TFS_DIR}/config.lua"
  [[ -f "${config}" ]] || return 0

  if [[ "${DB_NAME_EXPLICIT}" -eq 0 ]] && read_lua_string mysqlDatabase "${config}"; then
    DB_NAME="${LUA_STRING_VALUE}"
  fi

  if [[ "${DB_USER_EXPLICIT}" -eq 0 ]] && read_lua_string mysqlUser "${config}"; then
    DB_USER="${LUA_STRING_VALUE}"
  fi

  if [[ "${DB_PASSWORD_EXPLICIT}" -eq 0 ]] && read_lua_string mysqlPass "${config}"; then
    DB_PASSWORD="${LUA_STRING_VALUE}"
  fi
}

install_packages() {
  section "Instalando Nginx, PHP e MariaDB"

  local -a packages=(
    ca-certificates curl tar gzip openssl python3 acl
    nginx mariadb-server mariadb-client
    php-fpm php-cli php-mysql php-xml php-curl php-gd
    php-mbstring php-zip php-intl
  )

  "${SUDO[@]}" apt-get update
  "${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y "${packages[@]}"

  "${SUDO[@]}" systemctl enable --now mariadb
  "${SUDO[@]}" systemctl enable --now nginx

  php_version_supported || die "O PHP padrão instalado é incompatível. MyAAC 1.x requer PHP >= 8.1."
  "${SUDO[@]}" systemctl enable --now "${PHP_FPM_SERVICE}"
  verify_php_modules

  ok "PHP ${PHP_VERSION}, Nginx e MariaDB instalados"
}

configure_php() {
  section "Configurando PHP ${PHP_VERSION}"

  local ini_file="${TMP_DIR}/99-tfs18-myaac.ini"
  local target="/etc/php/${PHP_VERSION}/fpm/conf.d/99-tfs18-myaac.ini"

  cat >"${ini_file}" <<'EOF'
; TFS 1.8 + MyAAC
cgi.fix_pathinfo=0
memory_limit=256M
upload_max_filesize=32M
post_max_size=32M
max_execution_time=240
date.timezone=America/Sao_Paulo
session.cookie_httponly=1
session.cookie_samesite=Lax
expose_php=Off
EOF

  "${SUDO[@]}" install -o root -g root -m 0644 "${ini_file}" "${target}"
  "${SUDO[@]}" systemctl restart "${PHP_FPM_SERVICE}"

  [[ -S "${PHP_FPM_SOCKET}" ]] || die "Socket do PHP-FPM não encontrado: ${PHP_FPM_SOCKET}"
  ok "PHP-FPM ativo em ${PHP_FPM_SOCKET}"
}

write_mariadb_client_config() {
  local client_config="$1"
  {
    printf '[client]\n'
    printf 'host="127.0.0.1"\n'
    printf 'user="%s"\n' "$(escape_mariadb_option "${DB_USER}")"
    printf 'password="%s"\n' "$(escape_mariadb_option "${DB_PASSWORD}")"
    printf 'database="%s"\n' "$(escape_mariadb_option "${DB_NAME}")"
  } >"${client_config}"
  chmod 0600 "${client_config}"
}

escape_mariadb_option() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\n'/\\n}"
  value="${value//$'\r'/\\r}"
  value="${value//$'\t'/\\t}"
  value="${value//$'\b'/\\b}"
  printf '%s' "${value}"
}

write_mariadb_admin_config() {
  MARIADB_ADMIN_CONFIG="${TMP_DIR}/mariadb-admin.cnf"
  {
    printf '[client]\n'
    printf 'user=root\n'
    printf 'password="%s"\n' "$(escape_mariadb_option "${MARIADB_ROOT_PASSWORD}")"
    printf 'socket=/run/mysqld/mysqld.sock\n'
  } >"${MARIADB_ADMIN_CONFIG}"
  chmod 0600 "${MARIADB_ADMIN_CONFIG}"
}

mariadb_admin() {
  case "${MARIADB_ADMIN_MODE}" in
    socket)
      "${SUDO[@]}" mariadb --protocol=socket "$@"
      ;;
    debian)
      "${SUDO[@]}" mariadb --defaults-extra-file=/etc/mysql/debian.cnf --protocol=socket "$@"
      ;;
    password)
      "${SUDO[@]}" mariadb --defaults-extra-file="${MARIADB_ADMIN_CONFIG}" --protocol=socket "$@"
      ;;
    *)
      return 1
      ;;
  esac
}

prepare_mariadb_admin() {
  if "${SUDO[@]}" mariadb --protocol=socket -Nse 'SELECT 1' >/dev/null 2>&1; then
    MARIADB_ADMIN_MODE="socket"
    return 0
  fi

  if [[ -f /etc/mysql/debian.cnf ]] &&
    "${SUDO[@]}" mariadb --defaults-extra-file=/etc/mysql/debian.cnf --protocol=socket \
      -Nse 'SELECT 1' >/dev/null 2>&1; then
    MARIADB_ADMIN_MODE="debian"
    return 0
  fi

  if [[ -z "${MARIADB_ROOT_PASSWORD}" && -t 0 && "${ASSUME_YES}" -eq 0 ]]; then
    warn "O root do MariaDB não aceita autenticação automática por unix_socket."
    read -r -s -p "Senha do root do MariaDB: " MARIADB_ROOT_PASSWORD
    printf '\n'
  fi

  [[ -n "${MARIADB_ROOT_PASSWORD}" ]] || die \
    "Acesso administrativo necessário para criar o banco. Configure unix_socket ou execute com MARIADB_ROOT_PASSWORD definido."

  write_mariadb_admin_config
  MARIADB_ADMIN_MODE="password"
  mariadb_admin -Nse 'SELECT 1' >/dev/null 2>&1 || die \
    "A senha administrativa do MariaDB foi recusada. Verifique MARIADB_ROOT_PASSWORD."
}

import_tfs_schema_if_needed() {
  local client_config="$1"
  local table=""
  local actual_tables_output=""
  local -a expected_tables=()
  local -a actual_tables=()
  local -a missing_tables=()
  local -A actual_table_set=()

  mapfile -t expected_tables < <(sed -nE \
    's/^CREATE TABLE IF NOT EXISTS[[:space:]]+`?([A-Za-z0-9_]+)`?[[:space:]]*\(.*/\1/p' \
    "${TFS_DIR}/schema.sql")
  [[ "${#expected_tables[@]}" -gt 0 ]] || die \
    "Nenhuma tabela esperada foi encontrada em ${TFS_DIR}/schema.sql"

  actual_tables_output="$(mariadb --defaults-extra-file="${client_config}" -Nse \
    "SELECT table_name FROM information_schema.tables WHERE table_schema=DATABASE() AND table_type='BASE TABLE' ORDER BY table_name;")" ||
    die "Não foi possível consultar as tabelas existentes em ${DB_NAME}"
  if [[ -n "${actual_tables_output}" ]]; then
    mapfile -t actual_tables <<<"${actual_tables_output}"
  fi

  if [[ "${#actual_tables[@]}" -eq 0 ]]; then
    info "Importando ${TFS_DIR}/schema.sql..."
    mariadb --defaults-extra-file="${client_config}" <"${TFS_DIR}/schema.sql"
    ok "Schema do TFS importado"
    return
  fi

  for table in "${actual_tables[@]}"; do
    actual_table_set["${table}"]=1
  done
  for table in "${expected_tables[@]}"; do
    [[ -n "${actual_table_set[${table}]:-}" ]] || missing_tables+=("${table}")
  done

  if [[ "${#missing_tables[@]}" -gt 0 ]]; then
    die "Banco não está vazio e o schema do TFS está incompleto. Tabelas ausentes: ${missing_tables[*]}"
  fi

  ok "Banco já possui o schema completo do TFS; schema não foi importado novamente"
}

configure_database() {
  section "Configurando banco do TFS 1.8"

  systemctl is-active --quiet mariadb || "${SUDO[@]}" systemctl start mariadb

  local client_config="${TMP_DIR}/mariadb-client.cnf"
  write_mariadb_client_config "${client_config}"

  if mariadb --defaults-extra-file="${client_config}" -Nse 'SELECT 1' >/dev/null 2>&1; then
    ok "Banco existente acessível com o usuário ${DB_USER}; acesso root não é necessário"
    import_tfs_schema_if_needed "${client_config}"
    return 0
  fi

  prepare_mariadb_admin
  local db_host=""
  local password_hex=""
  password_hex="$(printf '%s' "${DB_PASSWORD}" | od -An -v -tx1 | tr -d ' \n')"
  {
    printf "CREATE DATABASE IF NOT EXISTS \`%s\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\n" "${DB_NAME}"
    printf "SET SESSION sql_mode = '';\n"
    printf "SET @tfs_password = X'%s';\n" "${password_hex}"
    for db_host in localhost 127.0.0.1; do
      printf "SET @tfs_statement = CONCAT('CREATE USER IF NOT EXISTS ''%s''@''%s'' IDENTIFIED BY ', QUOTE(@tfs_password));\n" "${DB_USER}" "${db_host}"
      printf 'PREPARE tfs_user_statement FROM @tfs_statement; EXECUTE tfs_user_statement; DEALLOCATE PREPARE tfs_user_statement;\n'
      printf "SET @tfs_statement = CONCAT('ALTER USER ''%s''@''%s'' IDENTIFIED BY ', QUOTE(@tfs_password));\n" "${DB_USER}" "${db_host}"
      printf 'PREPARE tfs_user_statement FROM @tfs_statement; EXECUTE tfs_user_statement; DEALLOCATE PREPARE tfs_user_statement;\n'
      printf "GRANT ALL PRIVILEGES ON \`%s\`.* TO '%s'@'%s';\n" "${DB_NAME}" "${DB_USER}" "${db_host}"
    done
    printf 'FLUSH PRIVILEGES;\n'
  } | mariadb_admin

  mariadb --defaults-extra-file="${client_config}" -Nse 'SELECT 1' >/dev/null ||
    die "O usuário ${DB_USER} não conseguiu acessar o banco ${DB_NAME}"
  import_tfs_schema_if_needed "${client_config}"
}

escape_lua_string() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\a'/\\a}"
  value="${value//$'\b'/\\b}"
  value="${value//$'\f'/\\f}"
  value="${value//$'\n'/\\n}"
  value="${value//$'\r'/\\r}"
  value="${value//$'\t'/\\t}"
  value="${value//$'\v'/\\v}"
  printf '%s' "${value}"
}

replace_lua_setting() {
  local file="$1"
  local key="$2"
  local value="$3"
  local escaped_value=""
  local replacement=""
  local staged_file=""

  escaped_value="$(escape_lua_string "${value}")"
  replacement="${key} = \"${escaped_value}\""
  staged_file="$(mktemp "${TMP_DIR}/lua-setting.XXXXXXXX")"

  LUA_SETTING_KEY="${key}" LUA_SETTING_REPLACEMENT="${replacement}" awk '
    BEGIN { pattern = "^[[:space:]]*" ENVIRON["LUA_SETTING_KEY"] "[[:space:]]*=" }
    $0 ~ pattern { print ENVIRON["LUA_SETTING_REPLACEMENT"]; replaced = 1; next }
    { print }
    END {
      if (!replaced) {
        print ""
        print ENVIRON["LUA_SETTING_REPLACEMENT"]
      }
    }
  ' "${file}" >"${staged_file}"
  mv -- "${staged_file}" "${file}"
}

configure_tfs() {
  section "Ligando o TFS 1.8 ao banco"

  local config="${TFS_DIR}/config.lua"
  local backup=""
  local source_config="${config}"
  local staged_config="${TMP_DIR}/config.lua"
  local owner_uid=""
  local owner_gid=""

  owner_uid="$(stat -c '%u' "${TFS_DIR}")"
  owner_gid="$(stat -c '%g' "${TFS_DIR}")"
  if [[ "${EUID}" -ne 0 && "${EUID}" -ne "${owner_uid}" ]]; then
    die "Execute o script como o dono de ${TFS_DIR} ou como root"
  fi

  if [[ ! -f "${config}" ]]; then
    source_config="${TFS_DIR}/config.lua.dist"
  else
    backup="${config}.backup.$(date +%Y%m%d-%H%M%S)"
    "${SUDO[@]}" cp -a "${config}" "${backup}"
  fi

  cp -a "${source_config}" "${staged_config}"

  replace_lua_setting "${staged_config}" mysqlHost "127.0.0.1"
  replace_lua_setting "${staged_config}" mysqlUser "${DB_USER}"
  replace_lua_setting "${staged_config}" mysqlPass "${DB_PASSWORD}"
  replace_lua_setting "${staged_config}" mysqlDatabase "${DB_NAME}"
  "${SUDO[@]}" install -o "${owner_uid}" -g "${owner_gid}" -m 0640 "${staged_config}" "${config}"

  if [[ -n "${backup}" ]]; then
    ok "config.lua atualizado; backup: ${backup}"
  else
    ok "config.lua criado a partir de config.lua.dist"
  fi
}

grant_myaac_tfs_access() {
  section "Permitindo leitura do TFS pelo MyAAC"

  local current="/"
  local part
  local relative="${TFS_DIR#/}"
  local -a parts=()
  IFS='/' read -r -a parts <<<"${relative}"

  for part in "${parts[@]}"; do
    [[ -n "${part}" ]] || continue
    current="${current%/}/${part}"
    if [[ "${current}" != "${TFS_DIR}" ]]; then
      "${SUDO[@]}" setfacl -m u:www-data:--x "${current}"
    fi
  done

  "${SUDO[@]}" setfacl -R -m u:www-data:rX "${TFS_DIR}"
  ok "www-data pode ler a configuração e os dados do TFS"
}

fetch_release_metadata() {
  section "Consultando release oficial do MyAAC"

  local endpoint="https://api.github.com/repos/slawkens/myaac/releases/latest"
  local metadata="${TMP_DIR}/release.json"
  local -a release_data=()

  if [[ "${MYAAC_RELEASE}" != "latest" ]]; then
    endpoint="https://api.github.com/repos/slawkens/myaac/releases/tags/${MYAAC_RELEASE}"
  fi

  curl -fsSL --retry 3 -H 'Accept: application/vnd.github+json' "${endpoint}" -o "${metadata}"

  mapfile -t release_data < <(python3 - "${metadata}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    release = json.load(handle)

assets = [asset for asset in release.get("assets", []) if asset.get("name", "").startswith("myaac-") and asset.get("name", "").endswith(".tar.gz")]
if not assets:
    raise SystemExit("release sem pacote myaac-*.tar.gz")

asset = assets[0]
digest = asset.get("digest") or ""
if digest.startswith("sha256:"):
    digest = digest.split(":", 1)[1]

print(release["tag_name"])
print(asset["browser_download_url"])
print(digest)
PY
  )

  [[ "${#release_data[@]}" -ge 3 ]] || die "Resposta inválida da API de releases do MyAAC"
  MYAAC_TAG="${release_data[0]}"
  MYAAC_URL="${release_data[1]}"
  MYAAC_SHA256="${release_data[2]}"

  ok "Release oficial selecionada: ${MYAAC_TAG}"
}

set_myaac_permissions() {
  "${SUDO[@]}" chown -R www-data:www-data "${WEB_ROOT}"
  "${SUDO[@]}" find "${WEB_ROOT}" -type d -exec chmod 0750 {} +
  "${SUDO[@]}" find "${WEB_ROOT}" -type f -exec chmod 0640 {} +

  "${SUDO[@]}" test ! -f "${WEB_ROOT}/aac" || "${SUDO[@]}" chmod 0750 "${WEB_ROOT}/aac"
  "${SUDO[@]}" test ! -d "${WEB_ROOT}/system/cache" || "${SUDO[@]}" chmod -R 0770 "${WEB_ROOT}/system/cache"

  local writable
  for writable in images/guilds images/houses images/gallery; do
    "${SUDO[@]}" test ! -d "${WEB_ROOT}/${writable}" || "${SUDO[@]}" chmod 0770 "${WEB_ROOT}/${writable}"
  done

  "${SUDO[@]}" test ! -f "${WEB_ROOT}/config.local.php" || "${SUDO[@]}" chmod 0660 "${WEB_ROOT}/config.local.php"
}

write_installer_ip() {
  local ip_file="${TMP_DIR}/ip.txt"

  {
    printf '%s\n' "${ADMIN_IP}"
    printf '127.0.0.1\n127.0.0.2\n::1\n'
  } | awk 'NF && !seen[$0]++' >"${ip_file}"

  "${SUDO[@]}" install -o www-data -g www-data -m 0640 "${ip_file}" "${WEB_ROOT}/install/ip.txt"
}

installer_is_locked() {
  "${SUDO[@]}" find "${WEB_ROOT}/install" -maxdepth 1 -type f \
    -name 'ip.txt.disabled.*' -print -quit 2>/dev/null | grep -q .
}

restore_installer_lock() {
  local backup_root="$1"
  local lock_marker=""

  lock_marker="$("${SUDO[@]}" find "${backup_root}/install" -maxdepth 1 -type f \
    -name 'ip.txt.disabled.*' -print -quit 2>/dev/null)"
  [[ -n "${lock_marker}" ]] || die "Marcador de bloqueio do instalador não foi encontrado no backup"

  "${SUDO[@]}" install -d -o www-data -g www-data -m 0750 "${WEB_ROOT}/install"
  "${SUDO[@]}" rm -f -- "${WEB_ROOT}/install/ip.txt"
  "${SUDO[@]}" cp -a -- "${lock_marker}" "${WEB_ROOT}/install/$(basename "${lock_marker}")"
}

deploy_myaac() {
  section "Instalando ${MYAAC_TAG} de slawkens/myaac"

  local archive="${TMP_DIR}/myaac.tar.gz"
  local archive_list="${TMP_DIR}/myaac-files.txt"
  local extract_root="${TMP_DIR}/extract"
  local extracted_dir=""
  local current_tag=""
  local installer_locked=0
  local saved_config="${TMP_DIR}/config.local.php"

  if "${SUDO[@]}" test -f "${WEB_ROOT}/.myaac-release"; then
    current_tag="$("${SUDO[@]}" cat "${WEB_ROOT}/.myaac-release")"
  fi
  if installer_is_locked; then
    installer_locked=1
  fi

  if [[ "${current_tag}" == "${MYAAC_TAG}" && "${FORCE_DEPLOY}" -eq 0 ]]; then
    ok "${MYAAC_TAG} já está instalada; arquivos preservados"
    if [[ "${installer_locked}" -eq 1 ]]; then
      ok "Instalador MyAAC permanece bloqueado"
    else
      write_installer_ip
    fi
    set_myaac_permissions
    return
  fi

  curl -fL --retry 3 "${MYAAC_URL}" -o "${archive}"
  if [[ -n "${MYAAC_SHA256}" ]]; then
    printf '%s  %s\n' "${MYAAC_SHA256}" "${archive}" | sha256sum -c -
  else
    warn "A release não publicou digest SHA-256; download não pôde ser comparado com digest da API"
  fi

  tar -tzf "${archive}" >"${archive_list}"
  if grep -Eq '(^/|(^|/)\.\.(/|$))' "${archive_list}"; then
    die "O pacote MyAAC contém caminho inseguro"
  fi

  mkdir -p "${extract_root}"
  tar -xzf "${archive}" -C "${extract_root}"
  extracted_dir="$(find "${extract_root}" -mindepth 1 -maxdepth 1 -type d -name 'myaac-*' -print -quit)"
  [[ -n "${extracted_dir}" ]] || die "Diretório principal não encontrado no pacote MyAAC"
  [[ -f "${extracted_dir}/composer.json" && -f "${extracted_dir}/vendor/autoload.php" ]] ||
    die "O pacote MyAAC não contém a aplicação completa"

  "${SUDO[@]}" install -d -o root -g root -m 0755 "$(dirname "${WEB_ROOT}")" /var/backups

  if "${SUDO[@]}" test -e "${WEB_ROOT}"; then
    BACKUP_PATH="/var/backups/myaac-$(date +%Y%m%d-%H%M%S)"
    [[ ! -e "${BACKUP_PATH}" ]] || die "Backup já existe: ${BACKUP_PATH}"

    if "${SUDO[@]}" test -f "${WEB_ROOT}/config.local.php"; then
      "${SUDO[@]}" cp -a "${WEB_ROOT}/config.local.php" "${saved_config}"
      "${SUDO[@]}" chown "$(id -u):$(id -g)" "${saved_config}"
    fi

    "${SUDO[@]}" mv -- "${WEB_ROOT}" "${BACKUP_PATH}"
    info "Instalação anterior movida para ${BACKUP_PATH}"
  fi

  "${SUDO[@]}" mv -- "${extracted_dir}" "${WEB_ROOT}"
  if [[ -f "${saved_config}" ]]; then
    "${SUDO[@]}" cp -a "${saved_config}" "${WEB_ROOT}/config.local.php"
  fi

  printf '%s\n' "${MYAAC_TAG}" >"${TMP_DIR}/.myaac-release"
  "${SUDO[@]}" install -o www-data -g www-data -m 0640 "${TMP_DIR}/.myaac-release" "${WEB_ROOT}/.myaac-release"

  if [[ "${installer_locked}" -eq 1 ]]; then
    restore_installer_lock "${BACKUP_PATH}"
    ok "Bloqueio do instalador MyAAC preservado após atualização"
  else
    write_installer_ip
  fi
  set_myaac_permissions
  ok "MyAAC instalado em ${WEB_ROOT}"
}

configure_nginx() {
  section "Configurando Nginx"

  local site_file="${TMP_DIR}/${SITE_NAME}.conf"
  local target="/etc/nginx/sites-available/${SITE_NAME}"
  local listen_default=""
  local enabled_site=""
  local resolved_site=""

  if [[ "${DOMAIN}" == "_" ]]; then
    for enabled_site in /etc/nginx/sites-enabled/*; do
      [[ -e "${enabled_site}" || -L "${enabled_site}" ]] || continue
      if [[ "${enabled_site}" == "/etc/nginx/sites-enabled/default" && -L "${enabled_site}" ]]; then
        continue
      fi
      resolved_site="$(readlink -f -- "${enabled_site}" 2>/dev/null || true)"
      [[ "${resolved_site}" == "${target}" ]] && continue
      if "${SUDO[@]}" grep -Eq \
        '^[[:space:]]*listen[[:space:]]+([^;[:space:]]*:)?80[[:space:]][^;]*default_server([[:space:]]|;)' \
        "${enabled_site}"; then
        die "Outro site Nginx já usa default_server na porta 80: ${enabled_site}"
      fi
    done
    listen_default=" default_server"
  fi

  cat >"${site_file}" <<EOF
server {
    listen 80${listen_default};
    listen [::]:80${listen_default};
    server_name ${DOMAIN};

    root ${WEB_ROOT};
    index index.php;
    client_max_body_size 32M;

    location ~ ^/system(?:/|\$) {
        deny all;
    }

    location ~* \.(?:ht|md|json|dist|sql)\$ {
        deny all;
    }

    location ~ /\.(?!well-known(?:/|\$)) {
        deny all;
    }

    location / {
        try_files \$uri \$uri/ /index.php?\$query_string;
    }

    location ~ \.php\$ {
        include snippets/fastcgi-php.conf;
        fastcgi_read_timeout 240;
        fastcgi_pass unix:${PHP_FPM_SOCKET};
    }

    location ~* \.(?:css|js|jpg|jpeg|gif|png|svg|ico|webp|woff2?)\$ {
        expires 7d;
        access_log off;
        try_files \$uri =404;
    }
}
EOF

  "${SUDO[@]}" install -o root -g root -m 0644 "${site_file}" "${target}"
  "${SUDO[@]}" ln -sfn "${target}" "/etc/nginx/sites-enabled/${SITE_NAME}"
  if [[ -L /etc/nginx/sites-enabled/default ]]; then
    "${SUDO[@]}" rm -- /etc/nginx/sites-enabled/default
  fi

  "${SUDO[@]}" nginx -t
  "${SUDO[@]}" systemctl reload nginx
  ok "Site Nginx habilitado"
}

install_phpmyadmin() {
  [[ "${WITH_PHPMYADMIN}" -eq 1 ]] || return 0

  section "Instalando phpMyAdmin local"
  local site_file="${TMP_DIR}/tfs18-phpmyadmin.conf"
  local target="/etc/nginx/sites-available/tfs18-phpmyadmin"

  if command_exists debconf-set-selections; then
    printf '%s\n' \
      'phpmyadmin phpmyadmin/dbconfig-install boolean false' \
      'phpmyadmin phpmyadmin/reconfigure-webserver multiselect' |
      "${SUDO[@]}" debconf-set-selections
  fi
  "${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y phpmyadmin
  [[ -d /usr/share/phpmyadmin ]] || die "/usr/share/phpmyadmin não encontrado"

  if "${SUDO[@]}" test -f /etc/dbconfig-common/phpmyadmin.conf; then
    "${SUDO[@]}" sed -i -E "s/^dbc_install=.*/dbc_install='false'/" \
      /etc/dbconfig-common/phpmyadmin.conf
  fi

  cat >"${site_file}" <<EOF
server {
    listen 127.0.0.1:2344;
    server_name localhost;
    root /usr/share/phpmyadmin;
    index index.php;

    location / {
        try_files \$uri \$uri/ /index.php?\$query_string;
    }

    location ~ \.php\$ {
        include snippets/fastcgi-php.conf;
        fastcgi_pass unix:${PHP_FPM_SOCKET};
    }

    location ~ /\. {
        deny all;
    }
}
EOF

  "${SUDO[@]}" install -o root -g root -m 0644 "${site_file}" "${target}"
  "${SUDO[@]}" ln -sfn "${target}" /etc/nginx/sites-enabled/tfs18-phpmyadmin
  "${SUDO[@]}" nginx -t
  "${SUDO[@]}" systemctl reload nginx
  ok "phpMyAdmin disponível somente em 127.0.0.1:2344"
}

save_credentials() {
  local credentials="${TMP_DIR}/credentials.txt"
  local target="/root/.config/tfs18-myaac/credentials.txt"

  cat >"${credentials}" <<EOF
TFS_DIR=${TFS_DIR}
MYAAC_ROOT=${WEB_ROOT}
MYAAC_VERSION=${MYAAC_TAG}
DB_HOST=127.0.0.1
DB_NAME=${DB_NAME}
DB_USER=${DB_USER}
DB_PASSWORD=${DB_PASSWORD}
EOF
  chmod 0600 "${credentials}"

  "${SUDO[@]}" install -d -o root -g root -m 0700 /root/.config/tfs18-myaac
  "${SUDO[@]}" install -o root -g root -m 0600 "${credentials}" "${target}"
}

verify_installation() {
  section "Verificação final"

  "${SUDO[@]}" nginx -t
  systemctl is-active --quiet nginx || die "Nginx não está ativo"
  systemctl is-active --quiet mariadb || die "MariaDB não está ativo"
  systemctl is-active --quiet "${PHP_FPM_SERVICE}" || die "${PHP_FPM_SERVICE} não está ativo"
  [[ -S "${PHP_FPM_SOCKET}" ]] || die "Socket PHP-FPM ausente"
  "${SUDO[@]}" test -f "${WEB_ROOT}/index.php" || die "index.php do MyAAC ausente"
  [[ -f "${TFS_DIR}/config.lua" ]] || die "config.lua do TFS ausente"

  local http_code
  http_code="$(curl -sS -o /dev/null -w '%{http_code}' -H "Host: ${DOMAIN}" http://127.0.0.1/ || true)"
  case "${http_code}" in
    200|301|302) ok "Nginx respondeu HTTP ${http_code}" ;;
    *) die "Resposta HTTP inesperada do MyAAC: ${http_code:-sem resposta}" ;;
  esac

  ok "PHP ${PHP_VERSION} com extensões obrigatórias"
  ok "MariaDB e schema do TFS acessíveis"
  ok "MyAAC ${MYAAC_TAG} pronto para o instalador web"
}

installer_url() {
  if [[ "${DOMAIN}" == "_" ]]; then
    printf 'http://IP-DA-VPS/install/'
  else
    printf 'http://%s/install/' "${DOMAIN}"
  fi
}

print_summary() {
  section "Instalação preparada"
  printf 'MyAAC:            %s\n' "${MYAAC_TAG}"
  printf 'PHP:              %s\n' "${PHP_VERSION}"
  printf 'TFS:              1.8 / protocolo 8.60\n'
  printf 'Diretório TFS:    %s\n' "${TFS_DIR}"
  printf 'Diretório MyAAC:  %s\n' "${WEB_ROOT}"
  printf 'Banco:            %s\n' "${DB_NAME}"
  printf 'Usuário do banco: %s\n' "${DB_USER}"
  printf 'Senha do banco:   %s\n' "${DB_PASSWORD}"
  printf 'Instalador:       %s\n' "$(installer_url)"
  printf 'IP autorizado:    %s\n' "${ADMIN_IP}"
  printf 'Credenciais:      /root/.config/tfs18-myaac/credentials.txt\n'

  if [[ -n "${BACKUP_PATH}" ]]; then
    printf 'Backup MyAAC:     %s\n' "${BACKUP_PATH}"
  fi

  if [[ "${WITH_PHPMYADMIN}" -eq 1 ]]; then
    printf '\nphpMyAdmin está restrito ao localhost. Na sua máquina, use:\n'
    printf '  ssh -L 2344:127.0.0.1:2344 usuario@IP-DA-VPS\n'
    printf 'Depois abra: http://127.0.0.1:2344\n'
  fi

  printf '\nNo instalador MyAAC use:\n'
  printf '  Server path: %s/\n' "${TFS_DIR}"
  printf '  Client version: 8.60\n'
  printf '  Database host: 127.0.0.1\n'
  printf '\nApós concluir no navegador, bloqueie /install com:\n'
  printf '  sudo ./install-tfs18-myaac.sh --lock-installer\n'
  warn "A senha acima também está no arquivo root-only. Não envie nem publique esse arquivo."
}

lock_installer() {
  local ip_file="${WEB_ROOT}/install/ip.txt"
  local disabled=""
  disabled="${WEB_ROOT}/install/ip.txt.disabled.$(date +%Y%m%d-%H%M%S)"

  init_sudo
  "${SUDO[@]}" test -f "${ip_file}" || die "${ip_file} não existe; o instalador já pode estar bloqueado"
  "${SUDO[@]}" mv -- "${ip_file}" "${disabled}"
  ok "Instalador MyAAC bloqueado. Arquivo movido para ${disabled}"
}

run_install() {
  load_existing_database_settings
  ask_database_settings

  section "Plano"
  printf 'Será instalada/configurada a pilha web para TFS 1.8 em %s.\n' "${UBUNTU_NAME}"
  printf 'O banco %s será criado e o schema será importado somente se estiver vazio.\n' "${DB_NAME}"
  printf 'Uma instalação MyAAC existente será preservada em /var/backups antes da atualização.\n'
  confirm "Continuar?" || die "Instalação cancelada"

  init_sudo
  TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tfs18-myaac.XXXXXXXX")"
  log "Início da instalação em ${UBUNTU_NAME}, TFS=${TFS_DIR}"

  install_packages
  configure_php
  fetch_release_metadata
  configure_database
  configure_tfs
  grant_myaac_tfs_access
  deploy_myaac
  configure_nginx
  install_phpmyadmin
  save_credentials
  verify_installation
  print_summary

  log "Instalação concluída: MyAAC=${MYAAC_TAG}, PHP=${PHP_VERSION}"
}

main() {
  parse_args "$@"
  banner
  detect_ubuntu
  resolve_tfs_dir
  validate_tfs
  detect_admin_ip
  validate_settings

  case "${MODE}" in
    check)
      environment_report
      ;;
    install)
      environment_report
      run_install
      ;;
    lock)
      lock_installer
      ;;
    menu)
      environment_report
      printf '\n'
      if confirm "Instalar ou atualizar TFS 1.8 + MyAAC nesta máquina?"; then
        run_install
      else
        info "Nenhuma alteração realizada. Para instalar depois: ./install-tfs18-myaac.sh --install"
      fi
      ;;
  esac
}

main "$@"
