/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/tcp.c
DESCRIPTION: TCP client mínimo: 3-way handshake, RX com ACK, FIN/FIN-ACK
VERSÃO: 1.0 (LBF-OS Kernel v2.9 / Runtime v6.3)
============================================================================ */
#include "tcp.h"
#include "ip.h"
#include "net_poll.h"
#include "net_utils.h"
#include "../system/liblib.h"

#define MAX_TCP_SOCKETS   4
#define TCP_RX_BUF_SIZE   4096
#define TCP_WINDOW_SIZE   4096
#define TCP_MSS           1400

typedef struct {
    bool     in_use;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint8_t  state;
    uint32_t snd_seq;   // próximo SEQ que vamos enviar
    uint32_t rcv_seq;   // próximo SEQ que esperamos do servidor
    uint8_t  rx_buf[TCP_RX_BUF_SIZE];
    uint16_t rx_len;    // bytes aguardando leitura do app
    bool     fin_seen;
} tcp_socket_t;

static tcp_socket_t g_tcp_socks[MAX_TCP_SOCKETS];
static uint16_t g_next_port = 49152;

static inline void tcp_memset(void* d, int v, unsigned long n) {
    uint8_t* p = (uint8_t*)d;
    while (n--) *p++ = (uint8_t)v;
}
static inline void tcp_memcpy(void* d, const void* s, unsigned long n) {
    uint8_t* pd = (uint8_t*)d; const uint8_t* ps = (const uint8_t*)s;
    while (n--) *pd++ = *ps++;
}

// ============================================================================
// CHECKSUM TCP (pseudo-header + segmento), mesmo estilo do ip_calculate_checksum
// ============================================================================
static uint16_t tcp_checksum(const uint8_t* seg, uint16_t seg_len,
                             uint32_t src_ip, uint32_t dst_ip) {
    uint8_t buf[1520];
    uint16_t idx = 0;
    // Pseudo-header (bytes na ordem do cabo)
    buf[idx++] = src_ip & 0xFF; buf[idx++] = (src_ip >> 8) & 0xFF;
    buf[idx++] = (src_ip >> 16) & 0xFF; buf[idx++] = (src_ip >> 24) & 0xFF;
    buf[idx++] = dst_ip & 0xFF; buf[idx++] = (dst_ip >> 8) & 0xFF;
    buf[idx++] = (dst_ip >> 16) & 0xFF; buf[idx++] = (dst_ip >> 24) & 0xFF;
    buf[idx++] = 0; buf[idx++] = 6;                      // zero + protocolo TCP
    buf[idx++] = (seg_len >> 8) & 0xFF; buf[idx++] = seg_len & 0xFF; // comprimento BE
    tcp_memcpy(buf + idx, seg, seg_len);
    return ip_calculate_checksum(buf, idx + seg_len);
}

// ============================================================================
// ENVIO DE SEGMENTO
// ============================================================================
static void tcp_send_segment(tcp_socket_t* s, uint8_t flags,
                             const void* data, uint16_t len) {
    uint8_t seg[1500];
    tcp_header_t* tcp = (tcp_header_t*)seg;

    tcp->src_port = htons(s->local_port);
    tcp->dst_port = htons(s->remote_port);
    tcp->seq = htonl(s->snd_seq);
    tcp->ack = htonl(s->rcv_seq);
    tcp->data_offset = (5 << 4);   // 20 bytes, sem opções
    tcp->flags = flags;
    tcp->window = htons(TCP_WINDOW_SIZE);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (data && len) tcp_memcpy(seg + sizeof(tcp_header_t), data, len);

    uint16_t seg_len = sizeof(tcp_header_t) + len;
    tcp->checksum = tcp_checksum(seg, seg_len, ip_get_my_ip(), s->remote_ip);

    ip_send_packet(s->remote_ip, IP_PROTO_TCP, seg, seg_len);

    // SYN e FIN consomem 1 número de sequência
    s->snd_seq += len + ((flags & (TCP_SYN | TCP_FIN)) ? 1 : 0);
}

