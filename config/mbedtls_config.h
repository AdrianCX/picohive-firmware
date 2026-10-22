#pragma once

/* Workaround for some mbedtls source files using INT_MAX without including limits.h */
#include <limits.h>
#include "lwipopts_common.h"

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#define MBEDTLS_HAVE_ASM

//   IN  - We are forced to use 16K since most servers/browsers don't respect negotiation parameters
//   OUT - Anything we send must be under this, generally driven by largest handshake message
//         Requires fixes to mbedtls via fixes.sh
#ifndef MBEDTLS_SSL_IN_CONTENT_LEN
#define MBEDTLS_SSL_IN_CONTENT_LEN     16384
#endif

#ifndef MBEDTLS_SSL_OUT_CONTENT_LEN
#define MBEDTLS_SSL_OUT_CONTENT_LEN    2048
#endif

#if TCP_WND < (MBEDTLS_SSL_IN_CONTENT_LEN + 64)
#error "TCP_WND is smaller than one TLS record (MBEDTLS_SSL_IN_CONTENT_LEN), received records that big would stall"
#endif

#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH

// Avoid holding things in RAM when checking trusted roots
#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT

// AES-GCM only, no CBC suites, servers in tls_domains.txt  browser offers ECDHE-*-AES-GCM
// Without CBC each record buffer has no MAC and padding room to reserve (~576 bytes less per connection).
//#define MBEDTLS_CIPHER_MODE_CBC
//#define MBEDTLS_ECP_DP_SECP192R1_ENABLED
//#define MBEDTLS_ECP_DP_SECP224R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
//#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
//#define MBEDTLS_ECP_DP_SECP192K1_ENABLED
//#define MBEDTLS_ECP_DP_SECP224K1_ENABLED
//#define MBEDTLS_ECP_DP_SECP256K1_ENABLED
//#define MBEDTLS_ECP_DP_BP256R1_ENABLED
//#define MBEDTLS_ECP_DP_BP384R1_ENABLED
//#define MBEDTLS_ECP_DP_BP512R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
//#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_HKDF_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
//#define MBEDTLS_PKCS5_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_AES_FEWER_TABLES

/* TLS 1.2 */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_GCM_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
// Fast modular reduction for the NIST curves, without it a P-256/P-384 handshake blocks for 10s+
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ASN1_WRITE_C

#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

/* server items */
#define MBEDTLS_SSL_ALPN
//#define MBEDTLS_SSL_SESSION_TICKETS
//#define MBEDTLS_SSL_TICKET_C
//#define MBEDTLS_SSL_CACHE_C

// In case someone wants to use frame-pointers
#define MULADDC_CANNOT_USE_R7

/* Needed for telegram */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

/* save some memory and avoid potentially oom */
#define MBEDTLS_MPI_MAX_SIZE 512
#define MBEDTLS_MPI_WINDOW_SIZE 1
