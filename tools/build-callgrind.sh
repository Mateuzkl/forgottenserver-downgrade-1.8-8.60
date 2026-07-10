#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-callgrind"
OUTPUT_BIN="${ROOT_DIR}/tfs-valgrind"
JOBS="${JOBS:-$(nproc)}"

source "${ROOT_DIR}/tools/cmake-linux-env.sh"

cd "${ROOT_DIR}"

echo "Iniciando a preparacao do ambiente..."
[[ "${BUILD_DIR}" == "${ROOT_DIR}/build-callgrind" ]] || {
	echo "Diretorio de build invalido: ${BUILD_DIR}" >&2
	exit 1
}
rm -rf -- "${BUILD_DIR}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake not found" >&2
	exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
	echo "ninja not found" >&2
	exit 1
fi

tfs_check_lua55_paths

echo "Configurando o CMake..."
cmake_args=(
	-S .
	-B "${BUILD_DIR}"
	-G Ninja
	-DCMAKE_BUILD_TYPE=RelWithDebInfo
	-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2\ -g\ -fno-omit-frame-pointer
	-DENABLE_NATIVE_OPTIMIZATIONS=OFF
	-DENABLE_SLOW_TASK_DETECTION=OFF
	-DUSE_MIMALLOC=OFF
	-DENABLE_UNITY_BUILD=OFF
	-DBUILD_TESTING=OFF
	-DBUILD_BENCHMARKING=OFF
)
tfs_append_linux_cmake_cache_args cmake_args
cmake "${cmake_args[@]}"

echo "Compilando..."
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ ! -x "${BUILD_DIR}/tfs" ]]; then
	echo "Binario Callgrind nao encontrado: ${BUILD_DIR}/tfs" >&2
	exit 1
fi

echo "Copiando executavel..."
cp "${BUILD_DIR}/tfs" "${OUTPUT_BIN}"
chmod +x "${OUTPUT_BIN}"

echo "Build finalizada! ${OUTPUT_BIN} esta pronto para uso."
