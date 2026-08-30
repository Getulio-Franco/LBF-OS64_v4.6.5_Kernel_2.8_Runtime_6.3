/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/udp.c
 * DESCRIPTION: Empacotamento de datagramas UDP e multiplexação de portas
 * ============================================================================ */

#include "udp.h"
#include "ip.h"
#include "net_utils.h"
#include "socket.h"

// Utilitário interno rápido
static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

int udp_send_packet(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void* payload, uint16_t payload_len) {
    uint16_t total_len = sizeof(udp_header_t) + payload_len;
    
    if (total_len > 1472) return -1; // 1500 (MTU) - 20 (IP) - 8 (UDP)

    uint8_t packet[1500];
    udp_header_t* udp = (udp_header_t*)packet;

    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(total_len);
    udp->checksum = 0; // Opcional no IPv4, deixamos zerado para maior performance no SO

    // Anexa os dados do usuário logo após o cabeçalho de 8 bytes
    if (payload && payload_len > 0) {
        local_memcpy(packet + sizeof(udp_header_t), payload, payload_len);
    }

    // Pede para a Camada de Rede encapsular e enviar
    return ip_send_packet(dest_ip, IP_PROTO_UDP, packet, total_len);
}

void udp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip) {
    if (!buffer || len < sizeof(udp_header_t)) return;

    const udp_header_t* udp = (const udp_header_t*)buffer;
    uint16_t total_len = ntohs(udp->length);

    if (total_len < sizeof(udp_header_t) || len < total_len) return;

    uint16_t payload_len = total_len - sizeof(udp_header_t);
    const uint8_t* payload = buffer + sizeof(udp_header_t);
    
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t src_port = ntohs(udp->src_port);

    // Encaminha o datagrama UDP para a camada de Sockets
    socket_deliver_udp(src_ip, src_port, 0, dest_port, payload, payload_len);
}

/*void udp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip) {
    if (!buffer || len < sizeof(udp_header_t)) return;

    const udp_header_t* udp = (const udp_header_t*)buffer;
    uint16_t total_len = ntohs(udp->length);

    if (total_len < sizeof(udp_header_t) || len < total_len) return;

    uint16_t payload_len = total_len - sizeof(udp_header_t);
    const uint8_t* payload = buffer + sizeof(udp_header_t);
    
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t src_port = ntohs(udp->src_port);

    // TODO: Aqui sua futura camada "socket.c" vai buscar qual processo (App HTTP/DNS) 
    // está escutando na "dest_port" e jogar os dados do "payload" na fila dele!
}*/
