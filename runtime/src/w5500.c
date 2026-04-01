#include "picfw/w5500.h"

#include <string.h>

/* --- Internal helpers --- */

static picfw_bool_t common_addr_valid(uint16_t addr) {
  return (picfw_bool_t)(addr < PICFW_W5500_COMMON_REG_SIZE);
}

static picfw_bool_t socket_addr_valid(uint16_t addr) {
  return (picfw_bool_t)(addr < PICFW_W5500_SOCKET_REG_SIZE);
}

/* Read a big-endian uint16_t from a register pair. */
static uint16_t read_u16_be(const uint8_t *regs, uint16_t addr) {
  return (uint16_t)((uint16_t)regs[addr] << 8) | (uint16_t)regs[addr + 1u];
}

/* Write a big-endian uint16_t to a register pair. */
static void write_u16_be(uint8_t *regs, uint16_t addr, uint16_t value) {
  regs[addr] = (uint8_t)(value >> 8);
  regs[addr + 1u] = (uint8_t)(value & 0xFFu);
}

/* --- Socket command processing (simulation model) --- */

/* Resolve OPEN command to the appropriate socket status based on mode. */
static uint8_t resolve_open_status(uint8_t mode) {
  if (mode == PICFW_W5500_SN_MR_TCP) {
    return PICFW_W5500_SN_SR_INIT;
  }
  if (mode == PICFW_W5500_SN_MR_UDP) {
    return PICFW_W5500_SN_SR_UDP;
  }
  return PICFW_W5500_SN_SR_CLOSED;
}

static void process_socket_command(picfw_w5500_t *w5500) {
  uint8_t cmd = w5500->socket0.regs[PICFW_W5500_SN_CR];

  if (cmd == 0u) {
    return;
  }

  switch (cmd) {
  case PICFW_W5500_SN_CR_OPEN:
    w5500->socket0.regs[PICFW_W5500_SN_SR] =
        resolve_open_status(w5500->socket0.regs[PICFW_W5500_SN_MR]);
    break;
  case PICFW_W5500_SN_CR_LISTEN:
    if (w5500->socket0.regs[PICFW_W5500_SN_SR] == PICFW_W5500_SN_SR_INIT) {
      w5500->socket0.regs[PICFW_W5500_SN_SR] = PICFW_W5500_SN_SR_LISTEN;
    }
    break;
  case PICFW_W5500_SN_CR_CLOSE:
  case PICFW_W5500_SN_CR_DISCON:
    w5500->socket0.regs[PICFW_W5500_SN_SR] = PICFW_W5500_SN_SR_CLOSED;
    break;
  default:
    break;
  }

  w5500->socket0.regs[PICFW_W5500_SN_CR] = 0u;
}

/* --- Public API --- */

void picfw_w5500_init(picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return;
  }
  memset(w5500, 0, sizeof(*w5500));
  /* Set chip version register (W5500 = 0x04) */
  w5500->common[PICFW_W5500_VERSIONR] = 0x04u;
  /* Default retry: 200ms (0x07D0), 8 retries */
  write_u16_be(w5500->common, PICFW_W5500_RTR0, 0x07D0u);
  w5500->common[PICFW_W5500_RCR] = 0x08u;
  w5500->initialized = PICFW_TRUE;
}

uint8_t picfw_w5500_read_common(const picfw_w5500_t *w5500, uint16_t addr) {
  if (w5500 == 0 || !common_addr_valid(addr)) {
    return 0u;
  }
  return w5500->common[addr];
}

void picfw_w5500_write_common(picfw_w5500_t *w5500, uint16_t addr,
                               uint8_t value) {
  if (w5500 == 0 || !common_addr_valid(addr)) {
    return;
  }
  w5500->common[addr] = value;
}

uint8_t picfw_w5500_read_socket(const picfw_w5500_t *w5500, uint16_t addr) {
  if (w5500 == 0 || !socket_addr_valid(addr)) {
    return 0u;
  }
  return w5500->socket0.regs[addr];
}

