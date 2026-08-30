/* ============================================================================
 * ARCHITECTURE: Ring 0 (Kernel Driver)
 * FILE: drivers/net/net_pci.c
 * DESCRIPTION: Mapeamento e controle do barramento PCI para dispositivos de Rede
 * ============================================================================ */

#include "net_pci.h"
#include "drivers/io.h"

// Protótipos das funções I/O (inl / outl) do Kernel
extern uint32_t inl(uint16_t port);
extern void outl(uint16_t port, uint32_t data);

// ============================================================================
// LEITURA E ESCRITA BÁSICA PCI
// ============================================================================
uint32_t net_pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));

    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void net_pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));

    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

bool net_pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t vendor = net_pci_read_config(bus, slot, func, PCI_VENDOR_ID_OFFSET);
    return (vendor & 0xFFFF) != 0xFFFF;
}

// ============================================================================
// HELPER: PREENCHIMENTO DE ESTRUTURA
// ============================================================================
static void populate_device_info(net_pci_device_t* dev, uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t reg00 = net_pci_read_config(bus, slot, func, PCI_VENDOR_ID_OFFSET);
    uint32_t reg08 = net_pci_read_config(bus, slot, func, PCI_CLASS_OFFSET);
    uint32_t reg3C = net_pci_read_config(bus, slot, func, PCI_INTERRUPT_LINE);

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    
    dev->vendor_id = reg00 & 0xFFFF;
    dev->device_id = reg00 >> 16;
    
    dev->class_code = reg08 >> 24;
    dev->subclass_code = (reg08 >> 16) & 0xFF;
    dev->prog_if = (reg08 >> 8) & 0xFF;

    dev->bar0 = net_pci_read_config(bus, slot, func, PCI_BAR0_OFFSET);
    dev->bar1 = net_pci_read_config(bus, slot, func, PCI_BAR1_OFFSET);
    dev->bar2 = net_pci_read_config(bus, slot, func, PCI_BAR2_OFFSET);
    dev->bar3 = net_pci_read_config(bus, slot, func, PCI_BAR3_OFFSET);
    dev->bar4 = net_pci_read_config(bus, slot, func, PCI_BAR4_OFFSET);
    dev->bar5 = net_pci_read_config(bus, slot, func, PCI_BAR5_OFFSET);

    dev->irq_line = reg3C & 0xFF;
    dev->irq_pin = (reg3C >> 8) & 0xFF;
    
    dev->found = true;
    dev->enabled = false;
}

// ============================================================================
// FUNÇÕES DE BUSCA
// ============================================================================
net_pci_device_t net_pci_find_device(uint8_t class_code, uint8_t subclass_code) {
    net_pci_device_t dev = {0}; // Zera lixo de memória
    dev.found = false;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                if (!net_pci_device_exists(bus, slot, func)) continue;

                uint32_t reg08 = net_pci_read_config(bus, slot, func, PCI_CLASS_OFFSET);
                if ((reg08 >> 24) == class_code && ((reg08 >> 16) & 0xFF) == subclass_code) {
                    populate_device_info(&dev, bus, slot, func);
                    return dev;
                }
            }
        }
    }
    return dev;
}

net_pci_device_t net_pci_find_device_by_id(uint16_t vendor_id, uint16_t device_id) {
    net_pci_device_t dev = {0};
    dev.found = false;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                if (!net_pci_device_exists(bus, slot, func)) continue;

                uint32_t reg00 = net_pci_read_config(bus, slot, func, PCI_VENDOR_ID_OFFSET);
                if ((reg00 & 0xFFFF) == vendor_id && (reg00 >> 16) == device_id) {
                    populate_device_info(&dev, bus, slot, func);
                    return dev;
                }
            }
        }
    }
    return dev;
}

net_pci_device_t net_pci_find_device_by_class(uint8_t class_code) {
    net_pci_device_t dev = {0};
    dev.found = false;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                if (!net_pci_device_exists(bus, slot, func)) continue;

                uint32_t reg08 = net_pci_read_config(bus, slot, func, PCI_CLASS_OFFSET);
                if ((reg08 >> 24) == class_code) {
                    populate_device_info(&dev, bus, slot, func);
                    return dev;
                }
            }
        }
    }
    return dev;
}

// ============================================================================
// LEITURAS DE INFORMAÇÕES ESPECÍFICAS
// ============================================================================
uint32_t net_pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    if (bar_index > 5) return 0;
    return net_pci_read_config(bus, slot, func, PCI_BAR0_OFFSET + (bar_index * 4));
}

uint64_t net_pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    if (bar_index > 4) return 0; // Um BAR de 64 bits ocupa 2 posições

    uint32_t low = net_pci_read_bar(bus, slot, func, bar_index);
    if ((low & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_MMIO && (low & PCI_BAR_64BIT_MASK)) {
        uint32_t high = net_pci_read_bar(bus, slot, func, bar_index + 1);
        return ((uint64_t)high << 32) | (low & PCI_BAR_MMIO_MASK);
    }
    return (uint64_t)(low & ((low & PCI_BAR_TYPE_MASK) ? PCI_BAR_IO_MASK : PCI_BAR_MMIO_MASK));
}

uint8_t net_pci_read_irq(uint8_t bus, uint8_t slot, uint8_t func) {
    return net_pci_read_config(bus, slot, func, PCI_INTERRUPT_LINE) & 0xFF;
}

uint32_t net_pci_read_class(uint8_t bus, uint8_t slot, uint8_t func) {
    return net_pci_read_config(bus, slot, func, PCI_CLASS_OFFSET) >> 8;
}

uint32_t net_pci_get_bar_type(uint32_t bar) {
    return (bar & PCI_BAR_TYPE_MASK);
}

// ============================================================================
// CONFIGURAÇÃO DO DISPOSITIVO
// ============================================================================
bool net_pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = net_pci_read_config(bus, slot, func, PCI_COMMAND_OFFSET);
    cmd |= PCI_COMMAND_MEM_SPACE | PCI_COMMAND_BUS_MASTER;
    net_pci_write_config(bus, slot, func, PCI_COMMAND_OFFSET, cmd);
    return true;
}

void net_pci_disable_bus_master(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = net_pci_read_config(bus, slot, func, PCI_COMMAND_OFFSET);
    cmd &= ~PCI_COMMAND_BUS_MASTER;
    net_pci_write_config(bus, slot, func, PCI_COMMAND_OFFSET, cmd);
}

bool net_pci_enable_device(net_pci_device_t *dev) {
    if (!dev || !dev->found) return false;
    net_pci_enable_bus_master(dev->bus, dev->slot, dev->func);
    dev->enabled = true;
    return true;
}

void net_pci_disable_device(net_pci_device_t *dev) {
    if (!dev || !dev->found) return;
    net_pci_disable_bus_master(dev->bus, dev->slot, dev->func);
    dev->enabled = false;
}

bool net_pci_set_power_state(uint8_t bus, uint8_t slot, uint8_t func, uint8_t state) {
    // Stub: Configuração avançada de ACPI/PCIe capabilites pode ser adicionada no futuro
    (void)bus; (void)slot; (void)func; (void)state;
    return true; 
}

void net_pci_dump_device_info(net_pci_device_t *dev) {
    // Stub: Para não depender de printf/vga_print no core do PCI.
    // Pode expandir para injetar logs detalhados posteriormente.
    (void)dev;
}
