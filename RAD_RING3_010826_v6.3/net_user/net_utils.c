/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: arp.c
 * DESCRIPTION: Processamento de requisições/respostas ARP e Cache de MACs
 * ============================================================================ */

#include "arp.h"
#include "net_interface.h"
#include "net_utils.h"

static arp_entry_t g_arp_table[ARP_TABLE_SIZE];
static uint32_t   g_my_ip = 0;
static uint8_t    g_next_victim = 0;

void arp_init(uint32_t my_ip) {
    g_my_ip = my_ip;
    g_next_victim = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        g_arp_table[i].valid = false;
    }
}

static void arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
    if (ip == 0 || !mac) return;

    // 1. Atualiza se o IP já existir na tabela
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            for (int j = 0; j < 6; j++) g_arp_table[i].mac[j] = mac[j];
            return;
        }
    }

    // 2. Procura um slot livre
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].ip = ip;
            for (int j = 0; j < 6; j++) g_arp_table[i].mac[j] = mac[j];
            g_arp_table[i].valid = true;
            return;
        }
    }

    // 3. Substituição circular (Round-Robin) se a tabela estiver cheia
    g_arp_table[g_next_victim].ip = ip;
    for (int j = 0; j < 6; j++) g_arp_table[g_next_victim].mac[j] = mac[j];
    g_arp_table[g_next_victim].valid = true;
    g_next_victim = (g_next_victim + 1) % ARP_TABLE_SIZE;
}

bool arp_lookup(uint32_t ip, uint8_t out_mac[6]) {
    if (!out_mac) return false;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            for (int j = 0; j < 6; j++) out_mac[j] = g_arp_table[i].mac[j];
            return true;
        }
    }
    return false;
}

int arp_send_request(uint32_t target_ip) {
    arp_header_t packet;
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t my_mac[6];

    net_get_my_mac(my_mac);

    packet.htype = htons(ARP_HTYPE_ETHERNET);
    packet.ptype = htons(ARP_PTYPE_IPV4);
    packet.hlen = 6;
    packet.plen = 4;
    packet.opcode = htons(ARP_OP_REQUEST);

    packet.sender_ip = g_my_ip;
    for (int i = 0; i < 6; i++) {
        packet.sender_mac[i] = my_mac[i];
        packet.target_mac[i] = 0x00;
    }
    packet.target_ip = target_ip;

    return net_send_frame(broadcast_mac, ETH_P_ARP, &packet, sizeof(arp_header_t));
}

void arp_process_packet(const uint8_t* buffer, uint16_t len) {
    if (!buffer || len < sizeof(arp_header_t)) return;

    const arp_header_t* arp = (const arp_header_t*)buffer;

    // Valida o tipo de hardware, protocolo e tamanho de endereços
    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET ||
        ntohs(arp->ptype) != ARP_PTYPE_IPV4 ||
        arp->hlen != 6 || arp->plen != 4) {
        return;
    }

    bool is_for_me = (arp->target_ip == g_my_ip);

    // Grava na cache apenas se for direcionado ao IP local
    if (is_for_me) {
        arp_cache_insert(arp->sender_ip, arp->sender_mac);
    }

    uint16_t opcode = ntohs(arp->opcode);

    if (opcode == ARP_OP_REQUEST && is_for_me) {
        arp_header_t reply;
        uint8_t my_mac[6];
        net_get_my_mac(my_mac);

        reply.htype = htons(ARP_HTYPE_ETHERNET);
        reply.ptype = htons(ARP_PTYPE_IPV4);
        reply.hlen = 6;
        reply.plen = 4;
        reply.opcode = htons(ARP_OP_REPLY);

        reply.sender_ip = g_my_ip;
        for (int i = 0; i < 6; i++) {
            reply.sender_mac[i] = my_mac[i];
            reply.target_mac[i] = arp->sender_mac[i];
        }
        reply.target_ip = arp->sender_ip;

        net_send_frame(arp->sender_mac, ETH_P_ARP, &reply, sizeof(arp_header_t));
    }
}
