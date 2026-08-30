#ifndef NET_PCI_H
#define NET_PCI_H

#include <stdint.h>
#include <stdbool.h>

// Definições de constantes PCI
#define PCI_CONFIG_ADDR         0xCF8
#define PCI_CONFIG_DATA         0xCFC

// Offsets do cabeçalho PCI
#define PCI_VENDOR_ID_OFFSET    0x00
#define PCI_COMMAND_OFFSET      0x04
#define PCI_STATUS_OFFSET       0x06
#define PCI_CLASS_OFFSET        0x08
#define PCI_BAR0_OFFSET         0x10
#define PCI_BAR1_OFFSET         0x14
#define PCI_BAR2_OFFSET         0x18
#define PCI_BAR3_OFFSET         0x1C
#define PCI_BAR4_OFFSET         0x20
#define PCI_BAR5_OFFSET         0x24
#define PCI_INTERRUPT_LINE      0x3C
#define PCI_INTERRUPT_PIN       0x3D

// Bits do registrador de comando
#define PCI_COMMAND_IO_SPACE    (1 << 0)
#define PCI_COMMAND_MEM_SPACE   (1 << 1)
#define PCI_COMMAND_BUS_MASTER  (1 << 2)
#define PCI_COMMAND_INT_DISABLE (1 << 10)

// Máscaras para BAR
#define PCI_BAR_IO_MASK         0xFFFFFFFC
#define PCI_BAR_MMIO_MASK       0xFFFFFFF0
#define PCI_BAR_TYPE_MASK       0x1
#define PCI_BAR_TYPE_IO         0x1
#define PCI_BAR_TYPE_MMIO       0x0
#define PCI_BAR_64BIT_MASK      0x4

// Estrutura do dispositivo PCI
typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint8_t irq_line;
    uint8_t irq_pin;
    bool found;
    bool enabled;
} net_pci_device_t;

// Funções principais de acesso à configuração PCI
uint32_t net_pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void net_pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
bool net_pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func);

// Funções de busca de dispositivos
net_pci_device_t net_pci_find_device(uint8_t class_code, uint8_t subclass_code);
net_pci_device_t net_pci_find_device_by_id(uint16_t vendor_id, uint16_t device_id);
net_pci_device_t net_pci_find_device_by_class(uint8_t class_code);

// Funções para ler informações específicas
uint32_t net_pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index);
uint64_t net_pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index);
uint8_t net_pci_read_irq(uint8_t bus, uint8_t slot, uint8_t func);
uint32_t net_pci_read_class(uint8_t bus, uint8_t slot, uint8_t func);

// Funções de configuração do dispositivo
bool net_pci_enable_device(net_pci_device_t *dev);
void net_pci_disable_device(net_pci_device_t *dev);
bool net_pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func);
void net_pci_disable_bus_master(uint8_t bus, uint8_t slot, uint8_t func);
bool net_pci_set_power_state(uint8_t bus, uint8_t slot, uint8_t func, uint8_t state);

// Funções utilitárias
void net_pci_dump_device_info(net_pci_device_t *dev);
uint32_t net_pci_get_bar_type(uint32_t bar);

#endif // NET_PCI_H
