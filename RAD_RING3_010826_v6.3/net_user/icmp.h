/* ============================================================================
 * ARCHITECTURE: Ring 3 (User Space Network Stack)
 * FILE: net_user/icmp.h
 * DESCRIPTION: Estruturas do protocolo ICMP e protótipos de mensagens
 * ============================================================================ */

#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct {
    uint8_t  type;      
    uint8_t  code;      
    uint16_t checksum;  
    uint16_t id;        
    uint16_t sequence;  
} __attribute__((packed)) icmp_header_t;

int  icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq, const void* data, uint16_t data_len);
void icmp_process_packet(const uint8_t* buffer, uint16_t len, uint32_t src_ip);

#endif // ICMP_H
