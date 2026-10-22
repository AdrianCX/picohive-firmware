FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake make gcc g++ binutils git python3 python3-cryptography python3-yaml python3-pip \
        openssl ca-certificates valgrind gdb xxd file procps \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir --break-system-packages minify-html==0.18.1
