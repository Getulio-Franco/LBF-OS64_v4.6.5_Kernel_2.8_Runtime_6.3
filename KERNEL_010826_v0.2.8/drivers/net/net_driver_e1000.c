/* ============================================================================
 * ARCHITECTURE: Ring 0 (Kernel Driver Implementation)
 * FILE: drivers/net/net_driver_e1000.c
 * DESCRIPTION: Driver físico otimizado para Intel E1000 / 82540EM
 * ============================================================================ */

#include "net_driver_e1000.h"
#include "drivers/video.h"
#include "drivers/net/net_pci.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ESTADO GLOBAL E BUFFERS DMA (Alinhados a 4096 bytes)
 * ============================================================================ */

static e1000_driver_state_t g_e1000_state;

static e1000_rx_desc_t rx_ring[E1000_NUM_RX_DESC] __attribute__((aligned(4096)));
static e1000_tx_desc_t tx_ring[E1000_NUM_TX_DESC] __attribute__((aligned(4096)));

static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_PACKET_SIZE] __attribute__((aligned(4096)));
static uint8_t tx_buffers[E1000_NUM_TX_DESC][E1000_PACKET_SIZE] __attribute__((aligned(4096)));

/* ============================================================================
 * PROTÓTIPOS INTERNOS
 * ============================================================================ */

static inline uint64_t e1000_virt_to_phys(void *addr);
static inline void     e1000_mmio_write32(uint32_t base, uint32_t reg, uint32_t value);
static inline uint32_t e1000_mmio_read32(uint32_t base, uint32_t reg);
static void            e1000_delay(volatile uint32_t count);
static uint16_t        e1000_read_eeprom(uint32_t base, uint8_t address);
static void            e1000_write_mac_address(uint32_t base, const uint8_t mac[6]);
static void            e1000_clear_multicast_table(void);
static void            e1000_init_rx(void);
static void            e1000_init_tx(void);
static void            e1000_disable_rx_tx(void);
static void            e1000_enable_rx_tx(void);
static void            e1000_clear_interrupts(void);

/* ============================================================================
 * FUNÇÕES AUXILIARES DE HARDWARE & MMIO
 * ============================================================================ */

static inline uint64_t e1000_virt_to_phys(void *addr)
{
    return (uint64_t)(uintptr_t)addr;
}

static inline void e1000_mmio_write32(uint32_t base, uint32_t reg, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)(base + reg);
    *ptr = value;
    asm volatile("mfence" ::: "memory");
}

static inline uint32_t e1000_mmio_read32(uint32_t base, uint32_t reg)
{
    volatile uint32_t *ptr = (volatile uint32_t *)(uintptr_t)(base + reg);
    uint32_t value = *ptr;
    asm volatile("lfence" ::: "memory");
    return value;
}

static void e1000_delay(volatile uint32_t count)
{
    while (count--)
    {
        asm volatile("" ::: "memory");
    }
}

/* ============================================================================
 * EEPROM & MAC
 * ============================================================================ */

static uint16_t e1000_read_eeprom(uint32_t base, uint8_t address)
{
    uint32_t value;

    e1000_mmio_write32(base, E1000_EERD, 1U | ((uint32_t)address << 8));

    for (int timeout = 0; timeout < 100000; timeout++)
    {
        value = e1000_mmio_read32(base, E1000_EERD);
        if (value & (1U << 4))
        {
            return (uint16_t)(value >> 16);
        }
        e1000_delay(10);
    }

    return 0xFFFF;
}

static void e1000_write_mac_address(uint32_t base, const uint8_t mac[6])
{
    uint32_t ral = ((uint32_t)mac[0])       |
                   ((uint32_t)mac[1] << 8)  |
                   ((uint32_t)mac[2] << 16) |
                   ((uint32_t)mac[3] << 24);

    uint32_t rah = ((uint32_t)mac[4])       |
                   ((uint32_t)mac[5] << 8)  |
                   (1U << 31);

    e1000_mmio_write32(base, E1000_RA, ral);
    e1000_mmio_write32(base, E1000_RA + 4, rah);
}

static void e1000_clear_multicast_table(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    for (int i = 0; i < 128; i++)
    {
        e1000_mmio_write32(base, E1000_MTA + ((uint32_t)i * 4), 0);
    }
}

/* ============================================================================
 * CONTROLE DE FLUXO RX/TX & INTERRUPÇÕES
 * ============================================================================ */

static void e1000_disable_rx_tx(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    uint32_t rctl = e1000_mmio_read32(base, E1000_RCTL);
    uint32_t tctl = e1000_mmio_read32(base, E1000_TCTL);

    rctl &= ~RCTL_EN;
    tctl &= ~TCTL_EN;

    e1000_mmio_write32(base, E1000_RCTL, rctl);
    e1000_mmio_write32(base, E1000_TCTL, tctl);
    e1000_delay(100000);
}