void picfw_w5500_write_socket(picfw_w5500_t *w5500, uint16_t addr,
                               uint8_t value) {
  if (w5500 == 0 || !socket_addr_valid(addr)) {
    return;
  }
  w5500->socket0.regs[addr] = value;

  /* Auto-process commands when written to CR */
  if (addr == PICFW_W5500_SN_CR) {
    process_socket_command(w5500);
  }
}

picfw_bool_t picfw_w5500_link_up(const picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return PICFW_FALSE;
  }
  return (picfw_bool_t)((w5500->common[PICFW_W5500_PHYCFGR] & 0x01u) != 0u);
}

void picfw_w5500_set_mac(picfw_w5500_t *w5500, const uint8_t mac[6]) {
  if (w5500 == 0 || mac == 0) {
    return;
  }
  memcpy(&w5500->common[PICFW_W5500_SHAR0], mac, 6u);
}

void picfw_w5500_set_ip(picfw_w5500_t *w5500, const uint8_t ip[4]) {
  if (w5500 == 0 || ip == 0) {
    return;
  }
  memcpy(&w5500->common[PICFW_W5500_SIPR0], ip, 4u);
}

void picfw_w5500_set_subnet(picfw_w5500_t *w5500, const uint8_t mask[4]) {
  if (w5500 == 0 || mask == 0) {
    return;
  }
  memcpy(&w5500->common[PICFW_W5500_SUBR0], mask, 4u);
}

void picfw_w5500_set_gateway(picfw_w5500_t *w5500, const uint8_t gw[4]) {
  if (w5500 == 0 || gw == 0) {
    return;
  }
  memcpy(&w5500->common[PICFW_W5500_GAR0], gw, 4u);
}

picfw_bool_t picfw_w5500_socket_open_tcp(picfw_w5500_t *w5500, uint16_t port) {
  if (w5500 == 0) {
    return PICFW_FALSE;
  }
  /* Set mode to TCP */
  w5500->socket0.regs[PICFW_W5500_SN_MR] = PICFW_W5500_SN_MR_TCP;
  /* Set source port */
  write_u16_be(w5500->socket0.regs, PICFW_W5500_SN_PORT0, port);
  /* Issue OPEN command */
  w5500->socket0.regs[PICFW_W5500_SN_CR] = PICFW_W5500_SN_CR_OPEN;
  process_socket_command(w5500);
  return (picfw_bool_t)(w5500->socket0.regs[PICFW_W5500_SN_SR] ==
                        PICFW_W5500_SN_SR_INIT);
}

picfw_bool_t picfw_w5500_socket_listen(picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return PICFW_FALSE;
  }
  w5500->socket0.regs[PICFW_W5500_SN_CR] = PICFW_W5500_SN_CR_LISTEN;
  process_socket_command(w5500);
  return (picfw_bool_t)(w5500->socket0.regs[PICFW_W5500_SN_SR] ==
                        PICFW_W5500_SN_SR_LISTEN);
}

void picfw_w5500_socket_close(picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return;
  }
  w5500->socket0.regs[PICFW_W5500_SN_CR] = PICFW_W5500_SN_CR_CLOSE;
  process_socket_command(w5500);
}

uint8_t picfw_w5500_socket_status(const picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return PICFW_W5500_SN_SR_CLOSED;
  }
  return w5500->socket0.regs[PICFW_W5500_SN_SR];
}

uint16_t picfw_w5500_socket_rx_size(const picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return 0u;
  }
  return read_u16_be(w5500->socket0.regs, PICFW_W5500_SN_RX_RSR0);
}

uint16_t picfw_w5500_socket_tx_free(const picfw_w5500_t *w5500) {
  if (w5500 == 0) {
    return 0u;
  }
  return read_u16_be(w5500->socket0.regs, PICFW_W5500_SN_TX_FSR0);
}
