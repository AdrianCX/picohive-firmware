#!/bin/bash
# The board's HTTPS certificate: an EC P-256 key and a self signed certificate
# for it, as the two headers os/libraries/pico_tls compiles in.
#
#   os/util/make_cert.sh 192.168.100.69                 the board's address
#   os/util/make_cert.sh 192.168.100.69 alarm.local     and any names it has
#   DAYS=730 os/util/make_cert.sh 192.168.100.69        a shorter life (default 3650)
#
# Writes src/certificate/key.h, cert.h and cert.crt in the app (found as the
# build finds it; PICO_APP_ROOT=<dir> to pick another, e.g. test/demo).
# Rebuild afterwards: the key is in the firmware, not in flash.
#
# P-256 because the pico is the one doing the handshake: ECDHE-ECDSA on P-256 is
# what config/mbedtls_config.h enables, and it is far quicker and lighter on the
# pico than RSA-2048.
#
# Every address and name the page is opened by goes in, as subject alternative
# names: browsers ignore the common name, and a certificate that does not name
# the address in the bar is refused even once it is trusted. To have it trusted -
# which is what makes the page a secure context, so the gamepad shows up -
# import cert.crt into the browser or the system as a trusted authority.
#
# key.h is the private key. Anyone who has it can pretend to be the board:
# keep src/certificate/ out of git.

set -euo pipefail

if [ $# -lt 1 ]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

DAYS="${DAYS:-3650}"
OS_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$OS_DIR/.devcontainer/find_app_root.sh"
OUT="$(find_app_root "$OS_DIR")/src/certificate"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SAN=""
for name in "$@"; do
    if [[ "$name" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || [[ "$name" == *:* ]]; then
        SAN="${SAN:+$SAN,}IP:$name"
    else
        SAN="${SAN:+$SAN,}DNS:$name"
    fi
done

openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/key.pem"

openssl req -new -x509 -sha256 -days "$DAYS" -key "$WORK/key.pem" -out "$WORK/cert.pem" \
    -subj "/CN=$1" \
    -addext "subjectAltName=$SAN" \
    -addext "keyUsage=critical,digitalSignature,keyCertSign" \
    -addext "extendedKeyUsage=serverAuth"

# DER, which is what mbedtls is handed: the SEC1 form of the key, and the
# certificate as is.
openssl ec -in "$WORK/key.pem" -outform DER -out "$WORK/key.der" 2>/dev/null
openssl x509 -in "$WORK/cert.pem" -outform DER -out "$WORK/cert.der"

mkdir -p "$OUT"

# What xxd -i ./key.der would write - arrays named __key_der and __cert_der,
# which is what tls_listener.cpp expects - without needing xxd on the machine.
header() {
    local guard="$1" file="$2"
    python3 - "$WORK/$file" "$guard" "__${file%.der}_der" > "$OUT/${file%.der}.h" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
guard, name = sys.argv[2], sys.argv[3]
print(f"#ifndef {guard}\n#define {guard}\nconst unsigned char {name}[] = {{")
for i in range(0, len(data), 12):
    end = "," if i + 12 < len(data) else ""
    print("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + end)
print(f"}};\nconst unsigned int {name}_len = {len(data)};\n#endif")
PY
}

header _KEY_H key.der
header _CERT_H cert.der
cp "$WORK/cert.pem" "$OUT/cert.crt"

echo "$OUT written for: $SAN, $DAYS days"
openssl x509 -in "$OUT/cert.crt" -noout -subject -enddate
