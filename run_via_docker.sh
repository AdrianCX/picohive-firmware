#!/bin/bash -e
#
# Run an emulated linux build via docker, optionally with valgrind or gdb - see .devcontainer/run_host.sh for parameters
#
export CONTAINER_RUNTIME=docker
exec "$(cd "$(dirname "$0")" && pwd)/.devcontainer/run_host.sh" "$@"
