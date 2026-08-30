#ifndef HDA_DMA_H
#define HDA_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include "hda_codec.h"

// ============================================================================
// CONFIGURAÇÕES DE BUFFER E BDL (DOUBLE BUFFER / PING-PONG)
// ============================================================================
#define HDA_BUFFER_PAGES      8
#define HDA_PAGE_SIZE         4096
#define HDA_TOTAL_BUFFER      (HDA_BUFFER_PAGES * HDA_PAGE_SIZE)

// Quantidade de Entradas na BDL para o Double Buffering (Buffer A e Buffer B)
#define HDA_BDL_ENTRIES       2

// Bits do Campo Flags da BDL
#define HDA_BDL_FLAG_IOC      (1U << 0) // Interrupt on Completion

// ============================================================================
// ESTRUTURAS DE DADOS DMA
// ============================================================================
typedef struct hda_bdl_entry {
    uint64_t phys_addr;
    uint32_t length;
    uint32_t flags; // Bit 0: IOC (Interrupt on Completion)
} __attribute__((packed)) hda_bdl_entry_t;

typedef struct {
    hda_hardware_context_t* hw;
    hda_codec_t*            codec;

    uint32_t stream_offset;
    uint8_t  stream_tag;

    // Tabela BDL alinhada a 4096 bytes (suporta até 2 entradas no Double Buffer)
    hda_bdl_entry_t bdl_table[HDA_BDL_ENTRIES] __attribute__((aligned(4096)));
    uint64_t        bdl_phys_addr;

    // Buffers PCM alocados no .bss do Kernel (Alinhados para DMA)
    int16_t  pcm_buffers[HDA_BUFFER_PAGES][HDA_PAGE_SIZE / sizeof(int16_t)] __attribute__((aligned(4096)));
    int16_t* pcm_virtual_buffers[HDA_BUFFER_PAGES];

    uint32_t pcm_buffer_size;
    bool     is_running;
    uint8_t  last_error_status;
    uint8_t  current_buffer_index;        // metade que o DMA está tocando AGORA
    volatile uint8_t completed_index;     // metade que ACABOU de tocar (segura p/ regravar)
    volatile bool    refill_pending;      // sinal p/ o audio_server regravar
    uint32_t irq_count;                   // quantas BCIS já chegaram (0 = fallback polling)
} hda_dma_stream_t;

// ============================================================================
// PROTÓTIPOS DAS FUNÇÕES
// ============================================================================
bool hda_dma_init(hda_dma_stream_t* stream, hda_hardware_context_t* hw, hda_codec_t* codec);
void hda_dma_start(hda_dma_stream_t* stream);
void hda_dma_stop(hda_dma_stream_t* stream);
void hda_dma_handle_irq(hda_dma_stream_t* stream);
void hda_dma_poll(hda_dma_stream_t* stream);
void hda_dma_sync_cache(void);

#endif // HDA_DMA_H
