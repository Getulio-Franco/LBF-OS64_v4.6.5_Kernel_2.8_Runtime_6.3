#ifndef NET_INTERFACE_H
#define NET_INTERFACE_H

#include "../system/liblib.h"

#define ETH_P_IP   0x0800
#define ETH_P_ARP  0x0806
#define ETH_ALEN   6
#define ETH_HLEN   14

typedef struct {
    uint8_t  dest_mac[ETH_ALEN];
    uint8_t  src_mac[ETH_ALEN];
    uint16_t ethertype;
} __attribute__((packed)) ethernet_header_t;

int net_init_interface(void);
int net_send_frame(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, uint16_t payload_len);
int net_poll_frame(void* rx_buffer, uint16_t* out_len);
void net_get_my_mac(uint8_t out_mac[ETH_ALEN]);

#endif // NET_INTERFACE_H
