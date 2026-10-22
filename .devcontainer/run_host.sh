#!/bin/bash -e

# Runs an app's linux build in the build container (where valgrind and gdb are).
# Driven by run_via_podman.sh / run_via_docker.sh.
#
#   run_via_podman.sh [--valgrind | --gdb] [--net | --ip ADDR] [--network NAME] [--publish]
#                     [--root] [--program NAME] [--no-trace-stdout] [app directory] [-- program options]
#
# Log: the program gets --trace-stdout, so every trace() line shows here.
#   --no-trace-stdout   only crashes and exits (also accepted after --).
#
# Program:
#   (default)    build/linux/<project name>.
#   --program N  build/linux/N, for apps building several executables.
#   Either one missing: the executables in build/linux/ are offered to pick from
#   (the only one is taken as it is; without a terminal they are listed).
#
# Networking:
#   (default)    the host's network: the app is reached on localhost:8080 and 8443
#                (the program's default, --port-offset 8000).
#   --net        a container network of its own ("picohive", 10.89.50.0/24, made if
#                missing), at 10.89.50.10: the device has its own IP, on the real
#                ports 80/443, other containers on that network reach it by IP or
#                by its name (picohive-<app>), and it reaches the internet through NAT.
#   --ip ADDR    the same, at ADDR. A network made for it gets ADDR's /24.
#   --network N  use network N instead of "picohive".
#   --publish    also map the host's 8080/8443 to the device's 80/443; with
#                rootless podman the host cannot reach container IPs directly.
#   --root       podman through sudo (implies --net): the network is then made on
#                the host, so this machine reaches the device's IP directly, a
#                browser included. The program still runs as you, its files stay
#                yours. Root has its own images: the first run builds it there.
#                Docker is rootful already, --root changes nothing for it.
#
# valgrind runs with --leak-check=full --track-origins=yes; VALGRIND_ARGS adds options.

CONTAINER_RUNTIME="${CONTAINER_RUNTIME:-podman}"
OS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${OS_DIR}/.devcontainer/find_app_root.sh"

TOOL=""
GIVEN=""
NETWORK=""
IP=""
PUBLISH=0
ROOT=0
PROGRAM_NAME=""
TRACE_STDOUT=1
while [ $# -gt 0 ]; do
    case "$1" in
        --valgrind) TOOL=valgrind; shift ;;
        --gdb) TOOL=gdb; shift ;;
        --net) NETWORK="${NETWORK:-picohive}"; IP="${IP:-10.89.50.10}"; shift ;;
        --ip) IP="$2"; NETWORK="${NETWORK:-picohive}"; shift 2 ;;
        --network) NETWORK="$2"; shift 2 ;;
        --publish) PUBLISH=1; shift ;;
        --root) ROOT=1; shift ;;
        --program) PROGRAM_NAME="$2"; shift 2 ;;
        --no-trace-stdout) TRACE_STDOUT=0; shift ;;
        --) shift; break ;;
        -*) echo "unknown option $1 (program options go after --)" >&2; exit 1 ;;
        *) GIVEN="$1"; shift ;;
    esac
done

# --trace-stdout unless told not to; the program has no --no-trace-stdout, so
# one given after -- is taken out here.
PROGRAM_ARGS=()
for arg in "$@"; do
    case "${arg}" in
        --no-trace-stdout) TRACE_STDOUT=0 ;;
        --trace-stdout) ;;
        *) PROGRAM_ARGS+=("${arg}") ;;
    esac
done
set -- "${PROGRAM_ARGS[@]}"
if [ "${TRACE_STDOUT}" = "1" ]; then
    set -- --trace-stdout "$@"
fi

if [ -n "${GIVEN}" ]; then
    APP_ROOT="$(cd "${GIVEN}" && pwd)" || APP_ROOT=""
else
    APP_ROOT="$(find_app_root "${OS_DIR}")" || APP_ROOT=""
fi

if [ -z "${APP_ROOT}" ] || ! grep -q "pico_os_pre_project" "${APP_ROOT}/CMakeLists.txt" 2>/dev/null; then
    echo "Usage: run_via_${CONTAINER_RUNTIME}.sh [--valgrind | --gdb] [--net | --ip ADDR] [--network NAME] [--publish] [--root] [--program NAME] [--no-trace-stdout] [app directory] [-- program options]"
    exit 1
fi

BUILD_DIR="${APP_ROOT}/build/linux"
APP_NAME="${PROGRAM_NAME:-$(sed -n 's/^[[:space:]]*project(\([A-Za-z0-9_@-]*\).*/\1/p' "${APP_ROOT}/CMakeLists.txt" | head -1)}"

