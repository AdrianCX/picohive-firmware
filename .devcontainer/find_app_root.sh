# Sourced helper. Locates the directory holding the top level CMakeLists.txt

find_app_root() {
    local os_dir="$1"
    local dir super

    if [ -n "${PICO_APP_ROOT:-}" ]; then
        (cd "${PICO_APP_ROOT}" && pwd)
        return
    fi

    super="$(git -C "${os_dir}" rev-parse --show-superproject-working-tree 2>/dev/null || true)"
    if [ -n "${super}" ] && [ -f "${super}/CMakeLists.txt" ]; then
        (cd "${super}" && pwd)
        return
    fi

    dir="${os_dir}"
    while [ "${dir}" != "/" ]; do
        dir="$(dirname "${dir}")"
        if [ -f "${dir}/CMakeLists.txt" ] && grep -q "pico_os_pre_project" "${dir}/CMakeLists.txt"; then
            echo "${dir}"
            return
        fi
    done

    echo "${os_dir}"
}
