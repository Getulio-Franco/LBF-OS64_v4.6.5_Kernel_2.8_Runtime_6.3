#include "hda_dma.h"
#include "hda_pci.h"
#include "util/string.h"
#include "drivers/video.h"
#include "drivers/audio/hda_codec.h"

// ============================================================================
// REGISTRADORES DE STREAM DMA (RELATIVOS A STREAM_OFFSET)
// ============================================================================
#define SD_CTL          0x00
#define SD_STS          0x03
#define SD_LPIB         0x04
#define SD_CBL          0x08
#define SD_LVI          0x0C
#define SD_FMT          0x12
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

#define SD_CTL_SRST     (1U << 0)
#define SD_CTL_RUN      (1U << 1)
#define SD_CTL_IOCE     (1U << 2) // Interrupt on Completion Enable
#define SD_CTL_FEIE     (1U << 3) // FIFO Error Interrupt Enable
#define SD_CTL_DEIE     (1U << 4) // Descriptor Error Interrupt Enable

#define SD_CTL_STREAM_TAG_MASK   0x00F00000
#define SD_CTL_STREAM_TAG_SHIFT  20

#define SD_STS_BCIS     (1U << 2) // Buffer Completion Interrupt Status
#define SD_STS_FIFOE    (1U << 3) // FIFO Error
#define SD_STS_DESE     (1U << 4) // Descriptor Error

#define HDA_FMT_48KHZ_16BIT_STEREO 0x0011
#define HDA_RESET_TIMEOUT 100000
#define HDA_RUN_TIMEOUT   100000

// Adicione a instância global
hda_dma_stream_t g_hda_stream;

static inline uint64_t virt_to_phys(void* virt_addr) {
    return (uint64_t)(uintptr_t)virt_addr;
}

