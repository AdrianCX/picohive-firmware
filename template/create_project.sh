#!/bin/bash -e

# Creates a new application repository built on this one, the way the epaper
# project is: a small repo of its own carrying this repository as its "os/"
# submodule, with everything else (SDK, libraries, build scripts) coming from
# there.
#
#   os/template/create_project.sh thermostat
#   os/template/create_project.sh --board pico_w --title "Garden watering" irrigation ~/work/irrigation
#   os/template/create_project.sh --board linux sensor_hub
#
# The project builds for any board; --board only picks what its README and the
# steps printed at the end use: a pico board (pico2_w, pico_w) or linux (a
# Linux program with the same libraries, see os/docs/linux.md).
#
# The files it copies are in template/files/, with @PROJECT_NAME@,
# @PROJECT_TITLE@ and @PICO_BOARD@ substituted; a path component named
# "_dot_x" is written out as ".x".

TEMPLATE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OS_DIR="$(cd "${TEMPLATE_DIR}/.." && pwd)"
FILES_DIR="${TEMPLATE_DIR}/files"

FALLBACK_OS_URL="https://github.com/AdrianCX/picohive-firmware.git"
FALLBACK_OS_BRANCH="main"

PICO_BOARD="pico2_w"
PROJECT_TITLE=""
OS_URL=""
OS_BRANCH=""
DO_GIT=1
DO_SUBMODULES=1
DO_KEYS=1
FORCE=0

usage() {
    cat <<EOF
Usage: ${BASH_SOURCE[0]} [options] <project-name> [destination]

Creates <destination> (default ./<project-name>) as a new pico application
repository with this repository added as its os/ submodule.

Options:
  -t, --title TITLE     human readable name used in the README and web UI
                        (default: project name with underscores as spaces)
  -b, --board BOARD     the board the README and the next steps use: pico2_w,
                        pico_w or linux (default: ${PICO_BOARD}); the project
                        builds for any of them
  -u, --os-url URL      URL the os submodule is added from; a local path works
                        and is much faster (default: this checkout's origin)
  -r, --os-branch NAME  branch the os submodule tracks
                        (default: the branch this checkout is on)
      --no-submodules   add the os submodule but skip the recursive clone of
                        the SDK and picotool (several hundred MB)
      --no-keys         skip the OTA key version the project signs upgrades
                        with; the first build makes one instead
      --no-git          only copy the files, no repository, no submodules
  -f, --force           write into an existing, possibly non empty directory
  -h, --help            this text
EOF
}

die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        -t|--title)      PROJECT_TITLE="$2"; shift 2 ;;
        -b|--board)      PICO_BOARD="$2"; shift 2 ;;
        -u|--os-url)     OS_URL="$2"; shift 2 ;;
        -r|--os-branch)  OS_BRANCH="$2"; shift 2 ;;
        --no-submodules) DO_SUBMODULES=0; shift ;;
        --no-keys)       DO_KEYS=0; shift ;;
        --no-git)        DO_GIT=0; shift ;;
        -f|--force)      FORCE=1; shift ;;
        -h|--help)       usage; exit 0 ;;
        --)              shift; break ;;
        -*)              usage >&2; die "unknown option $1" ;;
        *)               break ;;
    esac
done

