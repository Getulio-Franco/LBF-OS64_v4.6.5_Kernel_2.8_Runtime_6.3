/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network Stack)
FILE: net_user/arp.c
DESCRIPTION: Processamento de requisições/respostas ARP e Cache de MACs
CORREÇÃO v3.0: Convenção de IP "wire-correct" para x86 restaurada
               (estilo antigo do MAKE_IP). A versão "Big Endian" invertia
               os bytes no cabo e fazia o gateway descartar o ARP Request.
============================================================================ */
#include "arp.h"
#include "net_interface.h"
#include "net_utils.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "../system/string.h"

// ============================================================================
// CONVERSÃO IP -> STRING (CONVENÇÃO ANTIGA / WIRE-CORRECT)
// No x86, o 1º octeto é o byte MENOS significativo (ip & 0xFF)
// ============================================================================
static void ip_to_str_debug(uint32_t ip, char* dest) {
    if (!dest) return;
    uint8_t b1 = ip & 0xFF;          // 10
    uint8_t b2 = (ip >> 8) & 0xFF;   // 0
    uint8_t b3 = (ip >> 16) & 0xFF;  // 2
    uint8_t b4 = (ip >> 24) & 0xFF;  // 15
    char temp[16];
    strcpy(dest, "");
    IntToStr(b1, temp); strcat(dest, temp); strcat(dest, ".");
    IntToStr(b2, temp); strcat(dest, temp); strcat(dest, ".");
    IntToStr(b3, temp); strcat(dest, temp); strcat(dest, ".");
    IntToStr(b4, temp); strcat(dest, temp);
}

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================
static arp_entry_t g_arp_table[ARP_TABLE_SIZE];
static uint32_t    g_my_ip = 0;
static uint8_t     g_next_victim = 0;

static inline void mac_copy(uint8_t* dest, const uint8_t* src) {
    for (int i = 0; i < 6; i++) dest[i] = src[i];
}

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================
void arp_init(uint32_t my_ip) {
    char msg[64];
    strcpy(msg, "[ARP] Inicializando subsistema ARP com IP: ");
    ip_to_str_debug(my_ip, msg + strlen(msg));
    sys_debug(msg);
    g_my_ip = my_ip;
    g_next_victim = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        g_arp_table[i].valid = false;
        g_arp_table[i].ip = 0;
    }
}

void arp_set_ip(uint32_t my_ip) {
    char msg[64];
    strcpy(msg, "[ARP] IP alterado para: ");
    ip_to_str_debug(my_ip, msg + strlen(msg));
    sys_debug(msg);
    g_my_ip = my_ip;
}

uint32_t arp_get_ip(void) {
    return g_my_ip;
}

static void arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
    if (ip == 0 || !mac) return;
    char msg[128];
    strcpy(msg, "[ARP] Inserindo cache: IP=");
    ip_to_str_debug(ip, msg + strlen(msg));
    strcat(msg, " MAC=");
    for (int i = 0; i < 6; i++) {
        char hex[4];
        IntToHex(mac[i], hex, 2);
        strcat(msg, hex + 2);
        if (i < 5) strcat(msg, ":");
    }
    sys_debug(msg);

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            mac_copy(g_arp_table[i].mac, mac);
            return;
        }
    }
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].ip = ip;
            mac_copy(g_arp_table[i].mac, mac);
            g_arp_table[i].valid = true;
            return;
        }
    }
    g_arp_table[g_next_victim].ip = ip;
    mac_copy(g_arp_table[g_next_victim].mac, mac);
    g_arp_table[g_next_victim].valid = true;
    g_next_victim = (g_next_victim + 1) % ARP_TABLE_SIZE;
}

bool arp_lookup(uint32_t ip, uint8_t out_mac[6]) {
    if (!out_mac) return false;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            mac_copy(out_mac, g_arp_table[i].mac);
            return true;
        }
    }
    return false;
}

