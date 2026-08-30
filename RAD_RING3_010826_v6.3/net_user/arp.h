/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/arp.h
DESCRIPTION: Estruturas do protocolo ARP e Tabela de Tradução IP -> MAC
CORREÇÃO v2.9: MAKE_IP ajustado para Network Byte Order (Big Endian)
============================================================================ */
#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <stdbool.h>

// Constantes do Protocolo ARP
#define ARP_HTYPE_ETHERNET 0x0001
#define ARP_PTYPE_IPV4     0x0800
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2

// Tamanho máximo do cache ARP
#define ARP_TABLE_SIZE     16

// ============================================================================
// CORREÇÃO CRÍTICA: Network Byte Order (Big Endian)
// Antes: (a) | (b<<8) | (c<<16) | (d<<24) -> Gerava 15.2.0.10 no cabo
// Agora: (a<<24) | (b<<16) | (c<<8) | (d) -> Gera 10.0.2.15 no cabo
// ============================================================================

// CORRETO para x86: os bytes a.b.c.d vão na ordem certa para o cabo
#define MAKE_IP(a,b,c,d) ((uint32_t)(((uint8_t)(a)) | ((uint8_t)(b) << 8) | ((uint8_t)(c) << 16) | ((uint8_t)(d) << 24)))

// ============================================================================
// ESTRUTURA DO CABEÇALHO ARP (28 Bytes)
// ============================================================================
typedef struct {
    uint16_t htype;         // Hardware Type (Ethernet = 1)
    uint16_t ptype;         // Protocol Type (IPv4 = 0x0800)
    uint8_t  hlen;          // Hardware Address Length (6)
    uint8_t  plen;          // Protocol Address Length (4)
    uint16_t opcode;        // Operação (1 = Request, 2 = Reply)
    uint8_t  sender_mac[6]; // Endereço MAC do Remetente
    uint32_t sender_ip;     // Endereço IP do Remetente (Network Byte Order)
    uint8_t  target_mac[6]; // Endereço MAC do Destinatário (0x00 no Request)
    uint32_t target_ip;     // Endereço IP do Destinatário (Network Byte Order)
} __attribute__((packed)) arp_header_t;

// ============================================================================
// ESTRUTURA DA TABELA DE CACHE ARP
// ============================================================================
typedef struct {
    uint32_t ip;            // Endereço IP (Chave de busca)
    uint8_t  mac[6];        // Endereço MAC físico (Valor)
    bool     valid;         // Flag que indica se o slot está em uso
} arp_entry_t;

// ============================================================================
// PROTÓTIPOS
// ============================================================================
void     arp_init(uint32_t my_ip);
void     arp_set_ip(uint32_t my_ip);
uint32_t arp_get_ip(void);
void     arp_process_packet(const uint8_t* buffer, uint16_t len);
bool     arp_lookup(uint32_t ip, uint8_t out_mac[6]);
int      arp_send_request(uint32_t target_ip);

#endif // ARP_H
