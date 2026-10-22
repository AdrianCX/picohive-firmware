FROM ubuntu:24.04

RUN apt-get update && apt-get install -y cmake make gcc-arm-none-eabi libnewlib-arm-none-eabi xxd python3 g++ bash dos2unix git openssl python3-cryptography python3-yaml python3-pip

RUN pip3 install --no-cache-dir --break-system-packages minify-html==0.18.1

SHELL ["/bin/bash", "-c"]
