# Linux

[← Back](../README.md)

## Introduction

Apps can be built using Linux mocks to test out web page and higher level logic under valgrind/gdb.

How to do that:

```bash
os/build_via_podman.sh linux                     # build/linux/<app>, <app>.encrypted (OTA)
os/run_via_podman.sh --net                       # on its own IP, 10.89.50.10
os/run_via_podman.sh --valgrind --net            # under valgrind
os/run_via_podman.sh --gdb                       # under gdb
```

Or for docker: `build_via_docker.sh` / `run_via_docker.sh` 

Apps start with 8080 and 8443 as default ports, you can override via:

```bash
build/linux/<app> --port-offset 0      # 80/443, needs CAP_NET_BIND_SERVICE or ip_unprivileged_port_start
build/linux/<app> --port-offset 1000   # 1080/1443
```

The first start is unconfigured, like a new board, open the page and configure wifi at least.


## Differences

Hardware is unavailable on PC, hardware libraries are mocked
LWIP is replaced with some quick generated code
Mbedtls is kept the same but linked directly

