/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/net_poll.c
 * DESCRIPTION: Loop de recepção e despachante de quadros Ethernet
 * ============================================================================ */

#include "net_poll.h"
#include "net_interface.h"
#include "arp.h"
#include "ip.h"
#include "net_utils.h"

void net_poll(void) {
    uint8_t buffer[2048]; // Tamanho seguro para quadros Ethernet
    uint16_t len = 0;

    // Drena continuamente todos os quadros presentes na fila RX
    while (net_poll_frame(buffer, &len) == 0) {
        // Quadros menores que o cabeçalho Ethernet (14 bytes) são descartados
        if (len < ETH_HLEN) {
            len = 0;
            continue;
        }

        const ethernet_header_t* eth = (const ethernet_header_t*)buffer;
        uint16_t ethertype = ntohs(eth->ethertype);

        const uint8_t* payload = buffer + ETH_HLEN;
        uint16_t payload_len = len - ETH_HLEN;

        // Encaminha a carga útil para o protocolo correspondente
        switch (ethertype) {
            case ETH_P_ARP:
                sys_debug("[NET] Pacote ARP recebido no net_poll!\n");
                arp_process_packet(payload, payload_len);
                break;

            case ETH_P_IP:
                sys_debug("[NET] Pacote IP recebido no net_poll!\n");
                ip_process_packet(payload, payload_len);
                break;

            default:
                sys_debug("[NET] EtherType desconhecido recebido.\n");
                break;
        }

        len = 0; // Reseta o tamanho para a próxima iteração
    }
}
