#ifndef AUDIO_SERVER_H
#define AUDIO_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/audio/hda_dma.h"

// ============================================================================
// CONFIGURAÇÕES DO MIXER E BUFFERS (PADRÃO AC97: ESTÁTICOS)
// ============================================================================
#define MAX_AUDIO_STREAMS 4
#define MIXER_BUFFER_SIZE (4096 * 4)

// Ring Buffer por stream: 16KB = 8192 amostras estéreo
#define RB_PAGE_SIZE      4096
#define RB_PAGES_COUNT    4
#define RB_TOTAL_SIZE     (RB_PAGE_SIZE * RB_PAGES_COUNT)
#define RB_TOTAL_SAMPLES  (RB_TOTAL_SIZE / sizeof(int16_t))

// Formato PCM padrão do sistema (48kHz, 16-bit, Stereo)
#define SYS_SAMPLE_RATE 48000
#define SYS_CHANNELS    2
#define SYS_BPS         16

// ============================================================================
// ESTRUTURAS DE DADOS
// ============================================================================

typedef struct {
    bool     active;
    uint32_t pid;
    
    // Ring Buffer ESTÁTICO (não usa PMM!)
    int16_t  ring_buffer[RB_TOTAL_SAMPLES] __attribute__((aligned(4096)));
    uint32_t rb_write_pos;
    uint32_t rb_read_pos;
    
    uint8_t  volume;
    bool     muted;
} audio_stream_t;

typedef struct {
    hda_dma_stream_t* dma_stream;
    audio_stream_t streams[MAX_AUDIO_STREAMS];
    int16_t mixer_buffer[MIXER_BUFFER_SIZE] __attribute__((aligned(4096)));
    bool     dma_use_second_half;
    bool     is_initialized;
    // v2.2: volume de hardware adiado para o contexto do kernel
    volatile bool volume_dirty;
    uint8_t  pending_volume;
} audio_server_t;

// ============================================================================
// FUNÇÕES PÚBLICAS
// ============================================================================

bool audio_server_init(audio_server_t* server, hda_dma_stream_t* dma);
void audio_server_on_irq(audio_server_t* server);
int audio_server_open_stream(audio_server_t* server, uint32_t pid);
void audio_server_close_stream(audio_server_t* server, int stream_id);
int audio_server_write(audio_server_t* server, int stream_id, const void* user_buffer, uint32_t size_bytes);
/**
 * Processa o mixer e alimenta o DMA (chamada periodicamente).
 * Substitui a IRQ do DMA quando esta não está funcionando.
 */
void audio_server_poll(audio_server_t* server);
void audio_server_refill_half(audio_server_t* server, uint8_t half);
void audio_server_prime(audio_server_t* server);

// CONTROLE DE VOLUME
// ============================================================================
void audio_server_set_volume(audio_server_t* server, uint8_t percent, bool mute);

#endif // AUDIO_SERVER_H
