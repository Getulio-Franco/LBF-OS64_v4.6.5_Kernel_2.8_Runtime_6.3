/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/net_poll.c
 * DESCRIPTION: Loop de recepção e despachante de quadros Ethernet
 * 
 * MODIFICATION: Adicionado debug extensivo para rastrear RX usando sys_debug()
 * ============================================================================ */

#include "net_poll.h"
#include "net_interface.h"
#include "arp.h"
#include "ip.h"
#include "net_utils.h"
#include "../system/liblib.h" 
#include "../system/sysutils.h"
#include "../system/string.h"

void net_poll(void) {
    uint8_t buffer[2048];
    uint16_t len = 0;
    static int poll_count = 0;
    static int frame_count = 0;

    // ============================================================
    // DEBUG 1: Poll iniciado (a cada 10 polls para não floodar)
    // ============================================================
    poll_count++;
    if (poll_count % 10 == 0) {
        char msg[64];
        strcpy(msg, "[NET_POLL] Poll #");
        IntToStr(poll_count, msg + strlen(msg));
        sys_debug(msg);
    }

    // ============================================================
    // DEBUG 2: Chamando net_poll_frame()
    // ============================================================
    sys_debug("[NET_POLL] Chamando net_poll_frame()...");

    // ============================================================
    // DEBUG 3: Verifica o retorno de net_poll_frame()
    // ============================================================
    int ret = net_poll_frame(buffer, &len);
    
    char ret_msg[64];
    strcpy(ret_msg, "[NET_POLL] net_poll_frame() retornou: ");
    IntToStr(ret, ret_msg + strlen(ret_msg));
    sys_debug(ret_msg);

    // Se retornou 0, tem frame!
    if (ret == 0) {
        // ============================================================
        // DEBUG 4: Frame recebido!
        // ============================================================
        frame_count++;
        char msg[64];
        strcpy(msg, "[NET_POLL] >>> FRAME #");
        IntToStr(frame_count, msg + strlen(msg));
        strcat(msg, " recebido: ");
        UIntToStr(len, msg + strlen(msg));
        strcat(msg, " bytes");
        sys_debug(msg);

        // ============================================================
        // DEBUG 5: Mostra os primeiros bytes (Ethernet Header)
        // ============================================================
        if (len >= ETH_HLEN) {
            strcpy(msg, "[NET_POLL] Ethernet Header: ");
            for (int i = 0; i < 14 && i < len; i++) {
                char hex[4];
                IntToHex(buffer[i], hex, 2);
                strcat(msg, hex + 2);
                if (i < 13) strcat(msg, ":");
            }
            sys_debug(msg);
        }

        // Quadros menores que o cabeçalho Ethernet são descartados
        if (len < ETH_HLEN) {
            sys_debug("[NET_POLL] Frame menor que ETH_HLEN, descartando");
            len = 0;
            return;
        }

        const ethernet_header_t* eth = (const ethernet_header_t*)buffer;
        uint16_t ethertype = ntohs(eth->ethertype);

        // ============================================================
        // DEBUG 6: EtherType identificado
        // ============================================================
        strcpy(msg, "[NET_POLL] EtherType = 0x");
        IntToHex(ethertype, msg + strlen(msg), 4);
        strcat(msg, " (");
        if (ethertype == ETH_P_ARP) {
            strcat(msg, "ARP");
        } else if (ethertype == ETH_P_IP) {
            strcat(msg, "IP");
        } else {
            strcat(msg, "DESCONHECIDO");
        }
        strcat(msg, ")");
        sys_debug(msg);

        const uint8_t* payload = buffer + ETH_HLEN;
        uint16_t payload_len = len - ETH_HLEN;

        // ============================================================
        // DEBUG 7: Encaminhando para o protocolo
        // ============================================================
        switch (ethertype) {
            case ETH_P_ARP:
                sys_debug("[NET_POLL] Encaminhando para arp_process_packet()");
                arp_process_packet(payload, payload_len);
                break;
            case ETH_P_IP:
                sys_debug("[NET_POLL] Encaminhando para ip_process_packet()");
                ip_process_packet(payload, payload_len);
                break;
            default:
                strcpy(msg, "[NET_POLL] EtherType desconhecido: 0x");
                IntToHex(ethertype, msg + strlen(msg), 4);
                sys_debug(msg);
                break;
        }

        len = 0;
    } else {
        // ============================================================
        // DEBUG 8: net_poll_frame() retornou erro (sem frames)
        // ============================================================
        // Mostra o código de retorno
        if (poll_count % 100 == 0) {
            char err_msg[64];
            strcpy(err_msg, "[NET_POLL] net_poll_frame() retornou ");
            IntToStr(ret, err_msg + strlen(err_msg));
            strcat(err_msg, " (sem frames)");
            sys_debug(err_msg);
        }
    }
}
