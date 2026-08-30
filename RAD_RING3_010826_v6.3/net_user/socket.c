/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/socket.c
DESCRIPTION: Gerenciamento da tabela de sockets e integração TCP/UDP
VERSÃO: 2.0 (LBF-OS Kernel v2.9 / Runtime v6.3)

CORREÇÃO v2.0:
    - REMOVIDO uso de tcp_conn_t* / tcp_send_data / tcp_connect(3 args),
      que não existiam no tcp.h.
    - AGORA usa a API fd-based do tcp.c:
        tcp_connect(ip, port) -> int fd (faz o handshake internamente)
        tcp_send(fd, buf, len)
        tcp_recv(fd, buf, len, timeout_ms)
        tcp_close(fd)
    - Cada socket entry guarda o tcp_fd correspondente.
============================================================================ */
#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "net_poll.h"
#include "net_interface.h"
#include "net_utils.h"

#define MAX_SOCKETS   16
#define RX_BUF_SIZE   2048

typedef enum {
    SOCKET_STATE_FREE,
    SOCKET_STATE_CREATED,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_CLOSED
} socket_state_t;

typedef struct {
    int            domain;
    int            type;
    int            protocol;
    socket_state_t state;
    uint32_t       local_ip;
    uint16_t       local_port;
    uint32_t       remote_ip;
    uint16_t       remote_port;

    // v2.0: fd da camada TCP (fd-based). -1 = sem conexão TCP.
    int            tcp_fd;

    // Buffer circular de recepção para UDP
    uint8_t        rx_buffer[RX_BUF_SIZE];
    uint16_t       rx_head;
    uint16_t       rx_tail;
} socket_entry_t;

static socket_entry_t g_sockets[MAX_SOCKETS];
static uint16_t       g_ephemeral_port = 49152;

static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================
void socket_init(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        g_sockets[i].state   = SOCKET_STATE_FREE;
        g_sockets[i].tcp_fd  = -1;
        g_sockets[i].rx_head = 0;
        g_sockets[i].rx_tail = 0;
    }
    g_ephemeral_port = 49152;
}

// ============================================================================
// SOCKET
// ============================================================================
int socket(int domain, int type, int protocol) {
    (void)protocol;
    if (domain != AF_INET) return -1;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].state == SOCKET_STATE_FREE) {
            g_sockets[i].domain      = domain;
            g_sockets[i].type        = type;
            g_sockets[i].protocol    = (type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
            g_sockets[i].state       = SOCKET_STATE_CREATED;
            g_sockets[i].local_ip    = 0;
            g_sockets[i].local_port  = 0;
            g_sockets[i].remote_ip   = 0;
            g_sockets[i].remote_port = 0;
            g_sockets[i].tcp_fd      = -1;
            g_sockets[i].rx_head     = 0;
            g_sockets[i].rx_tail     = 0;
            return i;
        }
    }
    return -1;
}

// ============================================================================
// BIND
// ============================================================================
int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    (void)addrlen;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !addr) return -1;
    if (g_sockets[sockfd].state != SOCKET_STATE_CREATED) return -1;

    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    g_sockets[sockfd].local_ip   = in_addr->sin_addr.s_addr;
    g_sockets[sockfd].local_port = ntohs(in_addr->sin_port);
    g_sockets[sockfd].state      = SOCKET_STATE_BOUND;
    return 0;
}

// ============================================================================
// LISTEN / ACCEPT (não implementados — modo cliente apenas)
// ============================================================================
int listen(int sockfd, int backlog) {
    (void)sockfd; (void)backlog;
    return -1;
}

int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

