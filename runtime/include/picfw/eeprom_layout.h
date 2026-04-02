#ifndef PICFW_EEPROM_LAYOUT_H
#define PICFW_EEPROM_LAYOUT_H

#include "eeprom.h"

/* EEPROM layout convention for PIC16F15356 eBUS adapter network config.
 *
 * This defines the byte offsets and structure for IP configuration
 * stored in EEPROM.  The bootloader's READ_EE_DATA / WRITE_EE_DATA
 * commands (via ebuspicloader -i IP -m MASK) write to these offsets.
 *
 * Layout:
 *   0x00-0x01: Magic bytes (0x55, 0xAA) — set by eeprom_init
 *   0x10-0x13: IP address (4 bytes, network byte order)
 *   0x14-0x17: Subnet mask (4 bytes)
 *   0x18-0x1B: Default gateway (4 bytes)
 *   0x1C:      DHCP enable (0x01 = enabled, 0x00 = disabled)
 *   0x1D:      Config version (0x01)
 *   0x1E-0x1F: CRC16-CCITT over 0x10-0x1D (big-endian) */

#define PICFW_EEPROM_IP_OFFSET    0x10u
#define PICFW_EEPROM_MASK_OFFSET  0x14u
#define PICFW_EEPROM_GW_OFFSET    0x18u
#define PICFW_EEPROM_DHCP_OFFSET  0x1Cu
#define PICFW_EEPROM_CFG_VER_OFFSET 0x1Du
#define PICFW_EEPROM_CRC_OFFSET   0x1Eu

#define PICFW_EEPROM_CFG_VERSION  0x01u
#define PICFW_EEPROM_DHCP_ENABLED 0x01u
#define PICFW_EEPROM_DHCP_DISABLED 0x00u

/* IP configuration block (stack-local, not stored in HAL). */
typedef struct picfw_ip_config {
  uint8_t ip[4];
  uint8_t mask[4];
  uint8_t gateway[4];
  picfw_bool_t dhcp_enabled;
  picfw_bool_t valid;  /* CRC + version check passed */
} picfw_ip_config_t;

/* Read IP config from EEPROM, validate CRC and version.
 * Returns TRUE if config is valid (CRC matches, version == 0x01).
 * On invalid config, out->valid is FALSE and fields are zeroed. */
picfw_bool_t picfw_eeprom_read_ip_config(const picfw_eeprom_t *ee,
                                          picfw_ip_config_t *out);

/* Write IP config to EEPROM with CRC and version stamp. */
void picfw_eeprom_write_ip_config(picfw_eeprom_t *ee,
                                   const picfw_ip_config_t *config);

/* Compute CRC16-CCITT over the config region (0x10-0x1D). */
uint16_t picfw_eeprom_ip_config_crc(const picfw_eeprom_t *ee);

#endif
