/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: hda_pci.h
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

#ifndef HDA_PCI_H
#define HDA_PCI_H

#include <stdint.h>
#include <stdbool.h>

// Registradores PCI Config Space
#define PCI_VENDOR_ID          0x00
#define PCI_DEVICE_ID          0x02
#define PCI_COMMAND            0x04
#define PCI_STATUS             0x06
#define PCI_REVISION_ID        0x08
#define PCI_SUBCLASS           0x0A
#define PCI_BASE_CLASS         0x0B
#define PCI_HEADER_TYPE        0x0E
#define PCI_BAR0               0x10
#define PCI_INTERRUPT_LINE     0x3C

// Bits do PCI Command
#define PCI_CMD_MEMORY_SPACE   (1 << 1)
#define PCI_CMD_BUS_MASTER     (1 << 2)
#define PCI_CMD_INT_DISABLE    (1 << 10)

// Registradores Globais HDA
#define HDA_REG_GCAP           0x00
#define HDA_REG_GCTL           0x08
#define HDA_REG_STATESTS       0x0E

#define HDA_GCTL_CRST          (1 << 0)

// Estrutura de Contexto do Hardware Base
typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  function;
    
    uint64_t mmio_phys;
    uintptr_t mmio_virt;
    uint32_t mmio_size;
    
    uint8_t  num_input_streams;
    uint8_t  num_output_streams;
    uint8_t  num_bidir_streams;
    uint8_t  num_sdo;
    bool     supports_64bit;
    
    uint16_t codec_mask;

    uint8_t  irq_line;
    bool     using_msi;

    bool     is_ready;
} hda_hardware_context_t;

// API do Submódulo PCI HDA
uint32_t pci_read_dword_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_word_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_byte_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void     pci_write_dword_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void     pci_write_word_hda(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

bool hda_pci_init(hda_hardware_context_t* context);

#endif // HDA_PCI_H
