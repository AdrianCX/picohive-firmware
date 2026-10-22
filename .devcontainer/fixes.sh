#!/bin/bash -ex

# quick hacks
#
# Takes the path of this repository as $1, defaulting to the one this script
# lives in.

OS_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

fail() {
    echo "fixes.sh: $*" >&2
    exit 1
}

# 1. Change needed because IRQ is not re-enabled when going to sleep. So nobody will wake mutex to be notified of new data.
CYW43_LL="${OS_DIR}/submodules/pico-sdk/lib/cyw43-driver/src/cyw43_ll.c"
if [ ! -f "${CYW43_LL}" ]; then
    fail "no ${CYW43_LL}"
fi

if ! grep -q "CYW43_POST_POLL_HOOK; CYW43_SDPCM_SEND_COMMON_WAIT;" "${CYW43_LL}"; then
    perl -pi -e 's#            CYW43_SDPCM_SEND_COMMON_WAIT;#CYW43_POST_POLL_HOOK; CYW43_SDPCM_SEND_COMMON_WAIT;#g' "${CYW43_LL}"
fi

if ! grep -q "CYW43_POST_POLL_HOOK; CYW43_SDPCM_SEND_COMMON_WAIT;" "${CYW43_LL}"; then
    fail "the cyw43 sleep fix did not apply to ${CYW43_LL}, the code it changes has moved"
fi

# 2. mbedtls_hardware_poll() overflow: the last piece is filled with MIN(len, 8) instead of what is left, writing up to 7 bytes past the caller's buffer. TLS goes through it, and the buffer is on the heap.
PICO_MBEDTLS="${OS_DIR}/submodules/pico-sdk/src/rp2_common/pico_mbedtls/pico_mbedtls.c"
if [ ! -f "${PICO_MBEDTLS}" ]; then
    fail "no ${PICO_MBEDTLS}"
fi

if grep -q "MIN(len, sizeof(rand_data))" "${PICO_MBEDTLS}"; then
    perl -pi -e 's#MIN\(len, sizeof\(rand_data\)\)#MIN(len - *olen, sizeof(rand_data))#g' "${PICO_MBEDTLS}"
fi

if ! grep -q "MIN(len - \*olen, sizeof(rand_data))" "${PICO_MBEDTLS}"; then
    fail "the mbedtls_hardware_poll() fix did not apply to ${PICO_MBEDTLS}, check whether the SDK fixed the overflow itself"
fi

# 3. mbedtls TLS 1.2 client and max fragment length: after the server's hello the client sizes its receive limit (and, with MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH, its receive buffer) from the length it asked for, whether or not the server agreed, and refuses larger records. Most servers ignore the request (Google, OpenRouter, ...), and OpenRouter sends 16K records. Fixed: the client keeps the server's answer, and the receive limit is the agreed length, else MBEDTLS_SSL_IN_CONTENT_LEN, as on the server side. Records sent stay at the length asked for. Applied to both platforms (one mbedtls source).
SSL_TLS="${OS_DIR}/submodules/pico-sdk/lib/mbedtls/library/ssl_tls.c"
SSL_TLS12_CLIENT="${OS_DIR}/submodules/pico-sdk/lib/mbedtls/library/ssl_tls12_client.c"
for f in "${SSL_TLS}" "${SSL_TLS12_CLIENT}"; do
    if [ ! -f "${f}" ]; then
        fail "no ${f}"
    fi
done

if ! grep -q "pico_os fix 3" "${SSL_TLS12_CLIENT}"; then
    perl -0pi -e 's#(        return MBEDTLS_ERR_SSL_ILLEGAL_PARAMETER;\n    }\n)\n    return 0;\n}\n\#endif /\* MBEDTLS_SSL_MAX_FRAGMENT_LENGTH \*/#$1\n    ssl->session_negotiate->mfl_code = buf[0]; /* pico_os fix 3: the server agreed */\n    return 0;\n}\n\#endif /* MBEDTLS_SSL_MAX_FRAGMENT_LENGTH */#' "${SSL_TLS12_CLIENT}"
fi

if ! grep -q "pico_os fix 3" "${SSL_TLS}"; then
    perl -0pi -e 's#    if \(ssl->conf->endpoint == MBEDTLS_SSL_IS_CLIENT &&\n        ssl->state >= MBEDTLS_SSL_SERVER_HELLO_DONE\) \{\n        return ssl_mfl_code_to_length\(ssl->conf->mfl_code\);#    if (0 /* pico_os fix 3: the agreed length, below */ && ssl->conf->endpoint == MBEDTLS_SSL_IS_CLIENT &&\n        ssl->state >= MBEDTLS_SSL_SERVER_HELLO_DONE) {\n        return ssl_mfl_code_to_length(ssl->conf->mfl_code);#' "${SSL_TLS}"
fi

if [ "$(grep -c "pico_os fix 3" "${SSL_TLS12_CLIENT}")" != "1" ] || [ "$(grep -c "pico_os fix 3" "${SSL_TLS}")" != "1" ]; then
    fail "the mbedtls max fragment length fix did not apply to ${SSL_TLS} / ${SSL_TLS12_CLIENT}, the code it changes has moved"
fi

# 4. mbedtls receive limit when no max fragment length was agreed: it is the smaller of MBEDTLS_SSL_IN_CONTENT_LEN and MBEDTLS_SSL_OUT_CONTENT_LEN (MBEDTLS_TLS_EXT_ADV_CONTENT_LEN), and with MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH the receive buffer shrinks to it after the handshake. With 16K in / 2K out (config/mbedtls_config.h) every connection would then refuse records over 2K, which browsers and most servers send. Fixed: no agreed length means MBEDTLS_SSL_IN_CONTENT_LEN. The send side already starts from MBEDTLS_SSL_OUT_CONTENT_LEN. Applied to both platforms (one mbedtls source).
if ! grep -q "pico_os fix 4" "${SSL_TLS}"; then
    perl -0pi -e 's#        read_mfl = ssl_mfl_code_to_length\(ssl->session_(out|negotiate)->mfl_code\);#        read_mfl = (ssl->session_$1->mfl_code == MBEDTLS_SSL_MAX_FRAG_LEN_NONE) ? MBEDTLS_SSL_IN_CONTENT_LEN /* pico_os fix 4 */ : ssl_mfl_code_to_length(ssl->session_$1->mfl_code);#g' "${SSL_TLS}"
fi

if [ "$(grep -c "pico_os fix 4" "${SSL_TLS}")" != "2" ]; then
    fail "the mbedtls receive limit fix did not apply to ${SSL_TLS}, the code it changes has moved"
fi