static void e1000_enable_rx_tx(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    uint32_t rctl = e1000_mmio_read32(base, E1000_RCTL);
    uint32_t tctl = e1000_mmio_read32(base, E1000_TCTL);

    rctl |= RCTL_EN;
    tctl |= TCTL_EN;

    e1000_mmio_write32(base, E1000_RCTL, rctl);
    e1000_mmio_write32(base, E1000_TCTL, tctl);
}

static void e1000_clear_interrupts(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    e1000_mmio_write32(base, E1000_IMC, 0xFFFFFFFF);
    (void)e1000_mmio_read32(base, E1000_ICR);
}

/* ============================================================================
 * INICIALIZAÇÃO DOS ANÉIS RX E TX
 * ============================================================================ */

/*static void e1000_init_rx(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    uint32_t rctl = e1000_mmio_read32(base, E1000_RCTL);

    rctl &= ~RCTL_EN;
    e1000_mmio_write32(base, E1000_RCTL, rctl);
    e1000_delay(10000);

    for (int i = 0; i < E1000_NUM_RX_DESC; i++)
    {
        rx_ring[i].addr     = e1000_virt_to_phys(&rx_buffers[i][0]);
        rx_ring[i].length   = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status   = 0;
        rx_ring[i].errors   = 0;
        rx_ring[i].special  = 0;
    }

    g_e1000_state.rx_descs = rx_ring;
    g_e1000_state.rx_cur   = 0;

    uint64_t ring_phys = e1000_virt_to_phys(rx_ring);

    e1000_mmio_write32(base, E1000_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFULL));
    e1000_mmio_write32(base, E1000_RDBAH, (uint32_t)(ring_phys >> 32));
    e1000_mmio_write32(base, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_mmio_write32(base, E1000_RDH, 0);
    e1000_mmio_write32(base, E1000_RDT, E1000_NUM_RX_DESC - 1);

    e1000_clear_multicast_table();
    e1000_write_mac_address(base, g_e1000_state.mac_address);

    rctl = RCTL_EN | RCTL_BAM | (1U << 3) | (1U << 4) | RCTL_SECRC;
    rctl &= ~E1000_RCTL_LBM_MASK;

    e1000_mmio_write32(base, E1000_RCTL, rctl);
}*/

/*static void e1000_init_rx(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    
    // 1. Desabilita a recepção antes de mexer nos descritores e registradores
    uint32_t rctl = e1000_mmio_read32(base, E1000_RCTL);
    rctl &= ~RCTL_EN;
    e1000_mmio_write32(base, E1000_RCTL, rctl);
    e1000_delay(10000);

    // 2. Prepara os descritores do anel RX
    for (int i = 0; i < E1000_NUM_RX_DESC; i++)
    {
        rx_ring[i].addr     = e1000_virt_to_phys(&rx_buffers[i][0]);
        rx_ring[i].length   = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status   = 0; // 0 significa livre para a placa preencher
        rx_ring[i].errors   = 0;
        rx_ring[i].special  = 0;
    }

    g_e1000_state.rx_descs = rx_ring;
    g_e1000_state.rx_cur   = 0;

    uint64_t ring_phys = e1000_virt_to_phys(rx_ring);

    // 3. Configura o endereço físico e o tamanho do anel
    e1000_mmio_write32(base, E1000_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFULL));
    e1000_mmio_write32(base, E1000_RDBAH, (uint32_t)(ring_phys >> 32));
    e1000_mmio_write32(base, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));

    // 4. Zera o Head e aponta o Tail para o último descritor do anel
    e1000_mmio_write32(base, E1000_RDH, 0);
    e1000_mmio_write32(base, E1000_RDT, E1000_NUM_RX_DESC - 1);

    e1000_clear_multicast_table();
    e1000_write_mac_address(base, g_e1000_state.mac_address);

    // 5. Configura o RCTL por último com as flags corretas (incluindo BAM para Broadcast/DHCP)
    rctl = RCTL_EN | RCTL_BAM | (1U << 3) | (1U << 4) | RCTL_SECRC;
    rctl &= ~E1000_RCTL_LBM_MASK;

    e1000_mmio_write32(base, E1000_RCTL, rctl);
}*/

