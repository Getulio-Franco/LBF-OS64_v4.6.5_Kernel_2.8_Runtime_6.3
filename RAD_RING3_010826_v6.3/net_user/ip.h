/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/ip.h
 * DESCRIPTION: Estruturas do protocolo IPv4, configuração de rede e rotas
 * ============================================================================ */

#ifndef IP_H
#define IP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct {
    uint8_t  ihl_version;   // Versão (4 bits) + IHL Tamanho do Cabeçalho (4 bits)
    uint8_t  tos;           // Type of Service
    uint16_t total_len;     // Comprimento Total (Cabeçalho + Payload)
    uint16_t id;            // Identificação do Pacote
    uint16_t fragment;      // Flags (3 bits) + Fragment Offset (13 bits)
    uint8_t  ttl;           // Time to Live (Saltos máximos)
    uint8_t  protocol;      // Protocolo Superior (ICMP, TCP, UDP)
    uint16_t checksum;      // Checksum do Cabeçalho IP
    uint32_t src_ip;        // Endereço IP de Origem
    uint32_t dest_ip;       // Endereço IP de Destino
} __attribute__((packed)) ipv4_header_t;

// Funções de Inicialização e Reconfiguração Dinâmica (DHCP)
void     ip_init(uint32_t my_ip, uint32_t gateway_ip, uint32_t subnet_mask);
void     ip_set_config(uint32_t my_ip, uint32_t subnet_mask, uint32_t gateway_ip);

// Getters de Estado de Rede
uint32_t ip_get_my_ip(void);
uint32_t ip_get_gateway(void);
uint32_t ip_get_subnet_mask(void);

// Utilitários de Integridade, Roteamento e Transmissão
uint16_t ip_calculate_checksum(const void* vdata, size_t length);
int      ip_send_packet(uint32_t dest_ip, uint8_t protocol, const void* payload, uint16_t payload_len);
void     ip_process_packet(const uint8_t* buffer, uint16_t len);

#endif // IP_H