// ============================================================================
// CONNECT (3-way handshake)
// ============================================================================
int tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    int fd = -1;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (!g_tcp_socks[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;

    tcp_socket_t* s = &g_tcp_socks[fd];
    tcp_memset(s, 0, sizeof(tcp_socket_t));
    s->in_use = true;
    s->local_port = g_next_port++;
    s->remote_ip = remote_ip;
    s->remote_port = remote_port;
    s->state = TCP_STATE_SYN_SENT;
    s->snd_seq = 1000;      // ISN simples
    s->rcv_seq = 0;

    tcp_send_segment(s, TCP_SYN, NULL, 0);   // 1) SYN

    // Aguarda SYN-ACK (~2s)
    for (int i = 0; i < 200; i++) {
        net_poll();
        if (s->state == TCP_STATE_ESTABLISHED) return fd;
        if (s->state == TCP_STATE_CLOSED) return -1;
        sys_sleep(10);
    }
    s->in_use = false;
    return -1; // timeout
}

// ============================================================================
// SEND (dados com PSH+ACK, fragmentado em MSS)
// ============================================================================
int tcp_send(int fd, const void* data, uint16_t len) {
    if (fd < 0 || fd >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t* s = &g_tcp_socks[fd];
    if (!s->in_use || s->state != TCP_STATE_ESTABLISHED) return -1;

    uint16_t off = 0;
    while (off < len) {
        uint16_t chunk = (len - off) > TCP_MSS ? TCP_MSS : (len - off);
        tcp_send_segment(s, TCP_PSH | TCP_ACK, (const uint8_t*)data + off, chunk);
        off += chunk;
        // respira para o NAT/servidor ACKar
        for (int i = 0; i < 20; i++) { net_poll(); sys_sleep(5); }
    }
    return (int)len;
}

// ============================================================================
// RECV (lê do rx_buf, aguardando com net_poll)
// ============================================================================
int tcp_recv(int fd, void* buf, uint16_t max_len, int timeout_ms) {
    if (fd < 0 || fd >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t* s = &g_tcp_socks[fd];
    if (!s->in_use) return -1;

    int attempts = timeout_ms / 10;
    while (s->rx_len == 0 && !s->fin_seen && attempts-- > 0) {
        net_poll();
        sys_sleep(10);
    }
    if (s->rx_len == 0) return s->fin_seen ? 0 : -1;

    uint16_t n = s->rx_len < max_len ? s->rx_len : max_len;
    tcp_memcpy(buf, s->rx_buf, n);
    for (uint16_t i = 0; i < s->rx_len - n; i++) s->rx_buf[i] = s->rx_buf[i + n];
    s->rx_len -= n;
    return (int)n;
}

// ============================================================================
// CLOSE (FIN + espera breve)
// ============================================================================
void tcp_close(int fd) {
    if (fd < 0 || fd >= MAX_TCP_SOCKETS) return;
    tcp_socket_t* s = &g_tcp_socks[fd];
    if (!s->in_use) return;

    if (s->state == TCP_STATE_ESTABLISHED || s->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(s, TCP_FIN | TCP_ACK, NULL, 0);
        s->state = TCP_STATE_LAST_ACK;
        for (int i = 0; i < 50; i++) {
            net_poll();
            if (!s->in_use) return;
            sys_sleep(10);
        }
    }
    s->in_use = false;
    s->state = TCP_STATE_CLOSED;
}

// ============================================================================
// PROCESSAMENTO DE SEGMENTOS RECEBIDOS (chamado pelo ip_process_packet)
// ============================================================================
void tcp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip) {
    if (len < sizeof(tcp_header_t)) return;
    const tcp_header_t* tcp = (const tcp_header_t*)buffer;

    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);

    tcp_socket_t* s = NULL;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (g_tcp_socks[i].in_use &&
            g_tcp_socks[i].local_port == dst_port &&
            g_tcp_socks[i].remote_ip == src_ip &&
            g_tcp_socks[i].remote_port == src_port) {
            s = &g_tcp_socks[i];
            break;
        }
    }
    if (!s) return;

    if (tcp->flags & TCP_RST) {
        s->state = TCP_STATE_CLOSED;
        s->in_use = false;
        return;
    }

    uint32_t seg_seq = ntohl(tcp->seq);
    uint16_t hdr_len = (tcp->data_offset >> 4) * 4;
    if (len < hdr_len) return;
    uint16_t data_len = len - hdr_len;
    const uint8_t* data = buffer + hdr_len;

    switch (s->state) {
    case TCP_STATE_SYN_SENT:
        // 2) SYN-ACK do servidor
        if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            s->rcv_seq = seg_seq + 1;
            s->state = TCP_STATE_ESTABLISHED;
            tcp_send_segment(s, TCP_ACK, NULL, 0);   // 3) ACK
        }
        break;

    case TCP_STATE_ESTABLISHED:
    case TCP_STATE_CLOSE_WAIT:
        if (data_len > 0) {
            if (seg_seq == s->rcv_seq) {
                uint16_t space = TCP_RX_BUF_SIZE - s->rx_len;
                uint16_t copy = data_len < space ? data_len : space;
                tcp_memcpy(s->rx_buf + s->rx_len, data, copy);
                s->rx_len += copy;
                s->rcv_seq += data_len;
            }
            tcp_send_segment(s, TCP_ACK, NULL, 0);  // ACK (ou re-ACK)
        }
        if (tcp->flags & TCP_FIN) {
            s->rcv_seq = seg_seq + data_len + 1;
            s->fin_seen = true;
            s->state = TCP_STATE_CLOSE_WAIT;
            tcp_send_segment(s, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_STATE_LAST_ACK:
        if (tcp->flags & TCP_ACK) {
            s->state = TCP_STATE_CLOSED;
            s->in_use = false;
        }
        break;

    default:
        break;
    }
}
