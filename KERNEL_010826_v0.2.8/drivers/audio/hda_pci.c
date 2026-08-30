/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: hda_pci.c
Versão: 1.1
Data: 26/08/2026
Autor: LBF-OS Team

Mudanças v1.1:
    - Adicionado sufixo _hda nas funções PCI
    - Isola o driver Intel HDA do subsistema PCI do AC97
    - Evita conflitos de linkagem (múltiplas definições)
    - Driver mantido independente para testes
====================================================================
*/

#include "hda_pci.h"
#include "util/sysutils.h"
#include "util/string.h"
#include "drivers/proc.h"

#define TIMEOUT_LOOPS 100000

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// Operações de I/O PCI
static inline void outl_hda(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl_hda(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t pci_read_dword_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl_hda(PCI_CONFIG_ADDRESS, address);
    return inl_hda(PCI_CONFIG_DATA);
}

void pci_write_dword_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl_hda(PCI_CONFIG_ADDRESS, address);
    outl_hda(PCI_CONFIG_DATA, value);
}

uint16_t pci_read_word_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword_hda(bus, slot, func, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_byte_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword_hda(bus, slot, func, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_word_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl_hda(PCI_CONFIG_ADDRESS, address);
    
    uint32_t dword = inl_hda(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)value << shift);
    
    outl_hda(PCI_CONFIG_ADDRESS, address);
    outl_hda(PCI_CONFIG_DATA, dword);
}

// Auxiliares MMIO
static inline uint16_t mmio_read16(uintptr_t base, uint32_t offset) {
    return *(volatile uint16_t*)(base + offset);
}

static inline void mmio_write16(uintptr_t base, uint32_t offset, uint16_t value) {
    *(volatile uint16_t*)(base + offset) = value;
}

static inline uint32_t mmio_read32(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

static inline void mmio_write32(uintptr_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(base + offset) = value;
}

bool hda_pci_init(hda_hardware_context_t* context) {
    if (!context) return false;
    memset(context, 0, sizeof(hda_hardware_context_t));

    // 1. Descoberta do Controlador HDA no Barramento PCI (Class 0x04, Subclass 0x03)
    bool found = false;
    for (uint16_t bus = 0; bus < 256 && !found; bus++) {
        for (uint8_t slot = 0; slot < 32 && !found; slot++) {
            if (pci_read_word_hda((uint8_t)bus, slot, 0, PCI_VENDOR_ID) == 0xFFFF) continue;
            
            uint8_t header_type = pci_read_byte_hda((uint8_t)bus, slot, 0, PCI_HEADER_TYPE);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func && !found; func++) {
                if (pci_read_byte_hda((uint8_t)bus, slot, func, PCI_BASE_CLASS) == 0x04 && 
                    pci_read_byte_hda((uint8_t)bus, slot, func, PCI_SUBCLASS) == 0x03) {
                    context->bus = (uint8_t)bus;
                    context->slot = slot;
                    context->function = func;
                    found = true;
                }
            }
        }
    }

    if (!found) return false;

    // 2. Habilitação de Acesso
    uint16_t cmd = pci_read_word_hda(context->bus, context->slot, context->function, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;   // v1.2: SEM PCI_CMD_INT_DISABLE
    pci_write_word_hda(context->bus, context->slot, context->function, PCI_COMMAND, cmd);

    // 3. Leitura e Mapeamento BAR0
    uint32_t bar0_low_orig = pci_read_dword_hda(context->bus, context->slot, context->function, PCI_BAR0);
    uint32_t bar0_high_orig = pci_read_dword_hda(context->bus, context->slot, context->function, PCI_BAR0 + 4);
    bool is_64bit_bar = ((bar0_low_orig & 0x06) == 0x04);

    pci_write_dword_hda(context->bus, context->slot, context->function, PCI_BAR0, 0xFFFFFFFF);
    if (is_64bit_bar) {
        pci_write_dword_hda(context->bus, context->slot, context->function, PCI_BAR0 + 4, 0xFFFFFFFF);
    }

    uint32_t bar0_low_size = pci_read_dword_hda(context->bus, context->slot, context->function, PCI_BAR0);
    uint32_t bar0_high_size = is_64bit_bar ? pci_read_dword_hda(context->bus, context->slot, context->function, PCI_BAR0 + 4) : 0;

    pci_write_dword_hda(context->bus, context->slot, context->function, PCI_BAR0, bar0_low_orig);
    if (is_64bit_bar) {
        pci_write_dword_hda(context->bus, context->slot, context->function, PCI_BAR0 + 4, bar0_high_orig);
    }

    context->mmio_phys = bar0_low_orig & ~0x0F;
    if (is_64bit_bar) {
        context->mmio_phys |= ((uint64_t)bar0_high_orig << 32);
        uint64_t size_mask = ((uint64_t)bar0_high_size << 32) | (bar0_low_size & ~0x0F);
        context->mmio_size = (uint32_t)(~size_mask + 1);
    } else {
        context->mmio_size = ~(bar0_low_size & ~0x0F) + 1;
    }

    if (context->mmio_phys == 0 || context->mmio_phys == 0xFFFFFFFF) {
        return false;
    }

    context->mmio_virt = (uintptr_t)context->mmio_phys;

    // 4. Executar Global Reset do Controlador (CRST Bit 0)
    uint32_t gctl = mmio_read32(context->mmio_virt, HDA_REG_GCTL);
    mmio_write32(context->mmio_virt, HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);
    
    int timeout = TIMEOUT_LOOPS;
    while ((mmio_read32(context->mmio_virt, HDA_REG_GCTL) & HDA_GCTL_CRST) && --timeout > 0);
    if (timeout <= 0) return false;

    sys_sleep(10); // Pausa recomendada pela especificação Intel HDA

    mmio_write32(context->mmio_virt, HDA_REG_GCTL, mmio_read32(context->mmio_virt, HDA_REG_GCTL) | HDA_GCTL_CRST);

    timeout = TIMEOUT_LOOPS;
    while (!(mmio_read32(context->mmio_virt, HDA_REG_GCTL) & HDA_GCTL_CRST) && --timeout > 0);
    if (timeout <= 0) return false;

    sys_sleep(10);

    // 5. Ler Máscara de Codecs Ativos e Capacidades do Hardware
    uint16_t statests = mmio_read16(context->mmio_virt, HDA_REG_STATESTS);
    context->codec_mask = statests & 0x7FFF;
    mmio_write16(context->mmio_virt, HDA_REG_STATESTS, statests);

    uint16_t gcap = mmio_read16(context->mmio_virt, HDA_REG_GCAP);
    context->supports_64bit     = (gcap & (1 << 0)) != 0;
    context->num_sdo            = ((gcap >> 1) & 0x03) + 1;
    context->num_bidir_streams  = (gcap >> 3) & 0x1F;
    context->num_input_streams  = (gcap >> 8) & 0x0F;
    context->num_output_streams = (gcap >> 12) & 0x0F;

    context->irq_line = pci_read_byte_hda(context->bus, context->slot, context->function, PCI_INTERRUPT_LINE);
    context->using_msi = false;

    context->is_ready = true;
    return true;
}
