#!/bin/bash -e
#
# Run an emulated linux build via podman, optionally with valgrind or gdb - see .devcontainer/run_host.sh for parameters
#
export CONTAINER_RUNTIME=podman
exec "$(cd "$(dirname "$0")" && pwd)/.devcontainer/run_host.sh" "$@"
