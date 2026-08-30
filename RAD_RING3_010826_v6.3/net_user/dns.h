/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/dns.h
 * DESCRIPTION: Cliente DNS para resolução de nomes de domínio (A Records) via UDP
 * ============================================================================ */

#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DNS_PORT 53

// Cabeçalho Padrão DNS (12 Bytes)
typedef struct {
    uint16_t id;          // Identificador da Transação
    uint16_t flags;       // Flags de Controle (QR, Opcode, AA, TC, RD, RA, rcode)
    uint16_t q_count;     // Quantidade de Perguntas (Questions)
    uint16_t ans_count;   // Quantidade de Respostas (Answers)
    uint16_t auth_count;  // Quantidade de Servidores de Autoridade
    uint16_t add_count;   // Quantidade de Registros Adicionais
} __attribute__((packed)) dns_header_t;

/**
 * dns_resolve - Transforma um nome de domínio em um endereço IPv4 de 32-bit
 * @hostname: Nome do domínio (ex: "google.com")
 * @dns_server_ip: IP do servidor DNS recursivo (ex: 8.8.8.8 em ordem de rede)
 * @return: IP resolvido em ordem de rede (Big-Endian) ou 0 em caso de falha/timeout
 */
uint32_t dns_resolve(const char* hostname, uint32_t dns_server_ip);

#endif // DNS_H
