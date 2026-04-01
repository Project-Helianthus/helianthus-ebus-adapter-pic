#ifndef PICFW_W5500_H
#define PICFW_W5500_H

#include "common.h"

/* W5500 Ethernet controller register-level abstraction.
 *
 * Simulation model: register mirrors for common registers and socket 0.
 * On real hardware, register access goes through SPI1 (RB4-RB7) using
 * the W5500 variable-length data frame protocol.
 *
 * Only socket 0 is allocated (ebusd uses a single TCP connection).
 * This saves 392 bytes vs mirroring all 8 sockets. */

/* --- Common register block (BSB=0x00) --- */
#define PICFW_W5500_MR       0x0000u  /* Mode Register */
#define PICFW_W5500_GAR0     0x0001u  /* Gateway Address */
#define PICFW_W5500_SUBR0    0x0005u  /* Subnet Mask */
#define PICFW_W5500_SHAR0    0x0009u  /* Source Hardware Address (MAC) */
#define PICFW_W5500_SIPR0    0x000Fu  /* Source IP Address */
#define PICFW_W5500_IR       0x0015u  /* Interrupt Register */
#define PICFW_W5500_IMR      0x0016u  /* Interrupt Mask */
#define PICFW_W5500_RTR0     0x0019u  /* Retry Time (2 bytes) */
#define PICFW_W5500_RCR      0x001Bu  /* Retry Count */
#define PICFW_W5500_PHYCFGR  0x002Eu  /* PHY Configuration (bit0=link) */
#define PICFW_W5500_VERSIONR 0x0039u  /* Chip Version (should be 0x04) */

/* Common register mirror size */
#define PICFW_W5500_COMMON_REG_SIZE 64u

/* --- Socket 0 register block (BSB=0x01) --- */
#define PICFW_W5500_SN_MR      0x0000u  /* Socket Mode */
#define PICFW_W5500_SN_CR      0x0001u  /* Socket Command */
#define PICFW_W5500_SN_IR      0x0002u  /* Socket Interrupt */
#define PICFW_W5500_SN_SR      0x0003u  /* Socket Status */
#define PICFW_W5500_SN_PORT0   0x0004u  /* Source Port (2 bytes) */
#define PICFW_W5500_SN_DHAR0   0x0006u  /* Destination MAC (6 bytes) */
#define PICFW_W5500_SN_DIPR0   0x000Cu  /* Destination IP (4 bytes) */
#define PICFW_W5500_SN_DPORT0  0x0010u  /* Destination Port (2 bytes) */
#define PICFW_W5500_SN_TXBUF_SIZE 0x001Eu  /* TX Buffer Size */
#define PICFW_W5500_SN_RXBUF_SIZE 0x001Eu  /* RX Buffer Size (same offset) */
#define PICFW_W5500_SN_TX_FSR0 0x0020u  /* TX Free Size (2 bytes) */
#define PICFW_W5500_SN_TX_RD0  0x0022u  /* TX Read Pointer (2 bytes) */
#define PICFW_W5500_SN_TX_WR0  0x0024u  /* TX Write Pointer (2 bytes) */
#define PICFW_W5500_SN_RX_RSR0 0x0026u  /* RX Received Size (2 bytes) */
#define PICFW_W5500_SN_RX_RD0  0x0028u  /* RX Read Pointer (2 bytes) */
#define PICFW_W5500_SN_RX_WR0  0x002Au  /* RX Write Pointer (2 bytes) */

/* Socket register mirror size */
#define PICFW_W5500_SOCKET_REG_SIZE 48u

/* Socket mode values */
#define PICFW_W5500_SN_MR_TCP   0x01u
#define PICFW_W5500_SN_MR_UDP   0x02u
#define PICFW_W5500_SN_MR_MACRAW 0x04u

