/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: audio_server.c
Versão: 2.0
Data: 27/08/2026
Autor: LBF-OS Team

Mudanças v2.1:
    - REMOVIDO uso de PMM (pmm_alloc_blocks)
    - ADICIONADO buffers ESTÁTICOS alinhados (padrão AC97)
    - Ring buffers agora são arrays estáticos dentro da estrutura
    - Resolve Kernel Panic em syscalls (Page Fault em contexto Ring 3)
    
Motivo:
    - Buffers PMM não são mapeados no PML4 do processo Ring 3
    - Syscalls rodam com CR3 do processo, não do kernel
    - AC97 funciona porque usa buffers estáticos no .bss
====================================================================
*/

#include "audio_server.h"
#include "util/string.h"
#include "util/sysutils.h"
#include "drivers/video.h"
#include "hda_volume.h"

/* Buffer estático temporário para cópia segura da Ring 3 */
static int16_t k_write_temp[2048] __attribute__((aligned(4096)));

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================
bool audio_server_init(audio_server_t* server, hda_dma_stream_t* dma) {
    if (!server || !dma) return false;

    server->volume_dirty   = false;
    server->pending_volume = 100;
    server->dma_stream = dma;
    server->dma_use_second_half = false;
    server->is_initialized = false;

    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        server->streams[i].active = false;
        server->streams[i].pid = 0;
        server->streams[i].volume = 100;
        server->streams[i].muted = false;
        server->streams[i].rb_write_pos = 0;
        server->streams[i].rb_read_pos = 0;
        memset(server->streams[i].ring_buffer, 0, sizeof(server->streams[i].ring_buffer));
    }

    memset(server->mixer_buffer, 0, sizeof(server->mixer_buffer));

    server->is_initialized = true;
    vga_print_string("[AUDIO_SRV] Servidor de audio iniciado.\n", 0, 38);
    return true;
}

// ============================================================================
// GERENCIAMENTO DE STREAMS
// ============================================================================
int audio_server_open_stream(audio_server_t* server, uint32_t pid) {
    if (!server || !server->is_initialized) return -1;

    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        if (!server->streams[i].active) {
            server->streams[i].active = true;
            server->streams[i].pid = pid;
            server->streams[i].volume = 100;
            server->streams[i].muted = false;
            server->streams[i].rb_write_pos = 0;
            server->streams[i].rb_read_pos = 0;
            return i;
        }
    }
    return -1;
}

void audio_server_close_stream(audio_server_t* server, int stream_id) {
    if (!server || stream_id < 0 || stream_id >= MAX_AUDIO_STREAMS) return;
    server->streams[stream_id].active = false;
}

// ============================================================================
// ESCRITA DE DADOS (RING 3 -> KERNEL)
// ============================================================================
int audio_server_write(audio_server_t* server, int stream_id, const void* user_buffer, uint32_t size_bytes) {
    if (!server || !server->is_initialized) return -1;
    if (stream_id < 0 || stream_id >= MAX_AUDIO_STREAMS) return -1;
    if (!user_buffer || size_bytes == 0 || size_bytes % 4 != 0) return -1;
    if (size_bytes > sizeof(k_write_temp)) return -1;

    audio_stream_t* s = &server->streams[stream_id];
    if (!s->active) return -1;

    memcpy(k_write_temp, user_buffer, size_bytes);

    uint32_t samples = size_bytes / sizeof(int16_t);
    uint32_t written_samples = 0;

    for (uint32_t i = 0; i < samples; i++) {
        uint32_t next_pos = (s->rb_write_pos + 1) % RB_TOTAL_SAMPLES;
        if (next_pos == s->rb_read_pos) break;

        s->ring_buffer[s->rb_write_pos] = k_write_temp[i];
        s->rb_write_pos = next_pos;
        written_samples++;
    }

    return (int)(written_samples * sizeof(int16_t));
}

// ============================================================================
// MIXER E ALIMENTAÇÃO DO DMA
// ============================================================================
void audio_server_on_irq(audio_server_t* server) {
    if (!server || !server->is_initialized || !server->dma_stream) return;

    memset(server->mixer_buffer, 0, MIXER_BUFFER_SIZE * sizeof(int16_t));

    uint32_t half_dma_frames = server->dma_stream->pcm_buffer_size / (sizeof(int16_t) * SYS_CHANNELS * 2);
    uint32_t half_dma_samples = half_dma_frames * SYS_CHANNELS;

    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        audio_stream_t* s = &server->streams[i];
        if (!s->active || s->muted) continue;

        for (uint32_t j = 0; j < half_dma_samples; j++) {
            int16_t sample = 0;
            if (s->rb_read_pos != s->rb_write_pos) {
                sample = s->ring_buffer[s->rb_read_pos];
                s->rb_read_pos = (s->rb_read_pos + 1) % RB_TOTAL_SAMPLES;
            }

            int32_t mixed_sample = (int32_t)server->mixer_buffer[j] + ((sample * s->volume) / 100);
            if (mixed_sample > 32767) mixed_sample = 32767;
            if (mixed_sample < -32768) mixed_sample = -32768;

            server->mixer_buffer[j] = (int16_t)mixed_sample;
        }
    }

    int16_t* target_dma_buffer;
    if (server->dma_use_second_half) {
        target_dma_buffer = server->dma_stream->pcm_virtual_buffers[HDA_BUFFER_PAGES / 2];
    } else {
        target_dma_buffer = server->dma_stream->pcm_virtual_buffers[0];
    }

    memcpy(target_dma_buffer, server->mixer_buffer, half_dma_samples * sizeof(int16_t));

    extern void hda_dma_sync_cache(void);
    hda_dma_sync_cache();

    server->dma_use_second_half = !server->dma_use_second_half;
}