// ============================================================================
// ARP_SEND_REQUEST — IGUAL AO ANTIGO QUE FUNCIONAVA (opção 2)
// ============================================================================
int arp_send_request(uint32_t target_ip) {
    char msg[128];
    strcpy(msg, "[ARP] Preparando ARP Request para IP=");
    ip_to_str_debug(target_ip, msg + strlen(msg));
    sys_debug(msg);

    arp_header_t packet;
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t my_mac[6];

    // Fallback seguro NO ESTILO ANTIGO (wire-correct), nunca 0x0A00020F!
    if (g_my_ip == 0) {
        g_my_ip = MAKE_IP(10, 0, 2, 15);
        sys_debug("[ARP] IP padrão NAT configurado: 10.0.2.15");
    }

    strcpy(msg, "[ARP] Meu IP: ");
    ip_to_str_debug(g_my_ip, msg + strlen(msg));
    sys_debug(msg);

    strcpy(msg, "[ARP] IP Alvo: ");
    ip_to_str_debug(target_ip, msg + strlen(msg));
    sys_debug(msg);

    net_get_my_mac(my_mac);

    // Monta o pacote ARP (campos IP gravados crus = wire-correct no x86)
    packet.htype = htons(ARP_HTYPE_ETHERNET);
    packet.ptype = htons(ARP_PTYPE_IPV4);
    packet.hlen  = 6;
    packet.plen  = 4;
    packet.opcode = htons(ARP_OP_REQUEST);
    packet.sender_ip = g_my_ip;
    packet.target_ip = target_ip;
    mac_copy(packet.sender_mac, my_mac);
    for (int i = 0; i < 6; i++) packet.target_mac[i] = 0x00;

    sys_debug("[ARP] Chamando net_send_frame()...");
    // EtherType cru: o net_send_frame faz o htons() internamente
    int ret = net_send_frame(broadcast_mac, ETH_P_ARP, &packet, sizeof(arp_header_t));

    if (ret < 0) {
        sys_debug("[ARP] ERRO ao enviar ARP Request!");
    } else {
        sys_debug("[ARP] ARP Request enviado com sucesso!");
    }
    return ret;
}

// ============================================================================
// ARP_PROCESS_PACKET — IGUAL AO ANTIGO (compara valores crus, mesma convenção)
// ============================================================================
void arp_process_packet(const uint8_t* buffer, uint16_t len) {
    if (!buffer || len < sizeof(arp_header_t)) return;

    const arp_header_t* arp = (const arp_header_t*)buffer;

    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET || ntohs(arp->ptype) != ARP_PTYPE_IPV4) {
        return;
    }

    uint16_t opcode = ntohs(arp->opcode);
    bool is_for_me = (g_my_ip == 0) || (arp->target_ip == g_my_ip);

    // Reply ou Request p/ mim: aprende o MAC do remetente
    if (opcode == ARP_OP_REPLY || (opcode == ARP_OP_REQUEST && is_for_me)) {
        char msg[128];
        strcpy(msg, "[ARP] Reply recebido de IP=");
        ip_to_str_debug(arp->sender_ip, msg + strlen(msg));
        sys_debug(msg);
        arp_cache_insert(arp->sender_ip, arp->sender_mac);
    }

    // Responde a REQUEST direcionado a nós
    if (opcode == ARP_OP_REQUEST && is_for_me && g_my_ip != 0) {
        arp_header_t reply;
        uint8_t my_mac[6];
        net_get_my_mac(my_mac);
        reply.htype = htons(ARP_HTYPE_ETHERNET);
        reply.ptype = htons(ARP_PTYPE_IPV4);
        reply.hlen  = 6;
        reply.plen  = 4;
        reply.opcode = htons(ARP_OP_REPLY);
        reply.sender_ip = g_my_ip;
        reply.target_ip = arp->sender_ip;
        mac_copy(reply.sender_mac, my_mac);
        mac_copy(reply.target_mac, arp->sender_mac);
        net_send_frame(arp->sender_mac, ETH_P_ARP, &reply, sizeof(arp_header_t));
    }
}
