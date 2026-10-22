#!/bin/bash -e

# Container side of the build. Builds whatever app root surrounds this
# repository; see .devcontainer/find_app_root.sh.

if [ -z "$1" ]; then
    echo "./build.sh PICO_BOARD"
    echo "example: ./build.sh pico2_w"
    echo "example: ./build.sh pico_w"
    exit 1
fi

export PICO_BOARD="$1"

OS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${OS_DIR}/.devcontainer/find_app_root.sh"
APP_ROOT="$(find_app_root "${OS_DIR}")"

BUILD_DIR="${APP_ROOT}/build"
JOBS="$(nproc)"

drop_stale_cache() {
    local build_dir="$1" src_dir="$2" cached cached_build
    local cache="${build_dir}/CMakeCache.txt"

    [ -f "${cache}" ] || return 0
    cached="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache}")"
    cached_build="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "${cache}")"

    if [ -n "${cached_build}" ] && [ "${cached_build}" != "$(cd "${build_dir}" && pwd)" ]; then
        cached="${cached} (in ${cached_build})"
    fi

    if [ -n "${cached}" ] && [ "${cached}" != "${src_dir}" ]; then
        echo "Discarding ${build_dir}: configured for ${cached}, now building ${src_dir}"
        rm -rf "${build_dir}"
    fi
}

"${OS_DIR}/.devcontainer/fixes.sh" "${OS_DIR}"

if [ "${PICO_BOARD}" = "linux" ]; then
    drop_stale_cache "${BUILD_DIR}/linux" "${APP_ROOT}"
    cmake -S "${APP_ROOT}" -B "${BUILD_DIR}/linux" -DPICO_BOARD=linux -DCMAKE_BUILD_TYPE="${PICO_OS_BUILD_TYPE:-Debug}" ${PICO_OS_CMAKE_ARGS}
    make -C "${BUILD_DIR}/linux" -j"${JOBS}"

    echo ""
    echo "Built under ${BUILD_DIR}/linux/:"
    find "${BUILD_DIR}/linux" -maxdepth 1 -type f -executable ! -name '*.stripped' -printf '  %f\n'
    echo ""
    echo "Run it in the linux container: os/run_via_podman.sh [--valgrind] [--net] (see docs/linux.md)"
    exit 0
fi

if [ ! -f /usr/local/bin/picotool ]; then
    drop_stale_cache "${BUILD_DIR}/picotool" "${OS_DIR}/submodules/picotool"
    mkdir -p "${BUILD_DIR}/picotool"
    cd "${BUILD_DIR}/picotool"
    cmake -DPICO_SDK_PATH="${OS_DIR}/submodules/pico-sdk/" "${OS_DIR}/submodules/picotool/"
    make -j"${JOBS}"
    make install
fi

drop_stale_cache "${BUILD_DIR}/${PICO_BOARD}" "${APP_ROOT}"
mkdir -p "${BUILD_DIR}/${PICO_BOARD}"
cd "${BUILD_DIR}/${PICO_BOARD}"
cmake -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="${PICO_BOARD}" "${APP_ROOT}"
make -j"${JOBS}"

echo ""
echo "All builds are under ${BUILD_DIR}/${PICO_BOARD}/"
echo "You can upload the UF2s to the web UI on appropriate builds or copy via USB"
echo ""
echo "To see pico logs:"
echo "- on the board's config page, set the MQTT host to this machine and the logging port (23500 by default)"
echo "- to see logs displayed: python3 ${OS_DIR}/util/log_decrypt.py --port 23500"
echo "- to save logs to a folder: python3 ${OS_DIR}/util/log_decrypt.py --port 23500 --out-dir logs"
echo ""

# Try to detect stack overflows: the outcome is printed here, the 10 deepest
# call stacks go to <app>.stack.txt next to each ELF for reviewing later.
stack_limit_args=()
if [ -n "${PICO_STACK_LIMIT}" ]; then
    stack_limit_args=(--limit "${PICO_STACK_LIMIT}")
fi

stack_status=0
while read -r elf; do
    echo ""
    # From the app root, so the paths printed are the ones on the host too.
    elf=$(realpath --relative-to="${APP_ROOT}" "${elf}")
    (cd "${APP_ROOT}" && "${OS_DIR}/util/stack_depth.py" "${elf}" --top 10 --report "${elf%.elf}.stack.txt" \
        "${stack_limit_args[@]}") || stack_status=1
    # Static RAM - singletons included - next to it in <app>.ram.txt; it only reports.
    (cd "${APP_ROOT}" && "${OS_DIR}/util/ram_usage.py" "${elf}" --top 10 --report "${elf%.elf}.ram.txt") || true
done < <(find "${BUILD_DIR}/${PICO_BOARD}" -maxdepth 4 -name '*.elf' \
              ! -path "${BUILD_DIR}/${PICO_BOARD}/*/pico-sdk/*" ! -path "${BUILD_DIR}/${PICO_BOARD}/*/_deps/*" \
              ! -path "${BUILD_DIR}/${PICO_BOARD}/*/test/*" | sort)

exit ${stack_status}
