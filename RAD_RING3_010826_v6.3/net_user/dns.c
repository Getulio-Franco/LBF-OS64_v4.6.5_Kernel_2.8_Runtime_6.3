/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/dns.c
 * DESCRIPTION: Codificação de rótulos DNS, envio via UDP Socket e parsing de respostas
 * ============================================================================ */

#include "dns.h"
#include "socket.h"
#include "net_poll.h"
#include "net_utils.h"
#include "../system/liblib.h"

static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

// Converte "www.google.com" para a estrutura de rótulos DNS: "\x03www\x06google\x03com\x00"
static int encode_dns_name(const char* hostname, uint8_t* buffer) {
    int src_idx = 0;
    int buf_idx = 0;

    while (hostname[src_idx] != '\0') {
        int len_idx = buf_idx++;
        int count = 0;

        while (hostname[src_idx] != '.' && hostname[src_idx] != '\0') {
            buffer[buf_idx++] = (uint8_t)hostname[src_idx++];
            count++;
        }

        buffer[len_idx] = (uint8_t)count;

        if (hostname[src_idx] == '.') {
            src_idx++;
        }
    }

    buffer[buf_idx++] = 0; // Byte nulo delimitador de fim de nome
    return buf_idx;
}

uint32_t dns_resolve(const char* hostname, uint32_t dns_server_ip) {
    if (!hostname || dns_server_ip == 0) return 0;

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) return 0;

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);
    dest_addr.sin_addr.s_addr = dns_server_ip;

    if (connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        close(sockfd);
        return 0;
    }

    uint8_t packet[512];
    dns_header_t* dns = (dns_header_t*)packet;

    uint16_t transaction_id = 0x4242;
    dns->id         = htons(transaction_id);
    dns->flags      = htons(0x0100); // Consulta padrão com Recursão Desejada (RD = 1)
    dns->q_count    = htons(1);      // 1 Pergunta
    dns->ans_count  = 0;
    dns->auth_count = 0;
    dns->add_count  = 0;

    int offset = sizeof(dns_header_t);
    offset += encode_dns_name(hostname, packet + offset);

    // Anexa QTYPE (Type A = 1) e QCLASS (Class IN = 1)
    packet[offset++] = 0x00; packet[offset++] = 0x01; // Type A
    packet[offset++] = 0x00; packet[offset++] = 0x01; // Class IN

    if (send(sockfd, packet, offset, 0) < 0) {
        close(sockfd);
        return 0;
    }

    // Polling aguardando resposta do servidor DNS
    uint8_t rx_buf[512];
    int attempts = 100; // Timeout ~1 segundo
    int bytes_received = 0;

    while (attempts-- > 0) {
        net_poll();
        bytes_received = recv(sockfd, rx_buf, sizeof(rx_buf), 0);
        if (bytes_received > (int)sizeof(dns_header_t)) {
            break;
        }
        sys_sleep(10);
    }

    close(sockfd);

    if (bytes_received <= (int)sizeof(dns_header_t)) return 0;

    dns_header_t* resp_dns = (dns_header_t*)rx_buf;
    if (ntohs(resp_dns->id) != transaction_id) return 0;
    if (ntohs(resp_dns->ans_count) < 1) return 0;

    // Pula a seção de Pergunta (Question Section) na resposta
    int parse_ptr = sizeof(dns_header_t);

    while (parse_ptr < bytes_received && rx_buf[parse_ptr] != 0) {
        if ((rx_buf[parse_ptr] & 0xC0) == 0xC0) { // Trata ponteiro de compressão DNS
            parse_ptr += 2;
            break;
        }
        parse_ptr += rx_buf[parse_ptr] + 1;
    }
    if (rx_buf[parse_ptr] == 0) parse_ptr++;

    parse_ptr += 4; // Pula QTYPE e QCLASS da Pergunta

    // Analisa a seção de Respostas (Answer Section)
    uint16_t ans_count = ntohs(resp_dns->ans_count);
    for (uint16_t i = 0; i < ans_count; i++) {
        if (parse_ptr >= bytes_received) break;

        // Pula o campo NAME do registro de resposta
        if ((rx_buf[parse_ptr] & 0xC0) == 0xC0) {
            parse_ptr += 2;
        } else {
            while (parse_ptr < bytes_received && rx_buf[parse_ptr] != 0) {
                parse_ptr += rx_buf[parse_ptr] + 1;
            }
            parse_ptr++;
        }

        if (parse_ptr + 10 > bytes_received) break;

        uint16_t type     = (rx_buf[parse_ptr] << 8) | rx_buf[parse_ptr + 1];
        uint16_t data_len = (rx_buf[parse_ptr + 8] << 8) | rx_buf[parse_ptr + 9];
        parse_ptr += 10;

        // Se for um Registro Tipo A (IPv4 - 4 bytes)
        if (type == 1 && data_len == 4) {
            uint32_t resolved_ip = 0;
            local_memcpy(&resolved_ip, &rx_buf[parse_ptr], 4);
            return resolved_ip;
        }

        parse_ptr += data_len;
    }

    return 0;
}