/* Socket commands */
#define PICFW_W5500_SN_CR_OPEN    0x01u
#define PICFW_W5500_SN_CR_LISTEN  0x02u
#define PICFW_W5500_SN_CR_CONNECT 0x04u
#define PICFW_W5500_SN_CR_DISCON  0x08u
#define PICFW_W5500_SN_CR_CLOSE   0x10u
#define PICFW_W5500_SN_CR_SEND    0x20u
#define PICFW_W5500_SN_CR_RECV    0x40u

/* Socket status values */
#define PICFW_W5500_SN_SR_CLOSED      0x00u
#define PICFW_W5500_SN_SR_INIT        0x13u
#define PICFW_W5500_SN_SR_LISTEN      0x14u
#define PICFW_W5500_SN_SR_ESTABLISHED 0x17u
#define PICFW_W5500_SN_SR_CLOSE_WAIT  0x1Cu
#define PICFW_W5500_SN_SR_UDP         0x22u
#define PICFW_W5500_SN_SR_MACRAW      0x42u

/* TX/RX buffer sizes */
#define PICFW_W5500_TX_BUF_SIZE 2048u
#define PICFW_W5500_RX_BUF_SIZE 2048u

/* Default ebusd listen port */
#define PICFW_W5500_EBUSD_PORT 3333u

/* --- Data types --- */

typedef struct picfw_w5500_socket {
  uint8_t regs[PICFW_W5500_SOCKET_REG_SIZE];
} picfw_w5500_socket_t;

typedef struct picfw_w5500 {
  uint8_t common[PICFW_W5500_COMMON_REG_SIZE];
  picfw_w5500_socket_t socket0;
  picfw_bool_t initialized;
  picfw_bool_t link_up;
} picfw_w5500_t;

/* --- API --- */

/* Initialize W5500 (chip reset sequence). */
void picfw_w5500_init(picfw_w5500_t *w5500);

/* Read a common register byte. */
uint8_t picfw_w5500_read_common(const picfw_w5500_t *w5500, uint16_t addr);

/* Write a common register byte. */
void picfw_w5500_write_common(picfw_w5500_t *w5500, uint16_t addr,
                               uint8_t value);

/* Read a socket 0 register byte. */
uint8_t picfw_w5500_read_socket(const picfw_w5500_t *w5500, uint16_t addr);

/* Write a socket 0 register byte. */
void picfw_w5500_write_socket(picfw_w5500_t *w5500, uint16_t addr,
                               uint8_t value);

/* Check link status (reads PHYCFGR bit 0). */
picfw_bool_t picfw_w5500_link_up(const picfw_w5500_t *w5500);

/* Set MAC address (writes SHAR0-5). */
void picfw_w5500_set_mac(picfw_w5500_t *w5500, const uint8_t mac[6]);

/* Set IP address (writes SIPR0-3). */
void picfw_w5500_set_ip(picfw_w5500_t *w5500, const uint8_t ip[4]);

/* Set subnet mask (writes SUBR0-3). */
void picfw_w5500_set_subnet(picfw_w5500_t *w5500, const uint8_t mask[4]);

/* Set gateway (writes GAR0-3). */
void picfw_w5500_set_gateway(picfw_w5500_t *w5500, const uint8_t gw[4]);

/* Open socket 0 as TCP on the given port. Returns TRUE on success. */
picfw_bool_t picfw_w5500_socket_open_tcp(picfw_w5500_t *w5500, uint16_t port);

/* Put socket 0 into LISTEN mode. Returns TRUE on success. */
picfw_bool_t picfw_w5500_socket_listen(picfw_w5500_t *w5500);

/* Close socket 0. */
void picfw_w5500_socket_close(picfw_w5500_t *w5500);

/* Get socket 0 status register. */
uint8_t picfw_w5500_socket_status(const picfw_w5500_t *w5500);

/* Get RX received data size for socket 0 (2 bytes, big-endian). */
uint16_t picfw_w5500_socket_rx_size(const picfw_w5500_t *w5500);

/* Get TX free buffer size for socket 0 (2 bytes, big-endian). */
uint16_t picfw_w5500_socket_tx_free(const picfw_w5500_t *w5500);

#endif
