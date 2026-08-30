#include "hda_corb_rirb.h"
#include "util/string.h"
#include "mem/pmm.h"

// Registradores HDA MMIO - Offsets CORB/RIRB
#define HDA_REG_CORBLBASE   0x40
#define HDA_REG_CORBUBASE   0x44
#define HDA_REG_CORBWP      0x48
#define HDA_REG_CORBRP      0x4A
#define HDA_REG_CORBCTL     0x4C
#define HDA_REG_CORBST      0x4D
#define HDA_REG_CORBSIZE    0x4E

#define HDA_REG_RIRBLBASE   0x50
#define HDA_REG_RIRBUBASE   0x54
#define HDA_REG_RIRBWP      0x58
#define HDA_REG_RINTCNT     0x5A
#define HDA_REG_RIRBCTL     0x5C
#define HDA_REG_RIRBST      0x5D
#define HDA_REG_RIRBSIZE    0x5E

// Immediate Command Interface (Diagnóstico/Fallback)
#define HDA_REG_ICII        0x60
#define HDA_REG_ICOI        0x64
#define HDA_REG_ICI         0x68
#define HDA_ICI_ICV         (1 << 0)
#define HDA_ICI_IRV         (1 << 1)

#define CORB_RUN_BIT        0x02
#define RIRB_RUN_BIT        0x02
#define RIRB_INT_CTL        0x01

#define HDA_COMMAND_TIMEOUT 100000

// Auxiliares de I/O de Memória
static inline void hda_write8(hda_corb_rirb_t* ctx, uint32_t reg, uint8_t val) {
    *(volatile uint8_t*)(ctx->hw->mmio_virt + reg) = val;
}
static inline uint8_t hda_read8(hda_corb_rirb_t* ctx, uint32_t reg) {
    return *(volatile uint8_t*)(ctx->hw->mmio_virt + reg);
}
static inline void hda_write16(hda_corb_rirb_t* ctx, uint32_t reg, uint16_t val) {
    *(volatile uint16_t*)(ctx->hw->mmio_virt + reg) = val;
}
static inline uint16_t hda_read16(hda_corb_rirb_t* ctx, uint32_t reg) {
    return *(volatile uint16_t*)(ctx->hw->mmio_virt + reg);
}
static inline void hda_write32(hda_corb_rirb_t* ctx, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(ctx->hw->mmio_virt + reg) = val;
}
static inline uint32_t hda_read32(hda_corb_rirb_t* ctx, uint32_t reg) {
    return *(volatile uint32_t*)(ctx->hw->mmio_virt + reg);
}

bool hda_corb_rirb_init(hda_corb_rirb_t* ring_ctx, hda_hardware_context_t* hw_ctx) {
    if (!ring_ctx || !hw_ctx || !hw_ctx->is_ready) return false;
    
    memset(ring_ctx, 0, sizeof(hda_corb_rirb_t));
    ring_ctx->hw = hw_ctx;

    // Alocação de 1 página física (4KB alinhada, cobre os 128-byte alignment da Intel)
    void* block = pmm_alloc_block(); 
    if (!block) return false;

    uintptr_t virt_addr = (uintptr_t)block;
    uint64_t phys_addr = (uint64_t)virt_addr;
    memset((void*)virt_addr, 0, 4096);

    // Estruturação no buffer: CORB no offset 0, RIRB no offset 2048
    ring_ctx->corb_buffer = (uint32_t*)virt_addr;
    ring_ctx->corb_phys_addr = phys_addr;
    ring_ctx->corb_size = 256;

    ring_ctx->rirb_buffer = (hda_rirb_entry_t*)(virt_addr + 2048);
    ring_ctx->rirb_phys_addr = phys_addr + 2048;
    ring_ctx->rirb_size = 256;

    // Parar DMA CORB/RIRB antes de reconfigurar
    hda_write8(ring_ctx, HDA_REG_CORBCTL, 0x00);
    hda_write8(ring_ctx, HDA_REG_RIRBCTL, 0x00);

    uint32_t timeout = HDA_COMMAND_TIMEOUT;
    while ((hda_read8(ring_ctx, HDA_REG_CORBCTL) & CORB_RUN_BIT) || 
           (hda_read8(ring_ctx, HDA_REG_RIRBCTL) & RIRB_RUN_BIT)) {
        if (--timeout == 0) return false;
    }

    // Configurar CORB (Tamanho = 256 entradas, byte 0x02 em CORBSIZE)
    hda_write32(ring_ctx, HDA_REG_CORBLBASE, (uint32_t)(ring_ctx->corb_phys_addr & 0xFFFFFFFF));
    hda_write32(ring_ctx, HDA_REG_CORBUBASE, (uint32_t)(ring_ctx->corb_phys_addr >> 32));
    hda_write8(ring_ctx, HDA_REG_CORBSIZE, 0x02);

    // Reset dos ponteiros do CORB
    hda_write16(ring_ctx, HDA_REG_CORBRP, 0x8000); 
    timeout = HDA_COMMAND_TIMEOUT;
    while ((hda_read16(ring_ctx, HDA_REG_CORBRP) & 0x8000) == 0) {
        if (--timeout == 0) return false;
    }
    hda_write16(ring_ctx, HDA_REG_CORBRP, 0x0000);
    hda_write16(ring_ctx, HDA_REG_CORBWP, 0x0000);

    // Configurar RIRB (Tamanho = 256 entradas, byte 0x02 em RIRBSIZE)
    hda_write32(ring_ctx, HDA_REG_RIRBLBASE, (uint32_t)(ring_ctx->rirb_phys_addr & 0xFFFFFFFF));
    hda_write32(ring_ctx, HDA_REG_RIRBUBASE, (uint32_t)(ring_ctx->rirb_phys_addr >> 32));
    hda_write8(ring_ctx, HDA_REG_RIRBSIZE, 0x02);

    // Reset do ponteiro de escrita do RIRB
    hda_write16(ring_ctx, HDA_REG_RIRBWP, 0x8000);

    ring_ctx->corb_write_pos = 0;
    ring_ctx->rirb_read_pos = 0;

    // Habilitar motores DMA
    hda_write8(ring_ctx, HDA_REG_CORBCTL, CORB_RUN_BIT);
    hda_write8(ring_ctx, HDA_REG_RIRBCTL, RIRB_RUN_BIT | RIRB_INT_CTL);

    ring_ctx->is_initialized = true;
    return true;
}

