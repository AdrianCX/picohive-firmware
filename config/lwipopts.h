#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// 1. Full TCP/mbedtls debug logs
//#include "lwipopts_debug_full.h"

// 2. No LWIP/mbedtls debug logs.
#include "lwipopts_debug_off.h"

#include "lwipopts_common.h"

#define LWIP_ALTCP               1
#define LWIP_ALTCP_TLS           1
#define LWIP_ALTCP_TLS_MBEDTLS   1
#define ALTCP_MBEDTLS_USE_SESSION_TICKETS 0
#define ALTCP_MBEDTLS_USE_SESSION_CACHE 0

#endif

