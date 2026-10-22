#pragma once

#include "general_config.h"
#include "pico_logger.h"

// Send debug traces over UDP to remote host
#define LWIP_PLATFORM_DIAG(x) do {trace x;} while(0)
#define LWIP_PLATFORM_ASSERT(x) do {fail("Assertion \"%s\" failed at line %d in %s\n", x, __LINE__, __FILE__);} while(0)

#ifndef NO_SYS
#define NO_SYS                      1
#endif

#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif

// Using malloc to expand as needed
#define MEM_USE_POOLS               0
#define MEM_LIBC_MALLOC             1
#define MEMP_MEM_MALLOC             1
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (128*1024)
#define TCP_OVERSIZE                1

// General tuning due to low memory
#define TCP_TMR_INTERVAL   25
#define TCP_FAST_INTERVAL   50
#define TCP_SLOW_INTERVAL   100
#define TCP_FIN_WAIT_TIMEOUT   3000
#define TCP_SYN_RCVD_TIMEOUT   3000

#define MEMP_NUM_TCP_SEG            0
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_MSS                     1460

/* TCP_WND must hold one whole TLS record (MBEDTLS_SSL_IN_CONTENT_LEN plus its header, IV and tag) or the connection stalls */
#ifndef TCP_WND
#define TCP_WND                     (12 * TCP_MSS)
#endif

#ifndef TCP_OOSEQ_MAX_PBUFS
#define TCP_OOSEQ_MAX_PBUFS         4
#endif

#ifndef TCP_SND_BUF
#define TCP_SND_BUF                 (6 * TCP_MSS)
#endif

#define TCP_SND_QUEUELEN            ((2 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#ifndef LWIP_DEBUG_CHECKS
#define LWIP_DEBUG_CHECKS           0
#endif

#if LWIP_DEBUG_CHECKS
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#define MEM_STATS                   1
#define SYS_STATS                   1
#define MEMP_STATS                  1
#define LINK_STATS                  1
#define MEM_OVERFLOW_CHECK          1
#else
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0
#define MEM_OVERFLOW_CHECK          0
#endif

#define LWIP_MEM_ILLEGAL_FREE fail

#define PICO_PRINTF_ALWAYS_INCLUDED 0

#define LWIP_MBEDTLSDIR PICO_MBEDTLS_PATH