// ============================================================================
// CONNECT
// ============================================================================
int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    (void)addrlen;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !addr) return -1;

    socket_entry_t *s = &g_sockets[sockfd];
    if (s->state != SOCKET_STATE_CREATED && s->state != SOCKET_STATE_BOUND) return -1;

    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    s->remote_ip   = in_addr->sin_addr.s_addr;
    s->remote_port = ntohs(in_addr->sin_port);

    if (s->local_port == 0) {
        s->local_port = g_ephemeral_port++;
        if (g_ephemeral_port > 65530) g_ephemeral_port = 49152;
    }

    if (s->type == SOCK_STREAM) {
        // v2.0: tcp_connect já faz o 3-way handshake internamente (SYN->SYN-ACK->ACK)
        s->tcp_fd = tcp_connect(s->remote_ip, s->remote_port);
        if (s->tcp_fd < 0) {
            s->tcp_fd = -1;
            return -1;
        }
        s->state = SOCKET_STATE_CONNECTED;
        return 0;

    } else if (s->type == SOCK_DGRAM) {
        s->state = SOCKET_STATE_CONNECTED;
        return 0;
    }

    return -1;
}

// ============================================================================
// SEND
// ============================================================================
int send(int sockfd, const void *buf, size_t len, int flags) {
    (void)flags;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !buf || len == 0) return -1;

    socket_entry_t *s = &g_sockets[sockfd];
    if (s->state != SOCKET_STATE_CONNECTED) return -1;

    if (s->type == SOCK_STREAM && s->tcp_fd >= 0) {
        return tcp_send(s->tcp_fd, buf, (uint16_t)len);
    } else if (s->type == SOCK_DGRAM) {
        return udp_send_packet(s->remote_ip, s->local_port, s->remote_port,
                               buf, (uint16_t)len);
    }
    return -1;
}

// ============================================================================
// RECV
// ============================================================================
int recv(int sockfd, void *buf, size_t len, int flags) {
    (void)flags;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !buf || len == 0) return -1;

    socket_entry_t *s = &g_sockets[sockfd];

    if (s->type == SOCK_DGRAM) {
        // UDP: permite CONNECTED ou BOUND (escuta DHCP/DNS)
        if (s->state != SOCKET_STATE_CONNECTED && s->state != SOCKET_STATE_BOUND) return -1;

        net_poll();

        uint16_t available = (s->rx_tail >= s->rx_head)
                           ? (uint16_t)(s->rx_tail - s->rx_head)
                           : (uint16_t)(RX_BUF_SIZE - s->rx_head + s->rx_tail);
        if (available == 0) return 0;

        uint16_t read_bytes = ((uint16_t)len < available) ? (uint16_t)len : available;
        uint8_t *dest = (uint8_t *)buf;
        for (uint16_t i = 0; i < read_bytes; i++) {
            dest[i] = s->rx_buffer[s->rx_head];
            s->rx_head = (s->rx_head + 1) % RX_BUF_SIZE;
        }
        return read_bytes;

    } else {
        // TCP: delega ao tcp_recv (aguarda até 20ms por dados)
        if (s->state != SOCKET_STATE_CONNECTED || s->tcp_fd < 0) return -1;
        return tcp_recv(s->tcp_fd, buf, (uint16_t)len, 20);
    }
}

// ============================================================================
// CLOSE
// ============================================================================
int close(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS) return -1;

    socket_entry_t *s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_fd >= 0) {
        tcp_close(s->tcp_fd);
        s->tcp_fd = -1;
    }
    s->state = SOCKET_STATE_FREE;
    return 0;
}

// ============================================================================
// ENTREGA DE PACOTES UDP (chamado pelo udp_process_packet)
// ============================================================================
void socket_deliver_udp(uint32_t src_ip, uint16_t src_port, uint32_t dest_ip,
                        uint16_t dest_port, const uint8_t *data, uint16_t len) {
    (void)src_ip; (void)dest_ip; (void)src_port;
    if (!data || len == 0) return;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_entry_t *s = &g_sockets[i];
        // Aceita CONNECTED ou BOUND (portas locais como a do DHCP 68)
        if ((s->state == SOCKET_STATE_CONNECTED || s->state == SOCKET_STATE_BOUND) &&
            s->type == SOCK_DGRAM && s->local_port == dest_port) {
            for (uint16_t j = 0; j < len; j++) {
                uint16_t next_tail = (s->rx_tail + 1) % RX_BUF_SIZE;
                if (next_tail == s->rx_head) break; // Buffer cheio
                s->rx_buffer[s->rx_tail] = data[j];
                s->rx_tail = next_tail;
            }
            break;
        }
    }
}