static inline void stream_write8(hda_dma_stream_t* s, uint32_t reg, uint8_t val) {
    *(volatile uint8_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}

static inline void stream_write16(hda_dma_stream_t* s, uint32_t reg, uint16_t val) {
    *(volatile uint16_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}

static inline void stream_write32(hda_dma_stream_t* s, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(s->hw->mmio_virt + s->stream_offset + reg) = val;
}

static inline uint8_t stream_read8(hda_dma_stream_t* s, uint32_t reg) {
    return *(volatile uint8_t*)(s->hw->mmio_virt + s->stream_offset + reg);
}

static inline uint32_t stream_read32(hda_dma_stream_t* s, uint32_t reg) {
    return *(volatile uint32_t*)(s->hw->mmio_virt + s->stream_offset + reg);
}

void hda_dma_sync_cache(void) {
    asm volatile("mfence" ::: "memory");
    asm volatile("wbinvd" ::: "memory");
}

static bool hda_dma_reset_stream(hda_dma_stream_t* stream) {
    uint32_t timeout;
    uint32_t ctl = stream_read32(stream, SD_CTL) & ~SD_CTL_RUN;
    stream_write32(stream, SD_CTL, ctl);

    timeout = HDA_RUN_TIMEOUT;
    while ((stream_read8(stream, SD_CTL) & SD_CTL_RUN) && --timeout);
    if (!timeout) return false;

    stream_write32(stream, SD_CTL, SD_CTL_SRST);
    timeout = HDA_RESET_TIMEOUT;
    while (!(stream_read8(stream, SD_CTL) & SD_CTL_SRST) && --timeout);
    if (!timeout) return false;

    stream_write32(stream, SD_CTL, 0);
    timeout = HDA_RESET_TIMEOUT;
    while ((stream_read8(stream, SD_CTL) & SD_CTL_SRST) && --timeout);
    if (!timeout) return false;

    return !(stream_read32(stream, SD_CTL) & (SD_CTL_RUN | SD_CTL_SRST));
}

bool hda_dma_init(hda_dma_stream_t* stream, hda_hardware_context_t* hw, hda_codec_t* codec) {
    if (!stream || !hw || !codec || !codec->is_initialized) return false;

    stream->hw = hw;
    stream->codec = codec;
    stream->stream_tag = 1;
    stream->stream_offset = 0x80 + (hw->num_input_streams * 0x20);
    stream->pcm_buffer_size = HDA_TOTAL_BUFFER;
    stream->is_running = false;
    stream->current_buffer_index = 0;

    for (int i = 0; i < HDA_BUFFER_PAGES; i++) {
        stream->pcm_virtual_buffers[i] = stream->pcm_buffers[i];
    }

    uint32_t half_buffer_size = HDA_TOTAL_BUFFER / 2;

    /* Entrada 0 da BDL: Buffer A (Primeira metade da memória) */
    stream->bdl_table[0].phys_addr = virt_to_phys(stream->pcm_buffers[0]);
    stream->bdl_table[0].length    = half_buffer_size;
    stream->bdl_table[0].flags     = HDA_BDL_FLAG_IOC; // Dispara IRQ no fim do Buffer A

    /* Entrada 1 da BDL: Buffer B (Segunda metade da memória) */
    stream->bdl_table[1].phys_addr = virt_to_phys(stream->pcm_buffers[HDA_BUFFER_PAGES / 2]);
    stream->bdl_table[1].length    = half_buffer_size;
    stream->bdl_table[1].flags     = HDA_BDL_FLAG_IOC; // Dispara IRQ no fim do Buffer B

    stream->bdl_phys_addr = virt_to_phys(stream->bdl_table);

    if (!hda_dma_reset_stream(stream)) return false;

    /* Configuração de tamanho total do Ring e LVI (2 entradas: 0 e 1) */
    stream_write32(stream, SD_CBL, stream->pcm_buffer_size);
    stream_write16(stream, SD_LVI, HDA_BDL_ENTRIES - 1);
    stream_write16(stream, SD_FMT, HDA_FMT_48KHZ_16BIT_STEREO);

    /* Endereço Base da Tabela BDL */
    stream_write32(stream, SD_BDLPL, (uint32_t)(stream->bdl_phys_addr & 0xFFFFFFFFULL));
    stream_write32(stream, SD_BDLPU, (uint32_t)(stream->bdl_phys_addr >> 32));

    /* Ativação do Stream Tag e Interrupções de Hardware */
    uint32_t ctl = stream_read32(stream, SD_CTL);
    ctl &= ~SD_CTL_STREAM_TAG_MASK;
    ctl |= ((uint32_t)stream->stream_tag << SD_CTL_STREAM_TAG_SHIFT) & SD_CTL_STREAM_TAG_MASK;
    ctl |= SD_CTL_IOCE | SD_CTL_FEIE | SD_CTL_DEIE;
    ctl &= ~(SD_CTL_RUN | SD_CTL_SRST);

    stream_write32(stream, SD_CTL, ctl);

    if (!hda_setup_dac_stream(codec, stream->stream_tag, HDA_FMT_48KHZ_16BIT_STEREO)) return false;

    /* Limpeza inicial de status */
    stream_write8(stream, SD_STS, SD_STS_BCIS | SD_STS_FIFOE | SD_STS_DESE);
    return true;
}

void hda_dma_start(hda_dma_stream_t* stream) {
    if (!stream) return;
    hda_dma_sync_cache();

    uint32_t ctl = stream_read32(stream, SD_CTL) & ~SD_CTL_SRST;
    stream_write32(stream, SD_CTL, ctl);

    uint32_t timeout = HDA_RESET_TIMEOUT;
    while ((stream_read8(stream, SD_CTL) & SD_CTL_SRST) && --timeout);

    ctl = stream_read32(stream, SD_CTL) | SD_CTL_RUN;
    stream_write32(stream, SD_CTL, ctl);

    timeout = HDA_RUN_TIMEOUT;
    while (!(stream_read8(stream, SD_CTL) & SD_CTL_RUN) && --timeout);

    stream->is_running = (timeout > 0);
}

void hda_dma_stop(hda_dma_stream_t* stream) {
    if (!stream) return;
    uint32_t ctl = stream_read32(stream, SD_CTL) & ~SD_CTL_RUN;
    stream_write32(stream, SD_CTL, ctl);
    stream->is_running = false;
}

void hda_dma_handle_irq(hda_dma_stream_t* stream) {
    if (!stream || !stream->hw) return;              // protege contra instância não inicializada

    uint8_t status = stream_read8(stream, SD_STS);
    stream->last_error_status = status & (SD_STS_FIFOE | SD_STS_DESE);

    if (status & SD_STS_BCIS) {
        /* O DMA acabou de tocar completed_index e agora está tocando a outra metade.
           Portanto é SEGURO regravar completed_index. */
        stream->completed_index      = stream->current_buffer_index;
        stream->current_buffer_index = (stream->current_buffer_index + 1) % HDA_BDL_ENTRIES;
        stream->refill_pending       = true;
        stream->irq_count++;
    }

    if (status & (SD_STS_BCIS | SD_STS_FIFOE | SD_STS_DESE)) {
        stream_write8(stream, SD_STS, status & (SD_STS_BCIS | SD_STS_FIFOE | SD_STS_DESE));
    }
}

void hda_dma_poll(hda_dma_stream_t* stream) {
    if (!stream || !stream->is_running) return;
    hda_dma_handle_irq(stream);
}
