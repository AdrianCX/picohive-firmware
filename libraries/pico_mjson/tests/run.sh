#!/bin/bash
# Host tests for pico_mjson: reading by path, unescaping, integers, iteration,
# the writer and json_format. Built the way the firmware builds - no exceptions,
# and mjson with the parts it leaves out:
#   os/libraries/pico_mjson/tests/run.sh
HERE=$(cd "$(dirname "$0")" && pwd)

if [ -z "$IN_CONTAINER" ]; then
    exec podman run --rm -e IN_CONTAINER=1 -v "$HERE/../..":/libs:Z local_build_pico_https bash /libs/pico_mjson/tests/run.sh
fi

cd /libs/pico_mjson/tests

DEFINES="-DMJSON_ENABLE_RPC=0 -DMJSON_ENABLE_BASE64=0 -DMJSON_ENABLE_MERGE=0 -DMJSON_ENABLE_PRETTY=0"

gcc -std=c99 -Wall -O1 -fsanitize=address,undefined $DEFINES -I/libs/pico_mjson/mjson \
    -c /libs/pico_mjson/mjson/mjson.c -o /tmp/mjson.o || exit 1

g++ -std=c++20 -Wall -Wextra -fno-exceptions -O1 -fsanitize=address,undefined $DEFINES \
    -I. -I/libs/pico_mjson -I/libs/pico_mjson/mjson \
    mjson_test.cpp /libs/pico_mjson/pico_mjson.cpp /tmp/mjson.o -o /tmp/t || exit 1

/tmp/t