void hda_corb_rirb_stop(hda_corb_rirb_t* ring_ctx) {
    if (!ring_ctx || !ring_ctx->is_initialized) return;

    hda_write8(ring_ctx, HDA_REG_CORBCTL, 0x00);
    hda_write8(ring_ctx, HDA_REG_RIRBCTL, 0x00);
    ring_ctx->is_initialized = false;
}

bool hda_send_verb(hda_corb_rirb_t* ring_ctx, uint8_t codec_addr, uint32_t verb_data, uint32_t* out_response) {
    if (!ring_ctx || !ring_ctx->is_initialized || !out_response) return false;

    // Formatação: Codec (bits 31:28) + Verbo Payload
    uint32_t corb_entry = ((uint32_t)(codec_addr & 0x0F) << 28) | (verb_data & 0x0FFFFFFF);

    ring_ctx->corb_write_pos = (ring_ctx->corb_write_pos + 1) % ring_ctx->corb_size;
    ring_ctx->corb_buffer[ring_ctx->corb_write_pos] = corb_entry;

    hda_write16(ring_ctx, HDA_REG_CORBWP, ring_ctx->corb_write_pos);

    uint32_t timeout = HDA_COMMAND_TIMEOUT;
    uint16_t rirb_wp = hda_read16(ring_ctx, HDA_REG_RIRBWP);

    while (ring_ctx->rirb_read_pos == rirb_wp) {
        if (--timeout == 0) return false;
        rirb_wp = hda_read16(ring_ctx, HDA_REG_RIRBWP);
    }

    // Avançar antes de ler (compatibilidade mantida com VirtualBox)
    ring_ctx->rirb_read_pos = (ring_ctx->rirb_read_pos + 1) % ring_ctx->rirb_size;
    hda_rirb_entry_t rirb_entry = ring_ctx->rirb_buffer[ring_ctx->rirb_read_pos];

    hda_write8(ring_ctx, HDA_REG_RIRBST, 0x01); // Limpar resposta processada

    *out_response = rirb_entry.response;
    return true;
}

uint32_t hda_send_verb_immediate(hda_corb_rirb_t* ring_ctx, uint8_t codec_addr, uint32_t verb_data) {
    if (!ring_ctx || !ring_ctx->hw) return 0xFFFFFFFF;

    uint32_t cmd = ((uint32_t)(codec_addr & 0x0F) << 28) | (verb_data & 0x0FFFFFFF);
    uint32_t timeout = HDA_COMMAND_TIMEOUT;

    while ((hda_read16(ring_ctx, HDA_REG_ICI) & HDA_ICI_ICV) && --timeout);
    if (timeout == 0) return 0xFFFFFFFF;

    hda_write32(ring_ctx, HDA_REG_ICII, cmd);
    hda_write16(ring_ctx, HDA_REG_ICI, HDA_ICI_ICV);

    timeout = HDA_COMMAND_TIMEOUT;
    while (!(hda_read16(ring_ctx, HDA_REG_ICI) & HDA_ICI_IRV) && --timeout);
    if (timeout == 0) return 0xFFFFFFFF;

    uint32_t resp = hda_read32(ring_ctx, HDA_REG_ICOI);
    hda_write16(ring_ctx, HDA_REG_ICI, HDA_ICI_IRV);
    return resp;
}
