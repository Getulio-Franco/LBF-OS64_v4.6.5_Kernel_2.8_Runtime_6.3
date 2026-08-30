/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/icmp.c
 * DESCRIPTION: Resposta automática a Ping (Echo Reply) e emissão de Echo Request
 * ============================================================================ */

#include "icmp.h"
#include "ip.h"
#include "net_utils.h"

// Função utilitária rápida de cópia
static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq, const void* data, uint16_t data_len) {
    uint16_t total_len = sizeof(icmp_header_t) + data_len;
    
    if (total_len > 1000) return -1; // Limite de segurança da Stack

    uint8_t packet[1000];
    icmp_header_t* icmp = (icmp_header_t*)packet;

    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(id);
    icmp->sequence = htons(seq);

    if (data && data_len > 0) {
        local_memcpy(packet + sizeof(icmp_header_t), data, data_len);
    }

    // ICMP exige checksum sobre TODO o pacote (Header + Dados)
    icmp->checksum = ip_calculate_checksum(packet, total_len);

    return ip_send_packet(dest_ip, IP_PROTO_ICMP, packet, total_len);
}

void icmp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip) {
    if (!buffer || len < sizeof(icmp_header_t) || len > 1000) return;

    if (ip_calculate_checksum(buffer, len) != 0) return;

    const icmp_header_t* icmp = (const icmp_header_t*)buffer;

    // Se alguém na rede pingar o nosso Kernel, respondemos automaticamente!
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        uint8_t reply_buf[1000];
        icmp_header_t* reply_icmp = (icmp_header_t*)reply_buf;

        reply_icmp->type = ICMP_TYPE_ECHO_REPLY;
        reply_icmp->code = 0;
        reply_icmp->checksum = 0;
        reply_icmp->id = icmp->id;
        reply_icmp->sequence = icmp->sequence;

        uint16_t payload_len = len - sizeof(icmp_header_t);
        if (payload_len > 0) {
            local_memcpy(reply_buf + sizeof(icmp_header_t), buffer + sizeof(icmp_header_t), payload_len);
        }

        reply_icmp->checksum = ip_calculate_checksum(reply_buf, len);

        ip_send_packet(src_ip, IP_PROTO_ICMP, reply_buf, len);
    }
}