// drivers/net/net_driver_e1000.c
static void e1000_init_rx(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    
    // 1. Desabilita a recepção antes de mexer nos descritores e registradores
    uint32_t rctl = e1000_mmio_read32(base, E1000_RCTL);
    rctl &= ~RCTL_EN;
    e1000_mmio_write32(base, E1000_RCTL, rctl);
    e1000_delay(10000);

    // 2. Prepara os descritores do anel RX
    for (int i = 0; i < E1000_NUM_RX_DESC; i++)
    {
        rx_ring[i].addr     = e1000_virt_to_phys(&rx_buffers[i][0]);
        rx_ring[i].length   = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status   = 0; 
        rx_ring[i].errors   = 0;
        rx_ring[i].special  = 0;
    }
    g_e1000_state.rx_descs = rx_ring;
    g_e1000_state.rx_cur   = 0;

    uint64_t ring_phys = e1000_virt_to_phys(rx_ring);

    // 3. Configura o endereço físico e o tamanho do anel
    e1000_mmio_write32(base, E1000_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFULL));
    e1000_mmio_write32(base, E1000_RDBAH, (uint32_t)(ring_phys >> 32));
    e1000_mmio_write32(base, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));

    // 4. Zera o Head e aponta o Tail para o último descritor do anel
    e1000_mmio_write32(base, E1000_RDH, 0);
    e1000_mmio_write32(base, E1000_RDT, E1000_NUM_RX_DESC - 1);

    e1000_clear_multicast_table();
    e1000_write_mac_address(base, g_e1000_state.mac_address);

    // 5. Configura o RCTL por último com as flags corretas
    // RCTL_EN (Enable) | RCTL_BAM (Broadcast) | RCTL_UPE (Unicast Promisc) | RCTL_MPE (Multicast Promisc)
    // RCTL_SZ_2048 (Buffer Size 2048 - bits 16-17 = 00) | RCTL_BSEX = 0 (bit 25)
    // RCTL_SECRC (Strip CRC)
    rctl = RCTL_EN | RCTL_BAM | RCTL_UPE | RCTL_MPE | RCTL_SECRC;
    
    // Força o tamanho do buffer para 2048 bytes (limpa os bits 16, 17 e 25)
    rctl &= ~((1U << 25) | (3U << 16)); 
    rctl |= (0U << 16); // 00 = 2048 bytes
    
    // Limpa o Loopback Mode
    rctl &= ~E1000_RCTL_LBM_MASK;
    
    e1000_mmio_write32(base, E1000_RCTL, rctl);
}

static void e1000_init_tx(void)
{
    uint32_t base = g_e1000_state.mmio_base;

    for (int i = 0; i < E1000_NUM_TX_DESC; i++)
    {
        tx_ring[i].addr    = e1000_virt_to_phys(&tx_buffers[i][0]);
        tx_ring[i].length  = 0;
        tx_ring[i].cso     = 0;
        tx_ring[i].status  = E1000_TXD_STAT_DD;
        tx_ring[i].css     = 0;
        tx_ring[i].special = 0;
        tx_ring[i].cmd     = 0;
    }

    g_e1000_state.tx_descs = tx_ring;
    g_e1000_state.tx_cur   = 0;

    uint64_t ring_phys = e1000_virt_to_phys(tx_ring);

    e1000_mmio_write32(base, E1000_TDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFULL));
    e1000_mmio_write32(base, E1000_TDBAH, (uint32_t)(ring_phys >> 32));
    e1000_mmio_write32(base, E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_mmio_write32(base, E1000_TDH, 0);
    e1000_mmio_write32(base, E1000_TDT, 0);

    uint32_t tctl = (1U << 1) | (1U << 3) | (1U << 24) | (0x0F << 4) | (0x3F << 12);
    e1000_mmio_write32(base, E1000_TCTL, tctl);

    uint32_t tipg = 10 | (10 << 10) | (10 << 20);
    e1000_mmio_write32(base, E1000_TIPG, tipg);
}

/* ============================================================================
 * ROTINAS PÚBLICAS DO DRIVER
 * ============================================================================ */

