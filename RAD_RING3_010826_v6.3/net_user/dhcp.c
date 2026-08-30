/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/dhcp.c
 * DESCRIPTION: Sequência DORA (Discover, Offer, Request, ACK) do cliente DHCP
 * ============================================================================ */

#include "dhcp.h"
#include "socket.h"
#include "net_poll.h"
#include "net_utils.h"
#include "../system/liblib.h"

static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

static inline void local_memset(void* dest, int val, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    while (n--) *d++ = (uint8_t)val;
}

static void fill_dhcp_header(dhcp_packet_t* pkt, uint32_t xid, const uint8_t* mac_addr) {
    local_memset(pkt, 0, sizeof(dhcp_packet_t));
    pkt->op           = DHCP_OP_BOOTREQUEST;
    pkt->htype        = 1; // Ethernet
    pkt->hlen         = 6; // MAC 6 bytes
    pkt->hops         = 0;
    pkt->xid          = xid;
    pkt->secs         = 0;
    pkt->flags        = htons(0x8000); // Flag de Broadcast
    pkt->magic_cookie = htonl(0x63825363);

    if (mac_addr) {
        local_memcpy(pkt->chaddr, mac_addr, 6);
    }
}

int dhcp_request_ip(const uint8_t* mac_addr, dhcp_config_t* config) {
    if (!mac_addr || !config) return -1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) return -1;

    struct sockaddr_in bind_addr;
    local_memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(DHCP_CLIENT_PORT);
    bind_addr.sin_addr.s_addr = 0; // 0.0.0.0

    if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    struct sockaddr_in dest_addr;
    local_memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(DHCP_SERVER_PORT);
    dest_addr.sin_addr.s_addr = 0xFFFFFFFF; // 255.255.255.255 (Broadcast)

    if (connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    uint32_t transaction_id = 0x39A3C123;
    uint8_t pkt_buffer[576];
    dhcp_packet_t* dhcp_pkt = (dhcp_packet_t*)pkt_buffer;

    // --- 1. DHCP DISCOVER ---
    fill_dhcp_header(dhcp_pkt, transaction_id, mac_addr);
    uint8_t* opt_ptr = pkt_buffer + sizeof(dhcp_packet_t);

    *opt_ptr++ = DHCP_OPTION_MSG_TYPE;
    *opt_ptr++ = 1;
    *opt_ptr++ = DHCP_DISCOVER;
    *opt_ptr++ = DHCP_OPTION_END;

    int pkt_len = opt_ptr - pkt_buffer;
    if (send(sockfd, pkt_buffer, pkt_len, 0) < 0) {
        close(sockfd);
        return -1;
    }

    // --- 2. RECEBE DHCP OFFER ---
    uint8_t rx_buf[576];
    uint32_t offered_ip = 0;
    int attempts = 150;

    while (attempts-- > 0) {
        net_poll();
        int rx_len = recv(sockfd, rx_buf, sizeof(rx_buf), 0);
        if (rx_len >= (int)sizeof(dhcp_packet_t)) {
            dhcp_packet_t* rx_dhcp = (dhcp_packet_t*)rx_buf;
            if (rx_dhcp->xid == transaction_id && rx_dhcp->op == DHCP_OP_BOOTREPLY) {
                if (rx_dhcp->magic_cookie == htonl(0x63825363)) {
                    offered_ip = rx_dhcp->yiaddr;
                    break;
                }
            }
        }
        sys_sleep(10);
    }

    if (offered_ip == 0) {
        close(sockfd);
        return -1; // Timeout aguardando OFFER
    }

    // --- 3. DHCP REQUEST ---
    fill_dhcp_header(dhcp_pkt, transaction_id, mac_addr);
    opt_ptr = pkt_buffer + sizeof(dhcp_packet_t);

    *opt_ptr++ = DHCP_OPTION_MSG_TYPE;
    *opt_ptr++ = 1;
    *opt_ptr++ = DHCP_REQUEST;

    // Solicita o IP recebido na fase de Offer
    *opt_ptr++ = 50; // Requested IP Option
    *opt_ptr++ = 4;
    local_memcpy(opt_ptr, &offered_ip, 4);
    opt_ptr += 4;

    *opt_ptr++ = DHCP_OPTION_END;
    pkt_len = opt_ptr - pkt_buffer;

    if (send(sockfd, pkt_buffer, pkt_len, 0) < 0) {
        close(sockfd);
        return -1;
    }

    // --- 4. RECEBE DHCP ACK ---
    attempts = 150;
    bool ack_received = false;

    while (attempts-- > 0) {
        net_poll();
        int rx_len = recv(sockfd, rx_buf, sizeof(rx_buf), 0);
        if (rx_len >= (int)sizeof(dhcp_packet_t)) {
            dhcp_packet_t* rx_dhcp = (dhcp_packet_t*)rx_buf;
            if (rx_dhcp->xid == transaction_id && rx_dhcp->op == DHCP_OP_BOOTREPLY) {
                if (rx_dhcp->magic_cookie == htonl(0x63825363)) {
                    config->ip = rx_dhcp->yiaddr;

                    // Extrai Opções do ACK (Máscara, Gateway, DNS)
                    uint8_t* parse_opts = rx_buf + sizeof(dhcp_packet_t);
                    int opts_len = rx_len - sizeof(dhcp_packet_t);
                    int idx = 0;

                    while (idx < opts_len && parse_opts[idx] != DHCP_OPTION_END) {
                        uint8_t opt = parse_opts[idx];
                        if (opt == 0) { idx++; continue; }
                        uint8_t len = parse_opts[idx + 1];

                        if (idx + 2 + len > opts_len) break;

                        if (opt == DHCP_OPTION_SUBNET_MASK && len == 4) {
                            local_memcpy(&config->netmask, &parse_opts[idx + 2], 4);
                        } else if (opt == DHCP_OPTION_ROUTER && len >= 4) {
                            local_memcpy(&config->gateway, &parse_opts[idx + 2], 4);
                        } else if (opt == DHCP_OPTION_DNS && len >= 4) {
                            local_memcpy(&config->dns_server, &parse_opts[idx + 2], 4);
                        }

                        idx += 2 + len;
                    }

                    ack_received = true;
                    break;
                }
            }
        }
        sys_sleep(10);
    }

    close(sockfd);
    return ack_received ? 0 : -1;
}

