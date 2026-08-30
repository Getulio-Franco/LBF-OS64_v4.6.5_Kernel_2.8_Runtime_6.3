/* ============================================================================
 * ARCHITECTURE: Ring 0 (Kernel Driver Interface)
 * FILE: drivers/net/net_driver_e1000.h
 * DESCRIPTION: Mapeamento de registradores e descritores DMA da NIC Intel E1000
 * TARGET: Intel 82540EM / PCI ID 8086:100E
 * ============================================================================ */

#ifndef NET_DRIVER_E1000_H
#define NET_DRIVER_E1000_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * REGISTRADORES MMIO - INTEL E1000 / 82540EM
 * ============================================================================ */

#define E1000_CTRL              0x00000
#define E1000_STATUS            0x00008
#define E1000_EERD              0x00014

/* Interrupções */
#define E1000_ICR               0x000C0
#define E1000_ICS               0x000C8
#define E1000_IMS               0x000D0
#define E1000_IMC               0x000D8

/* Receive */
#define E1000_RCTL              0x00100
#define E1000_RDBAL             0x02800
#define E1000_RDBAH             0x02804
#define E1000_RDLEN             0x02808
#define E1000_RDH               0x02810
#define E1000_RDT               0x02818

/* Transmit */
#define E1000_TCTL              0x00400
#define E1000_TIPG              0x00410
#define E1000_TDBAL             0x03800
#define E1000_TDBAH             0x03804
#define E1000_TDLEN             0x03808
#define E1000_TDH               0x03810
#define E1000_TDT               0x03818

/* Multicast / Receive Address */
#define E1000_MTA               0x05200
#define E1000_RA                0x05400

/* ============================================================================
 * PCI / DEVICE CONSTANTS
 * ============================================================================ */

#define E1000_VENDOR_ID         0x8086
#define E1000_DEVICE_ID         0x100E

/* ============================================================================
 * CTRL FLAGS
 * ============================================================================ */

#define E1000_CTRL_FD           (1U << 0)
#define E1000_CTRL_LRST         (1U << 3)
#define E1000_CTRL_ASDE         (1U << 5)
#define E1000_CTRL_SLU          (1U << 6)
#define E1000_CTRL_ILOS         (1U << 7)
#define E1000_CTRL_RST          (1U << 26)

/* ============================================================================
 * RCTL FLAGS
 * ============================================================================ */

#define RCTL_RST                (1U << 0)
#define RCTL_EN                 (1U << 1)
#define RCTL_SBP                (1U << 2)
#define RCTL_UPE                (1U << 3)
#define RCTL_MPE                (1U << 4)
#define RCTL_LPE                (1U << 5)

#define E1000_RCTL_LBM_NO       (0U << 6)
#define E1000_RCTL_LBM_MAC      (1U << 6)
#define E1000_RCTL_LBM_SLP      (2U << 6)
#define E1000_RCTL_LBM_TCVR     (3U << 6)
#define E1000_RCTL_LBM_MASK     (3U << 6)

#define RCTL_BAM                (1U << 15)
#define RCTL_SECRC              (1U << 26)

/* ============================================================================
 * TCTL FLAGS
 * ============================================================================ */

#define TCTL_EN                 (1U << 1)
#define TCTL_PSP                (1U << 3)
#define TCTL_CT_SHIFT           4
#define TCTL_COLD_SHIFT         12
#define TCTL_RTLC               (1U << 24)

/* ============================================================================
 * TX DESCRIPTOR STATUS / CMD FLAGS
 * ============================================================================ */

#define E1000_TXD_STAT_DD       (1U << 0)
#define E1000_TXD_CMD_EOP       (1U << 0)
#define E1000_TXD_CMD_IFCS      (1U << 1)
#define E1000_TXD_CMD_RS        (1U << 3)

/* ============================================================================
 * DESCRITORES E CONFIGURAÇÕES DE DMA
 * ============================================================================ */

#define E1000_MAC_LEN           6
#define E1000_NUM_RX_DESC       32
#define E1000_NUM_TX_DESC       32
#define E1000_PACKET_SIZE       2048

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    uint32_t mmio_base;
    uint8_t  mac_address[E1000_MAC_LEN];
    uint8_t  irq_line;
    e1000_rx_desc_t *rx_descs;
    e1000_tx_desc_t *tx_descs;
    uint16_t rx_cur;
    uint16_t tx_cur;
    bool     initialized;
    bool     link_up;
} e1000_driver_state_t;

/* ============================================================================
 * API DO DRIVER
 * ============================================================================ */

int  e1000_driver_init(uint32_t mmio_base, uint8_t irq_line);
int  e1000_send_packet(const void *data, uint16_t len);
int  e1000_receive_packet(void *out_buffer, uint16_t *out_len);
void e1000_get_mac_address(uint8_t out_mac[6]);
void e1000_irq_handler(void);
void e1000_validate_ring0(void);
void e1000_hard_reset(void);

#endif /* NET_DRIVER_E1000_H */