int e1000_driver_init(uint32_t mmio_base, uint8_t irq_line)
{
    g_e1000_state.mmio_base = mmio_base;
    g_e1000_state.irq_line  = irq_line;
    g_e1000_state.rx_descs  = rx_ring;
    g_e1000_state.tx_descs  = tx_ring;
    g_e1000_state.rx_cur    = 0;
    g_e1000_state.tx_cur    = 0;

    e1000_clear_interrupts();

    uint32_t ctrl = e1000_mmio_read32(mmio_base, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    e1000_mmio_write32(mmio_base, E1000_CTRL, ctrl);

    uint16_t m0 = e1000_read_eeprom(mmio_base, 0);
    uint16_t m1 = e1000_read_eeprom(mmio_base, 1);
    uint16_t m2 = e1000_read_eeprom(mmio_base, 2);

    g_e1000_state.mac_address[0] = (uint8_t)(m0 & 0xFF);
    g_e1000_state.mac_address[1] = (uint8_t)(m0 >> 8);
    g_e1000_state.mac_address[2] = (uint8_t)(m1 & 0xFF);
    g_e1000_state.mac_address[3] = (uint8_t)(m1 >> 8);
    g_e1000_state.mac_address[4] = (uint8_t)(m2 & 0xFF);
    g_e1000_state.mac_address[5] = (uint8_t)(m2 >> 8);

    e1000_write_mac_address(mmio_base, g_e1000_state.mac_address);
    e1000_clear_multicast_table();

    e1000_init_rx();
    e1000_init_tx();
    e1000_enable_rx_tx();

    e1000_mmio_write32(mmio_base, E1000_IMS, (1U << 7) | (1U << 0) | (1U << 2));
    g_e1000_state.initialized = true;
    return 0;
}

int e1000_send_packet(const void *data, uint16_t len)
{
    if (!data || len == 0 || len > E1000_PACKET_SIZE)
    {
        return -1;
    }

    uint16_t cur = g_e1000_state.tx_cur;
    e1000_tx_desc_t *desc = &g_e1000_state.tx_descs[cur];

    if (!(desc->status & E1000_TXD_STAT_DD))
    {
        return -2;
    }

    const uint8_t *src = (const uint8_t *)data;
    for (uint16_t i = 0; i < len; i++)
    {
        tx_buffers[cur][i] = src[i];
    }

    desc->length  = len;
    desc->cso     = 0;
    desc->css     = 0;
    desc->special = 0;
    desc->cmd     = (1U << 0) | (1U << 1) | (1U << 3); // EOP | IFCS | RS
    desc->status  = 0;

    asm volatile("sfence" ::: "memory");

    uint16_t next = (uint16_t)((cur + 1) % E1000_NUM_TX_DESC);
    g_e1000_state.tx_cur = next;

    e1000_mmio_write32(g_e1000_state.mmio_base, E1000_TDT, next);
    return 0;
}

int e1000_receive_packet(void *out_buffer, uint16_t *out_len)
{
    if (!out_buffer || !out_len)
    {
        return -1;
    }

    uint16_t cur = g_e1000_state.rx_cur;
    e1000_rx_desc_t *desc = &g_e1000_state.rx_descs[cur];

    if (!(desc->status & (1U << 0))) // DD status
    {
        return -1;
    }

    uint16_t len = desc->length;
    if (len == 0 || len > E1000_PACKET_SIZE)
    {
        desc->status = 0;
        desc->length = 0;
        e1000_mmio_write32(g_e1000_state.mmio_base, E1000_RDT, cur);
        g_e1000_state.rx_cur = (uint16_t)((cur + 1) % E1000_NUM_RX_DESC);
        return -3;
    }

    uint8_t *dest = (uint8_t *)out_buffer;
    for (uint16_t i = 0; i < len; i++)
    {
        dest[i] = rx_buffers[cur][i];
    }

    *out_len = len;

    desc->status   = 0;
    desc->length   = 0;
    desc->checksum = 0;
    desc->errors   = 0;
    desc->special  = 0;

    asm volatile("sfence" ::: "memory");

    e1000_mmio_write32(g_e1000_state.mmio_base, E1000_RDT, cur);
    g_e1000_state.rx_cur = (uint16_t)((cur + 1) % E1000_NUM_RX_DESC);

    return 0;
}

void e1000_get_mac_address(uint8_t out_mac[6])
{
    if (!out_mac) return;
    for (int i = 0; i < 6; i++)
    {
        out_mac[i] = g_e1000_state.mac_address[i];
    }
}

void e1000_irq_handler(void)
{
    uint32_t base = g_e1000_state.mmio_base;
    (void)e1000_mmio_read32(base, E1000_ICR);
}

void e1000_hard_reset(void)
{
    e1000_clear_interrupts();
    e1000_disable_rx_tx();

    uint32_t base = g_e1000_state.mmio_base;
    uint32_t ctrl = e1000_mmio_read32(base, E1000_CTRL);
    ctrl |= E1000_CTRL_RST;
    e1000_mmio_write32(base, E1000_CTRL, ctrl);
    e1000_delay(1000000);

    ctrl = e1000_mmio_read32(base, E1000_CTRL) | E1000_CTRL_SLU;
    e1000_mmio_write32(base, E1000_CTRL, ctrl);

    e1000_write_mac_address(base, g_e1000_state.mac_address);
    e1000_init_rx();
    e1000_init_tx();
    e1000_enable_rx_tx();
}

void e1000_validate_ring0(void)
{
    net_pci_device_t dev = net_pci_find_device_by_id(0x8086, 0x100E);
    if (!dev.found) return;

    net_pci_enable_device(&dev);
    uint32_t bar0 = net_pci_read_bar(dev.bus, dev.slot, dev.func, 0) & 0xFFFFFFF0;

    if (e1000_driver_init(bar0, dev.irq_line) == 0)
    {
        vga_print_string("[RING 0] INITIALIZATION: APTO", 0, 15);
    }
}