[ $# -ge 1 ] || { usage >&2; exit 1; }
[ $# -le 2 ] || { usage >&2; die "too many arguments"; }

PROJECT_NAME="$1"
DEST="${2:-./${PROJECT_NAME}}"

# The name becomes a CMake target, an executable and a C string.
[[ "${PROJECT_NAME}" =~ ^[A-Za-z][A-Za-z0-9_]*$ ]] \
    || die "'${PROJECT_NAME}' is not usable as a target name: letters, digits and underscores, starting with a letter"

if [ -z "${PROJECT_TITLE}" ]; then
    PROJECT_TITLE="${PROJECT_NAME//_/ }"
fi

[ -d "${FILES_DIR}" ] || die "no template files at ${FILES_DIR}"

#
# Destination
#
if [ -e "${DEST}" ]; then
    [ -d "${DEST}" ] || die "${DEST} exists and is not a directory"
    if [ "${FORCE}" != "1" ] && [ -n "$(ls -A "${DEST}")" ]; then
        die "${DEST} is not empty, use --force to write into it anyway"
    fi
fi

# A repository inside a repository is almost always a mistake here: the parent
# would swallow the new project as untracked files rather than a project.
# Checked against the nearest existing ancestor, before anything is created.
if [ "${DO_GIT}" = "1" ] && [ "${FORCE}" != "1" ] && [ ! -d "${DEST}/.git" ]; then
    probe="${DEST}"
    while [ ! -d "${probe}" ]; do
        probe="$(dirname "${probe}")"
    done

    if enclosing="$(git -C "${probe}" rev-parse --show-toplevel 2>/dev/null)"; then
        die "${DEST} would be inside the git repository at ${enclosing};
       pick a destination outside it, or pass --force"
    fi
fi

mkdir -p "${DEST}"
DEST="$(cd "${DEST}" && pwd)"

#
# Where the os submodule comes from
#
if [ -z "${OS_URL}" ]; then
    OS_URL="$(git -C "${OS_DIR}" remote get-url origin 2>/dev/null || true)"
    [ -n "${OS_URL}" ] || OS_URL="${FALLBACK_OS_URL}"
fi

if [ -z "${OS_BRANCH}" ]; then
    OS_BRANCH="$(git -C "${OS_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
    if [ -z "${OS_BRANCH}" ] || [ "${OS_BRANCH}" = "HEAD" ]; then
        OS_BRANCH="${FALLBACK_OS_BRANCH}"
    fi
fi

# Cloning a submodule over the file transport is refused by default since git
# 2.38 (CVE-2022-39253), so allow it explicitly when the url is a local path.
GIT_CLONE_OPTS=()
if [ -d "${OS_URL}" ]; then
    OS_URL="$(cd "${OS_URL}" && pwd)"
    GIT_CLONE_OPTS=(-c protocol.file.allow=always)
fi

echo "Creating ${PROJECT_NAME} in ${DEST}"
echo "  title:     ${PROJECT_TITLE}"
echo "  board:     ${PICO_BOARD}"
echo "  os:        ${OS_URL} (branch ${OS_BRANCH})"
echo ""

#
# Copy the template, substituting as we go
#
sed_escape() { printf '%s' "$1" | sed -e 's/[&|\\]/\\&/g'; }

SED_SCRIPT="s|@PROJECT_NAME@|$(sed_escape "${PROJECT_NAME}")|g
s|@PROJECT_TITLE@|$(sed_escape "${PROJECT_TITLE}")|g
s|@PICO_BOARD@|$(sed_escape "${PICO_BOARD}")|g"

while IFS= read -r -d '' src; do
    rel="${src#"${FILES_DIR}/"}"

    # _dot_gitignore -> .gitignore, in any path component.
    out="${DEST}/${rel//_dot_/.}"

    mkdir -p "$(dirname "${out}")"

    if grep -Iq . "${src}" 2>/dev/null; then
        sed -e "${SED_SCRIPT}" "${src}" > "${out}"
    else
        # Binary template file (an image, a font): copy it through untouched.
        cp "${src}" "${out}"
    fi

    chmod --reference="${src}" "${out}" 2>/dev/null || true
    echo "  ${rel//_dot_/.}"
done < <(find "${FILES_DIR}" -type f -print0 | sort -z)

#
# The project's own OTA keys. Upgrades are signed with these and only a build
# carrying the matching halves accepts them, so they belong to the project, not
# to the os checkout it was made from.
#
if [ "${DO_KEYS}" = "1" ]; then
    echo ""
    if [ -d "${DEST}/keys/versions/0" ]; then
        echo "keys/versions/0 already present, leaving it alone"
    else
        "${OS_DIR}/signing/create_keys.sh" 0 "${DEST}/keys"
    fi
fi

if [ "${DO_GIT}" != "1" ]; then
    echo ""
    echo "Files copied. os/ was not added: rerun without --no-git, or add it by hand:"
    echo "  git -C ${DEST} submodule add -b ${OS_BRANCH} ${OS_URL} os"
    exit 0
fi

#
# Repository and submodules
#
cd "${DEST}"

if [ ! -d .git ]; then
    echo ""
    echo "Initialising repository"
    git init -q
    # Old git defaults the first branch to master; the projects use main.
    git symbolic-ref HEAD refs/heads/main
fi

if [ -e os ]; then
    echo "os/ already present, leaving it alone"
else
    echo "Adding os submodule (${OS_URL}, branch ${OS_BRANCH})"
    git "${GIT_CLONE_OPTS[@]}" submodule add -b "${OS_BRANCH}" -- "${OS_URL}" os
fi

if [ "${DO_SUBMODULES}" = "1" ]; then
    echo "Fetching the SDK and picotool inside os/ (this takes a while)"
    git -C os "${GIT_CLONE_OPTS[@]}" submodule update --init --recursive
else
    echo "Skipping the recursive clone inside os/ (--no-submodules)"
fi

git add -A

if git commit -q -m "Initial ${PROJECT_NAME}, from the os template" 2>/dev/null; then
    echo "Committed the initial tree"
else
    echo "Nothing committed (set user.name/user.email and commit by hand)"
fi

if [ "${PICO_BOARD}" = "linux" ]; then
cat <<EOF

Done. Next:

  cd ${DEST}
  os/build_via_podman.sh linux                    # or build_via_docker.sh
  os/run_via_podman.sh --net -- --trace-stdout     # at 10.89.50.10 on the "picohive" network

The program lands in build/linux/${PROJECT_NAME}, next to
${PROJECT_NAME}.encrypted, which a running instance takes over the air. The same
sources build for a board: os/build_via_podman.sh pico2_w

Then:
  - back up keys/, it is gitignored and an instance only takes upgrades signed
    with the key version it is running
  - it starts unconfigured, as a board in access point mode does: write the
    config on its page (port 80), see os/docs/linux.md
  - fill in src/main.cpp callbacks and src/config/general_config.h
EOF
exit 0
fi

cat <<EOF

Done. Next:

  cd ${DEST}
  os/build_via_podman.sh ${PICO_BOARD}     # or build_via_docker.sh

The firmware lands in build/${PICO_BOARD}/${PROJECT_NAME}.uf2, to flash over
USB, next to ${PROJECT_NAME}.uf2.encrypted, which a running board takes over the air.
The same sources build for linux: os/build_via_podman.sh linux (os/docs/linux.md)

Then:
  - back up keys/, it is gitignored and a device only takes firmware signed
    with the key version it is running
  - set the MQTT host and logging port on the board's config page to get logs
  - fill in src/main.cpp callbacks and src/config/general_config.h
  - the board comes up as an access point until wifi is configured through
    the web UI it serves (src/web/page.yaml)
EOF
