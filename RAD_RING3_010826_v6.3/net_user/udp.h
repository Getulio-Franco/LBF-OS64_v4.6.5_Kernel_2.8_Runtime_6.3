/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/udp.h
 * DESCRIPTION: Estruturas do protocolo UDP (Camada de Transporte)
 * ============================================================================ */

#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stddef.h>

// Cabeçalho UDP (8 Bytes)
typedef struct {
    uint16_t src_port;  // Porta de Origem
    uint16_t dest_port; // Porta de Destino
    uint16_t length;    // Tamanho Total (Cabeçalho UDP + Payload)
    uint16_t checksum;  // Checksum (Opcional no IPv4, 0 = Ignorado)
} __attribute__((packed)) udp_header_t;

// ============================================================================
// PROTÓTIPOS
// ============================================================================

/**
 * udp_send_packet - Empacota e envia dados não-confiáveis para um destino
 */
int udp_send_packet(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void* payload, uint16_t payload_len);

/**
 * udp_process_packet - Recebe datagramas do IP e encaminha para o aplicativo (Sockets)
 */
void udp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip);

#endif // UDP_H