// ============================================================================
// POLLING PERIÓDICO
// ============================================================================
// Regrava UMA metade (0 = páginas 0-3, 1 = páginas 4-7)
void audio_server_refill_half(audio_server_t* server, uint8_t half) {
    hda_dma_stream_t* d = server->dma_stream;
    uint32_t half_frames  = d->pcm_buffer_size / (sizeof(int16_t) * SYS_CHANNELS * 2);
    uint32_t half_samples = half_frames * SYS_CHANNELS;

    memset(server->mixer_buffer, 0, half_samples * sizeof(int16_t));

    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        audio_stream_t* s = &server->streams[i];
        if (!s->active || s->muted) continue;
        for (uint32_t j = 0; j < half_samples; j++) {
            int16_t sample = 0;
            if (s->rb_read_pos != s->rb_write_pos) {
                sample = s->ring_buffer[s->rb_read_pos];
                s->rb_read_pos = (s->rb_read_pos + 1) % RB_TOTAL_SAMPLES;
            }
            int32_t mixed = (int32_t)server->mixer_buffer[j] + ((sample * s->volume) / 100);
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            server->mixer_buffer[j] = (int16_t)mixed;
        }
    }

    int16_t* target = d->pcm_virtual_buffers[half * (HDA_BUFFER_PAGES / 2)];
    memcpy(target, server->mixer_buffer, half_samples * sizeof(int16_t));
    hda_dma_sync_cache();
}

// Preenche as DUAS metades antes do RUN (evita tocar lixo no 1º ciclo)
void audio_server_prime(audio_server_t* server) {
    if (!server || !server->is_initialized) return;
    audio_server_refill_half(server, 0);
    audio_server_refill_half(server, 1);
}

// Chamado no loop do kernel: só regrava quando o IRQ autoriza
void audio_server_poll(audio_server_t* server) {
    if (!server || !server->is_initialized || !server->dma_stream) return;

    // v2.2: aplica volume de hardware pendente (contexto kernel = CR3 do kernel)
    if (server->volume_dirty && server->dma_stream->codec) {
        server->volume_dirty = false;
        hda_codec_set_volume(server->dma_stream->codec, server->pending_volume);
        vga_print_string("[AUDIO_SRV] volume de hardware aplicado pelo kernel\n", 0, 38);
    }

    hda_dma_stream_t* d = server->dma_stream;
    if (!d->is_running) return;

    if (d->irq_count > 0) {
        if (d->refill_pending) {
            d->refill_pending = false;
            audio_server_refill_half(server, d->completed_index);
        }
    } else {
        audio_server_refill_half(server, server->dma_use_second_half ? 1 : 0);
        server->dma_use_second_half = !server->dma_use_second_half;
    }
}

// ============================================================================
// CONTROLE DE VOLUME (SOFTWARE + HARDWARE)
// ============================================================================
/*void audio_server_set_volume(audio_server_t* server, uint8_t percent, bool mute) {
    if (!server || !server->is_initialized) return;
    if (percent > 100) percent = 100;

    // Volume de SOFTWARE (mixer): buffers estáticos, seguro no CR3 do processo
    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        server->streams[i].volume = percent;
        server->streams[i].muted  = mute;
    }

    // Volume de HARDWARE: NÃO enviar verbos aqui (CORB/MMIO não mapeados no Ring 3).
    // Apenas marca; o loop do kernel aplica com CR3 do kernel.
    server->pending_volume = percent;
    server->volume_dirty   = true;

    vga_print_string("[AUDIO_SRV] set_volume: aplicado (software), hardware adiado\n", 0, 38);
}*/

void audio_server_set_volume(audio_server_t* server, uint8_t percent, bool mute) {
    if (!server || !server->is_initialized) return;
    if (percent > 100) percent = 100;

    // 100% SOFTWARE: o mixer faz todo o controle (0 = silêncio digital)
    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        server->streams[i].volume = percent;
        server->streams[i].muted  = (percent == 0);
    }

    // NENHUM verbo de hardware aqui.
    // O ganho do codec fica no máximo (0xB07F setado no boot) e o mixer
    // controla tudo. Isso elimina o Page Fault de CORB/MMIO na syscall.
}
