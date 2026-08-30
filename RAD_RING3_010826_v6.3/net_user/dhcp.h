/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/dhcp.h
 * DESCRIPTION: Cliente DHCP (DORA) para atribuição dinâmica de IP e parâmetros
 * ============================================================================ */

#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER      3
#define DHCP_OPTION_DNS         6
#define DHCP_OPTION_MSG_TYPE    53
#define DHCP_OPTION_END         255

// Estrutura do Pacote DHCP / BOOTP (236 bytes de cabeçalho + Magic Cookie)
typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    char     sname[64];
    char     file[128];
    uint32_t magic_cookie;
} __attribute__((packed)) dhcp_packet_t;

// Configuração de rede obtida do servidor DHCP
typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
} dhcp_config_t;

int dhcp_request_ip(const uint8_t* mac_addr, dhcp_config_t* config);

#endif // DHCP_H