/*int dhcp_request_ip(const uint8_t* mac_addr, dhcp_config_t* config) {
    if (!mac_addr || !config) return -1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) return -1;

    struct sockaddr_in bind_addr;
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(DHCP_CLIENT_PORT);
    bind_addr.sin_addr.s_addr = 0; // Escuta em 0.0.0.0

    if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(DHCP_SERVER_PORT);
    dest_addr.sin_addr.s_addr = 0xFFFFFFFF; // Broadcast Global 255.255.255.255

    if (connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    uint32_t transaction_id = 0x39A3C123;
    uint8_t pkt_buffer[512];
    dhcp_packet_t* dhcp_pkt = (dhcp_packet_t*)pkt_buffer;

    // --- 1. DHCP DISCOVER ---
    fill_dhcp_header(dhcp_pkt, transaction_id, mac_addr);
    uint8_t* opt_ptr = pkt_buffer + sizeof(dhcp_packet_t);

    *opt_ptr++ = DHCP_OPTION_MSG_TYPE;
    *opt_ptr++ = 1;
    *opt_ptr++ = DHCP_DISCOVER;
    *opt_ptr++ = DHCP_OPTION_END;

    int pkt_len = opt_ptr - pkt_buffer;
    if (send(sockfd, pkt_buffer, pkt_len, 0) < 0) {
        close(sockfd);
        return -1;
    }

    // --- 2. RECEBE DHCP OFFER ---
    uint8_t rx_buf[512];
    uint32_t offered_ip = 0;
    int attempts = 100;

    while (attempts-- > 0) {
        net_poll();
        int rx_len = recv(sockfd, rx_buf, sizeof(rx_buf), 0);
        if (rx_len >= (int)sizeof(dhcp_packet_t)) {
            dhcp_packet_t* rx_dhcp = (dhcp_packet_t*)rx_buf;
            if (rx_dhcp->xid == transaction_id && rx_dhcp->op == DHCP_OP_BOOTREPLY) {
                offered_ip = rx_dhcp->yiaddr;
                break;
            }
        }
        sys_sleep(10);
    }

    if (offered_ip == 0) {
        close(sockfd);
        return -1; // Timeout aguardando OFFER
    }

    // --- 3. DHCP REQUEST ---
    fill_dhcp_header(dhcp_pkt, transaction_id, mac_addr);
    opt_ptr = pkt_buffer + sizeof(dhcp_packet_t);

    *opt_ptr++ = DHCP_OPTION_MSG_TYPE;
    *opt_ptr++ = 1;
    *opt_ptr++ = DHCP_REQUEST;

    // Solicita o IP recebido na fase de Offer
    *opt_ptr++ = 50; // Requested IP Option
    *opt_ptr++ = 4;
    local_memcpy(opt_ptr, &offered_ip, 4);
    opt_ptr += 4;

    *opt_ptr++ = DHCP_OPTION_END;
    pkt_len = opt_ptr - pkt_buffer;

    if (send(sockfd, pkt_buffer, pkt_len, 0) < 0) {
        close(sockfd);
        return -1;
    }

    // --- 4. RECEBE DHCP ACK ---
    attempts = 100;
    bool ack_received = false;

    while (attempts-- > 0) {
        net_poll();
        int rx_len = recv(sockfd, rx_buf, sizeof(rx_buf), 0);
        if (rx_len >= (int)sizeof(dhcp_packet_t)) {
            dhcp_packet_t* rx_dhcp = (dhcp_packet_t*)rx_buf;
            if (rx_dhcp->xid == transaction_id && rx_dhcp->op == DHCP_OP_BOOTREPLY) {
                config->ip = rx_dhcp->yiaddr;

                // Extrai Opções do ACK (Máscara, Gateway, DNS)
                uint8_t* parse_opts = rx_buf + sizeof(dhcp_packet_t);
                int opts_len = rx_len - sizeof(dhcp_packet_t);
                int idx = 0;

                while (idx < opts_len && parse_opts[idx] != DHCP_OPTION_END) {
                    uint8_t opt = parse_opts[idx];
                    if (opt == 0) { idx++; continue; }
                    uint8_t len = parse_opts[idx + 1];

                    if (opt == DHCP_OPTION_SUBNET_MASK && len == 4) {
                        local_memcpy(&config->netmask, &parse_opts[idx + 2], 4);
                    } else if (opt == DHCP_OPTION_ROUTER && len >= 4) {
                        local_memcpy(&config->gateway, &parse_opts[idx + 2], 4);
                    } else if (opt == DHCP_OPTION_DNS && len >= 4) {
                        local_memcpy(&config->dns_server, &parse_opts[idx + 2], 4);
                    }

                    idx += 2 + len;
                }

                ack_received = true;
                break;
            }
        }
        sys_sleep(10);
    }

    close(sockfd);
    return ack_received ? 0 : -1;
}*/
