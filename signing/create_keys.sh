#!/bin/bash -e

# Makes one numbered key version: a whole set, both pairs, both halves.
#
#   <app>/keys/versions/<n>/ota_signing_key.pem  signs upgrades
#   keys/versions/<n>/ota_signing_key.pub.pem    compiled in, checks upgrades
#   keys/versions/<n>/ota_encryption_key.pem     compiled in, decrypts upgrades
#   keys/versions/<n>/ota_encryption_key.pub.pem encrypts upgrades
#
# The build makes a version it needs and does not have, so this is only for
# making one ahead of time. Which versions a build uses comes from
# PICO_OS_KEY_VERSION and PICO_OS_NEXT_KEY_VERSION; see the README.
#
# Usage: ./create_keys.sh <version> [--force] [key root]

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [--force] [key root]" >&2
    exit 1
fi

VERSION="$1"
shift

FORCE=0
if [ "$1" = "--force" ]; then
    FORCE=1
    shift
fi

if [ -n "$1" ]; then
    KEY_ROOT="$(realpath "$1")"
elif [ -n "${PICO_OS_KEY_DIR}" ]; then
    KEY_ROOT="$(realpath "${PICO_OS_KEY_DIR}")"
else
    # keys/ next to the application's src/, the same place the build looks.
    OS_DIR="$(cd "$(dirname "$0")/.." && pwd)"
    source "${OS_DIR}/.devcontainer/find_app_root.sh"
    KEY_ROOT="$(find_app_root "${OS_DIR}")/keys"
fi

DIR="${KEY_ROOT}/versions/${VERSION}"

SIGNING="${DIR}/ota_signing_key.pem"
SIGNING_PUBLIC="${DIR}/ota_signing_key.pub.pem"
ENCRYPTION="${DIR}/ota_encryption_key.pem"
ENCRYPTION_PUBLIC="${DIR}/ota_encryption_key.pub.pem"

if [ -f "${SIGNING}" ] && [ "${FORCE}" != "1" ]; then
    echo "${DIR} already has keys, pass --force to replace them" >&2
    exit 1
fi

mkdir -p "${DIR}"

# Signing: RSA-3072. The device only ever does the public half of this one, so
# it is cheap on the chip and can afford the size.
( umask 077 && openssl genpkey -quiet -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "${SIGNING}" )
openssl pkey -in "${SIGNING}" -pubout -out "${SIGNING_PUBLIC}"

# Encryption: ECDH on P-256. The device does the private half here, and a
# scalar multiplication is under a second on a Cortex-M0+ where RSA-2048 was
# four seconds.
( umask 077 && openssl genpkey -quiet -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "${ENCRYPTION}" )
openssl pkey -in "${ENCRYPTION}" -pubout -out "${ENCRYPTION_PUBLIC}"

echo "Key version ${VERSION}: ${DIR}"
echo "Back it up. A device only takes firmware signed with the key version it is running."
