/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/ip.c
DESCRIPTION: Empacotamento de datagramas IPv4, rotas e verificação de integridade
CORREÇÃO v2.9: Roteamento forçado via Gateway para NAT, EtherType alinhado.
============================================================================ */
#include "ip.h"
#include "arp.h"
#include "net_interface.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "net_utils.h"

static uint32_t g_my_ip = 0;
static uint32_t g_gateway_ip = 0;
static uint32_t g_subnet_mask = 0;
static uint16_t g_ip_packet_id = 1;

static inline void local_memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

void ip_init(uint32_t my_ip, uint32_t gateway_ip, uint32_t subnet_mask) {
    ip_set_config(my_ip, subnet_mask, gateway_ip);
    g_ip_packet_id = 1;
}

void ip_set_config(uint32_t my_ip, uint32_t subnet_mask, uint32_t gateway_ip) {
    g_my_ip = my_ip;
    g_subnet_mask = subnet_mask;
    g_gateway_ip = gateway_ip;
    arp_set_ip(my_ip); 
}

uint32_t ip_get_my_ip(void) { return g_my_ip; }
uint32_t ip_get_gateway(void) { return g_gateway_ip; }
uint32_t ip_get_subnet_mask(void) { return g_subnet_mask; }

uint16_t ip_calculate_checksum(const void* vdata, size_t length) {
    const uint16_t* data = (const uint16_t*)vdata;
    uint32_t sum = 0;
    while (length > 1) {
        sum += *data++;
        length -= 2;
    }
    if (length > 0) {
        sum += *(const uint8_t*)data;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

int ip_send_packet(uint32_t dest_ip, uint8_t protocol, const void* payload, uint16_t payload_len) {
    if (!payload && payload_len > 0) return -1;
    
    uint16_t total_len = sizeof(ipv4_header_t) + payload_len;
    if (total_len > 1500) return -1; // Excede MTU Ethernet Padrão
    
    uint8_t packet[1500];
    ipv4_header_t* ip = (ipv4_header_t*)packet;
    
    ip->ihl_version = 0x45; // IPv4 (4) + Header Length 5 dwords (20 bytes)
    ip->tos = 0;
    ip->total_len = htons(total_len);
    ip->id = htons(g_ip_packet_id++);
    ip->fragment = htons(0x4000); // Flag Don't Fragment (DF)
    ip->ttl = 64;                 // Saltos padrão
    ip->protocol = protocol;
    ip->checksum = 0;             // Zera antes do cálculo
    ip->src_ip = g_my_ip;
    ip->dest_ip = dest_ip;
    
    // Calcula Checksum do Cabeçalho IP
    ip->checksum = ip_calculate_checksum(ip, sizeof(ipv4_header_t));
    
    // Anexa o Payload logo após o cabeçalho IP
    if (payload_len > 0) {
        local_memcpy(packet + sizeof(ipv4_header_t), payload, payload_len);
    }
    
    uint8_t dest_mac[6];
    
    // 1. Trata Broadcast Universal diretamente sem ARP
    if (dest_ip == 0xFFFFFFFF) {
        for (int i = 0; i < 6; i++) dest_mac[i] = 0xFF;
        // CORREÇÃO: Passa ETH_P_IP cru. O net_send_frame fará o htons().
        return net_send_frame(dest_mac, ETH_P_IP, packet, total_len);
    }
    
    // ========================================================================
    // 2. ROTEAMENTO NAT (CORREÇÃO CRÍTICA)
    // No VirtualBox NAT, o DNS (10.0.2.3) e a Internet não respondem ARP.
    // TODO tráfego que não for para o próprio IP deve ir para o Gateway.
    // ========================================================================
    uint32_t target_ip = dest_ip;
    if (dest_ip != g_my_ip) {
        target_ip = g_gateway_ip; // Força o uso do Gateway (10.0.2.2)
    }
    
    // 3. Resolução ARP do endereço MAC do próximo salto (Gateway)
    if (!arp_lookup(target_ip, dest_mac)) {
        arp_send_request(target_ip);
        return -2; // Aguarda o próximo poll para tentar novamente
    }
    
    // Encapsula e envia para a placa de rede
    // CORREÇÃO: Passa ETH_P_IP cru.
    return net_send_frame(dest_mac, ETH_P_IP, packet, total_len);
}

void ip_process_packet(const uint8_t* buffer, uint16_t len) {
    if (!buffer || len < sizeof(ipv4_header_t)) return;
    
    const ipv4_header_t* ip = (const ipv4_header_t*)buffer;
    
    // Valida versão IPv4 (primeiros 4 bits)
    if ((ip->ihl_version >> 4) != 4) return;
    
    uint8_t header_len = (ip->ihl_version & 0x0F) * 4;
    if (header_len < sizeof(ipv4_header_t) || len < header_len) return;
    
    // Valida Checksum do cabeçalho IP
    if (ip_calculate_checksum(ip, header_len) != 0) return;
    
    // Filtro de Destino: aceita unicast para nós, broadcast universal, subnet broadcast ou escuta inicial sem IP (DHCP)
    bool is_for_us = (ip->dest_ip == g_my_ip) ||
                     (ip->dest_ip == 0xFFFFFFFF) ||
                     (g_my_ip == 0) ||
                     (g_subnet_mask != 0 && ip->dest_ip == (g_my_ip | ~g_subnet_mask));
                     
    if (!is_for_us) return;
    
    uint16_t total_len = ntohs(ip->total_len);
    if (total_len < header_len || len < total_len) return;
    
    uint16_t payload_len = total_len - header_len;
    const uint8_t* payload = buffer + header_len;
    
    // Despacha para o protocolo de camada superior
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            icmp_process_packet(payload, payload_len, ip->src_ip);
            break;
        case IP_PROTO_UDP:
            udp_process_packet(payload, payload_len, ip->src_ip);
            break;
        case IP_PROTO_TCP:
            tcp_process_packet(payload, payload_len, ip->src_ip);
            break;
        default:
            break;
    }
}
