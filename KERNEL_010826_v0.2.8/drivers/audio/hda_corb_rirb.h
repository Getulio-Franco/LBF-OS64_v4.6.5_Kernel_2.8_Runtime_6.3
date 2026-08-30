#ifndef HDA_CORB_RIRB_H
#define HDA_CORB_RIRB_H

#include <stdint.h>
#include <stdbool.h>
#include "hda_pci.h"

// Estrutura de resposta RIRB (64 bits - Especificação Intel HDA)
typedef struct hda_rirb_entry {
    uint32_t response;      // Bits [31:0]: Resposta do Verbo / Parâmetro
    uint32_t response_ex;   // Bits [63:32]: Codec CAD (bits 3:0) + Status Unsolicited (bit 4)
} __attribute__((packed)) hda_rirb_entry_t;

// Contexto do Transport Layer (CORB/RIRB)
typedef struct {
    hda_hardware_context_t* hw;
    
    // CORB (Command Outbound Ring Buffer)
    uint32_t* corb_buffer;
    uint64_t  corb_phys_addr;
    uint16_t  corb_write_pos;
    uint16_t  corb_size;

    // RIRB (Response Inbound Ring Buffer)
    hda_rirb_entry_t* rirb_buffer;
    uint64_t  rirb_phys_addr;
    uint16_t  rirb_read_pos;
    uint16_t  rirb_size;
    
    bool is_initialized;
} hda_corb_rirb_t;

// API da Camada de Transporte
bool hda_corb_rirb_init(hda_corb_rirb_t* ring_ctx, hda_hardware_context_t* hw_ctx);
void hda_corb_rirb_stop(hda_corb_rirb_t* ring_ctx);
bool hda_send_verb(hda_corb_rirb_t* ring_ctx, uint8_t codec_addr, uint32_t verb_data, uint32_t* out_response);
uint32_t hda_send_verb_immediate(hda_corb_rirb_t* ring_ctx, uint8_t codec_addr, uint32_t verb_data);

#endif // HDA_CORB_RIRB_H
