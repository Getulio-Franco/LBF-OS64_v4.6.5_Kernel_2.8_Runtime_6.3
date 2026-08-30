#include "net_interface.h"
#include "net_utils.h"

int net_init_interface(void) {
    return 0;
}

int net_send_frame(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, uint16_t payload_len) {
    uint8_t frame_buffer[1518];
    uint8_t my_mac[6];

    // Validação de entrada
    if (!dest_mac || !payload || payload_len == 0) {
        return -1;
    }

    uint16_t header_len = sizeof(ethernet_header_t);
    if ((payload_len + header_len) > 1518) {
        return -1; // Excede MTU Ethernet máximo
    }

    // Obtém o MAC da interface
    net_get_my_mac(my_mac);

    // Monta o cabeçalho Ethernet
    ethernet_header_t* eth = (ethernet_header_t*)frame_buffer;
    for (int i = 0; i < 6; i++) {
        eth->dest_mac[i] = dest_mac[i];
        eth->src_mac[i]  = my_mac[i];
    }
    
    // CORREÇÃO: Garante que o EtherType esteja em Network Byte Order (Big-Endian) no cabo.
    // As chamadas agora passam ETH_P_IP / ETH_P_ARP crus, e nós fazemos o htons aqui.
    eth->ethertype = htons(ethertype);

    // Copia o payload logo após o cabeçalho Ethernet
    uint8_t* payload_dst = frame_buffer + header_len;
    const uint8_t* payload_src = (const uint8_t*)payload;
    for (uint16_t i = 0; i < payload_len; i++) {
        payload_dst[i] = payload_src[i];
    }

    uint16_t total_len = header_len + payload_len;

    // Preenchimento (Padding) com zeros para atingir o tamanho mínimo de quadro IEEE 802.3 (60 bytes sem FCS)
    if (total_len < 60) {
        for (uint16_t i = total_len; i < 60; i++) {
            frame_buffer[i] = 0;
        }
        total_len = 60;
    }

    // Envia o quadro via Syscall e retorna o código de status
    return (int)_do_syscall(SYS_NET_SEND, (uint64_t)frame_buffer, (uint64_t)total_len, 0, 0, 0);
}

int net_poll_frame(void* rx_buffer, uint16_t* out_len) {
    // Validação de ponteiros para evitar falhas de segmentação antes de entrar no Ring 0
    if (!rx_buffer || !out_len) {
        return -1;
    }
    return (int)_do_syscall(SYS_NET_RECV, (uint64_t)rx_buffer, (uint64_t)out_len, 0, 0, 0);
}

void net_get_my_mac(uint8_t out_mac[ETH_ALEN]) {
    if (!out_mac) {
        return;
    }
    _do_syscall(SYS_NET_GET_MAC, (uint64_t)out_mac, 0, 0, 0, 0);
}
