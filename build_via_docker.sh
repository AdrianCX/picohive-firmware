#!/bin/bash -e

export CONTAINER_RUNTIME=docker
exec "$(cd "$(dirname "$0")" && pwd)/.devcontainer/build_host.sh" "$@"
