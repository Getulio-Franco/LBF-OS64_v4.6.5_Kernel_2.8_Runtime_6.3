/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/tcp.h
DESCRIPTION: TCP client mínimo (handshake, dados, FIN) para HTTP
VERSÃO: 1.0 (LBF-OS Kernel v2.9 / Runtime v6.3)
============================================================================ */
#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stdbool.h>

// Flags TCP
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

// Estados da conexão
#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_CLOSE_WAIT  3
#define TCP_STATE_LAST_ACK    4

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; // (offset << 4)
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_header_t;

// API interna usada pelo socket.c
int  tcp_connect(uint32_t remote_ip, uint16_t remote_port);
int  tcp_send(int fd, const void* data, uint16_t len);
int  tcp_recv(int fd, void* buf, uint16_t max_len, int timeout_ms);
void tcp_close(int fd);
void tcp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip);

#endif // TCP_H
