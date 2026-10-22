#!/bin/bash -e

# Host side of the containerised build. Driven by build_via_podman.sh /
# build_via_docker.sh, which set CONTAINER_RUNTIME.
#
#   build_via_podman.sh <board> [app directory]     board: pico_w, pico2_w, ..., or linux

CONTAINER_RUNTIME="${CONTAINER_RUNTIME:-podman}"

if [ -z "$1" ]; then
    echo "Usage: build_via_${CONTAINER_RUNTIME}.sh (pico_w|pico2_w|...|linux) [app directory]"
    exit 1
fi

PICO_BOARD="$1"

if [ "${PICO_BOARD}" = "linux" ]; then
    PLATFORM=linux
    IMAGE=local_build_pico_os_linux
else
    PLATFORM=pico
    IMAGE=local_build_pico_https
fi

OS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${OS_DIR}/.devcontainer/find_app_root.sh"
if [ -n "$2" ]; then
    APP_ROOT="$(cd "$2" && pwd)"
else
    APP_ROOT="$(find_app_root "${OS_DIR}")"
fi

MOUNTS=(-v "${APP_ROOT}":/workspaces/app:z -v "${OS_DIR}":/workspaces/app/os:z)
CONTAINER_APP_ROOT=/workspaces/app
CONTAINER_OS_DIR=/workspaces/app/os

echo "Building '${PICO_BOARD}' (${PLATFORM}) for ${APP_ROOT} with the OS in ${OS_DIR}"

if [ ! -f "${OS_DIR}/submodules/pico-sdk/lib/cyw43-driver/src/cyw43_ll.c" ] || [ ! -f "${OS_DIR}/submodules/pico-sdk/lib/mbedtls/library/aes.c" ]; then
    git -C "${OS_DIR}" submodule update --init --recursive
fi

KEY_DIR="${APP_ROOT}/keys"
if command -v openssl >/dev/null && [ ! -d "${KEY_DIR}/versions" ]; then
    echo "No OTA keys yet, generating version 0 in ${KEY_DIR}"
    "${OS_DIR}/signing/create_keys.sh" 0 "${KEY_DIR}"
fi

"${CONTAINER_RUNTIME}" build --network host -t "${IMAGE}" -f "${OS_DIR}/.devcontainer/${PLATFORM}.Dockerfile" "${OS_DIR}/.devcontainer"

RUN_ARGS=(--rm)
if [ -t 0 ]; then
    RUN_ARGS+=(-it)
fi
if [ "${CONTAINER_RUNTIME}" = "docker" ] && [ "${PLATFORM}" = "linux" ]; then
    RUN_ARGS+=(--user "$(id -u):$(id -g)")
fi

"${CONTAINER_RUNTIME}" run "${RUN_ARGS[@]}" \
    "${MOUNTS[@]}" \
    -e PICO_BOARD="${PICO_BOARD}" \
    -e PICO_APP_ROOT="${CONTAINER_APP_ROOT}" \
    -e PICO_STACK_LIMIT="${PICO_STACK_LIMIT}" \
    -e PICO_OS_BUILD_TYPE="${PICO_OS_BUILD_TYPE}" \
    -e PICO_OS_CMAKE_ARGS="${PICO_OS_CMAKE_ARGS}" \
    "${IMAGE}" \
    "${CONTAINER_OS_DIR}/.devcontainer/build.sh" "${PICO_BOARD}"
