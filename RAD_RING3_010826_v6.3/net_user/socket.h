/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/socket.h
DESCRIPTION: API de Sockets estilo BSD para aplicações de usuário
VERSÃO: 2.0 (LBF-OS Kernel v2.9 / Runtime v6.3)
============================================================================ */
#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    uint8_t        sin_zero[8];
};

// ============================================================================
// PROTÓTIPOS DA API BSD
// ============================================================================
void socket_init(void);
int  socket(int domain, int type, int protocol);
int  bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
int  connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
int  listen(int sockfd, int backlog);
int  accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen);
int  send(int sockfd, const void *buf, size_t len, int flags);
int  recv(int sockfd, void *buf, size_t len, int flags);
int  close(int sockfd);

// Integração interna com a camada UDP (chamado pelo udp_process_packet)
void socket_deliver_udp(uint32_t src_ip, uint16_t src_port, uint32_t dest_ip,
                        uint16_t dest_port, const uint8_t *data, uint16_t len);

#endif // SOCKET_H