# The ELF executables in build/linux/, without the .stripped OTA payloads.
linux_programs() {
    local f
    for f in "${BUILD_DIR}"/*; do
        if [ -f "${f}" ] && [ -x "${f}" ] && [[ "${f}" != *.stripped ]] && [ "$(head -c 4 "${f}" | tail -c 3)" = "ELF" ]; then
            basename "${f}"
        fi
    done
}

if [ ! -x "${BUILD_DIR}/${APP_NAME}" ]; then
    mapfile -t PROGRAMS < <(linux_programs)
    if [ "${#PROGRAMS[@]}" = "0" ]; then
        echo "error: no ${BUILD_DIR}/${APP_NAME}, build it first with build_via_${CONTAINER_RUNTIME}.sh linux" >&2
        exit 1
    elif [ "${#PROGRAMS[@]}" = "1" ]; then
        echo "[run] no build/linux/${APP_NAME}, running build/linux/${PROGRAMS[0]}"
        APP_NAME="${PROGRAMS[0]}"
    elif [ -t 0 ]; then
        echo "[run] no build/linux/${APP_NAME}; which one?"
        PS3="program: "
        select choice in "${PROGRAMS[@]}"; do
            if [ -n "${choice}" ]; then
                APP_NAME="${choice}"
                break
            fi
        done
        if [ -z "${choice}" ]; then
            exit 1
        fi
    else
        echo "error: no ${BUILD_DIR}/${APP_NAME}; pick one with --program: ${PROGRAMS[*]}" >&2
        exit 1
    fi
fi

MOUNTS=(-v "${APP_ROOT}":/workspaces/app:z -v "${OS_DIR}":/workspaces/app/os:z)
CONTAINER_APP_ROOT=/workspaces/app

PROGRAM="${CONTAINER_APP_ROOT}/build/linux/${APP_NAME}"

case "${TOOL}" in
    valgrind)
        TOOL_CMD=(valgrind --leak-check=full --track-origins=yes --show-leak-kinds=definite,indirect --error-exitcode=99 ${VALGRIND_ARGS})
        ;;
    gdb)
        TOOL_CMD=(gdb -q -ex run --args)
        set -- --abort-on-fail --no-watchdog "$@"
        ;;
    *)
        TOOL_CMD=()
        ;;
esac

RT=("${CONTAINER_RUNTIME}")
if [ "${ROOT}" = "1" ]; then
    NETWORK="${NETWORK:-picohive}"
    IP="${IP:-10.89.50.10}"
    if [ "${CONTAINER_RUNTIME}" = "podman" ]; then
        RT=(sudo podman)
    fi
fi

RUN_ARGS=(--rm -i --init)

if [ -n "${NETWORK}" ]; then
    # An IP of its own: the real ports 80/443 (the program's default is 8080/8443).
    set -- --port-offset 0 "$@"

    if ! "${RT[@]}" network inspect "${NETWORK}" >/dev/null 2>&1; then
        subnet="${IP:-10.89.50.10}"
        subnet="${subnet%.*}.0/24"
        echo "Creating container network ${NETWORK} (${subnet})"
        "${RT[@]}" network create --subnet "${subnet}" "${NETWORK}" >/dev/null
    fi

    NAME="picohive-${APP_NAME}"
    RUN_ARGS+=(--network "${NETWORK}" --name "${NAME}" --hostname "${NAME}"
               --sysctl net.ipv4.ip_unprivileged_port_start=0)
    if [ -n "${IP}" ]; then
        RUN_ARGS+=(--ip "${IP}")
    fi
    if [ "${PUBLISH}" = "1" ]; then
        RUN_ARGS+=(-p 8080:80 -p 8443:443)
    fi

    echo "Device ${NAME} on network ${NETWORK}: http://${IP:-<assigned>}/ and https://${IP:-<assigned>}/"
    echo "  other containers: --network ${NETWORK}, then ${IP:-${NAME}} or ${NAME}"
    if [ "${ROOT}" = "1" ] || [ "${CONTAINER_RUNTIME}" = "docker" ]; then
        echo "  this host: http://${IP:-<the address above>}/ directly"
    elif [ "${PUBLISH}" = "1" ]; then
        echo "  this host: http://localhost:8080/ (the page's websocket wants :80, use ws_command.py here)"
    else
        echo "  this host: not routed (rootless), use --root, --publish or podman unshare --rootless-netns"
    fi
else
    RUN_ARGS+=(--network host)
fi
if [ -t 0 ]; then
    RUN_ARGS+=(-t)
fi
if [ "${CONTAINER_RUNTIME}" = "docker" ] || [ "${ROOT}" = "1" ]; then
    RUN_ARGS+=(--user "${SUDO_UID:-$(id -u)}:${SUDO_GID:-$(id -g)}")
fi
if [ "${TOOL}" = "gdb" ]; then
    RUN_ARGS+=(--cap-add=SYS_PTRACE --security-opt seccomp=unconfined)
fi

if "${RT[@]}" image exists local_build_pico_os_linux 2>/dev/null; then
    echo "[run] checking the local_build_pico_os_linux image against linux.Dockerfile"
else
    echo "[run] no local_build_pico_os_linux image$([ "${RT[0]}" = "sudo" ] && echo " for root") yet: building it (a few minutes)"
fi

if ! "${RT[@]}" build --network host -t local_build_pico_os_linux -f "${OS_DIR}/.devcontainer/linux.Dockerfile" "${OS_DIR}/.devcontainer"; then
    echo "error: building the image failed" >&2
    exit 1
fi

echo "[run] starting ${PROGRAM#${CONTAINER_APP_ROOT}/}${TOOL:+ under ${TOOL}}${*:+ with: $*}"
echo "[run] its log follows$( [[ " $* " == *" --trace-stdout "* ]] || echo " (only crashes and exits: leave out --no-trace-stdout for everything)"); Ctrl-C stops it"

exec "${RT[@]}" run "${RUN_ARGS[@]}" \
    "${MOUNTS[@]}" \
    -w "${CONTAINER_APP_ROOT}" \
    local_build_pico_os_linux \
    "${TOOL_CMD[@]}" "${PROGRAM}" "$@"
