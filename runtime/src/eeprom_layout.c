#include "picfw/eeprom_layout.h"

#include <string.h>

/* CRC16-CCITT (same algorithm as bootloader's picboot_crc16_ccitt). */
static uint16_t crc16_ccitt(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFFu;
  uint8_t i;
  uint8_t j;

  for (i = 0u; i < len; i++) {
    crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
    for (j = 0u; j < 8u; j++) {
      if ((crc & 0x8000u) != 0u) {
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

uint16_t picfw_eeprom_ip_config_crc(const picfw_eeprom_t *ee) {
  uint8_t buf[14]; /* 0x10..0x1D = 14 bytes */
  uint8_t count;

  if (ee == 0) {
    return 0u;
  }

  count = picfw_eeprom_read_block(ee, PICFW_EEPROM_IP_OFFSET, buf,
                                   (uint8_t)(PICFW_EEPROM_CRC_OFFSET -
                                             PICFW_EEPROM_IP_OFFSET));
  if (count != 14u) {
    return 0u;
  }

  return crc16_ccitt(buf, 14u);
}

picfw_bool_t picfw_eeprom_read_ip_config(const picfw_eeprom_t *ee,
                                          picfw_ip_config_t *out) {
  uint16_t stored_crc;
  uint16_t computed_crc;
  uint8_t version;

  if (out == 0) {
    return PICFW_FALSE;
  }
  memset(out, 0, sizeof(*out));
  out->valid = PICFW_FALSE;

  if (ee == 0) {
    return PICFW_FALSE;
  }

  /* Check config version */
  version = picfw_eeprom_read_byte(ee, PICFW_EEPROM_CFG_VER_OFFSET);
  if (version != PICFW_EEPROM_CFG_VERSION) {
    return PICFW_FALSE;
  }

  /* Verify CRC */
  stored_crc = (uint16_t)(
      ((uint16_t)picfw_eeprom_read_byte(ee, PICFW_EEPROM_CRC_OFFSET) << 8) |
      (uint16_t)picfw_eeprom_read_byte(ee, PICFW_EEPROM_CRC_OFFSET + 1u));
  computed_crc = picfw_eeprom_ip_config_crc(ee);
  if (stored_crc != computed_crc) {
    return PICFW_FALSE;
  }

  /* Read fields */
  picfw_eeprom_read_block(ee, PICFW_EEPROM_IP_OFFSET, out->ip, 4u);
  picfw_eeprom_read_block(ee, PICFW_EEPROM_MASK_OFFSET, out->mask, 4u);
  picfw_eeprom_read_block(ee, PICFW_EEPROM_GW_OFFSET, out->gateway, 4u);
  out->dhcp_enabled = (picfw_bool_t)(
      picfw_eeprom_read_byte(ee, PICFW_EEPROM_DHCP_OFFSET) ==
      PICFW_EEPROM_DHCP_ENABLED);
  out->valid = PICFW_TRUE;
  return PICFW_TRUE;
}

void picfw_eeprom_write_ip_config(picfw_eeprom_t *ee,
                                   const picfw_ip_config_t *config) {
  uint16_t crc;

  if (ee == 0 || config == 0) {
    return;
  }

  picfw_eeprom_write_block(ee, PICFW_EEPROM_IP_OFFSET, config->ip, 4u);
  picfw_eeprom_write_block(ee, PICFW_EEPROM_MASK_OFFSET, config->mask, 4u);
  picfw_eeprom_write_block(ee, PICFW_EEPROM_GW_OFFSET, config->gateway, 4u);
  picfw_eeprom_write_byte(ee, PICFW_EEPROM_DHCP_OFFSET,
                           config->dhcp_enabled ? PICFW_EEPROM_DHCP_ENABLED
                                                : PICFW_EEPROM_DHCP_DISABLED);
  picfw_eeprom_write_byte(ee, PICFW_EEPROM_CFG_VER_OFFSET,
                           PICFW_EEPROM_CFG_VERSION);

  /* Compute and store CRC over 0x10-0x1D */
  crc = picfw_eeprom_ip_config_crc(ee);
  picfw_eeprom_write_byte(ee, PICFW_EEPROM_CRC_OFFSET,
                           (uint8_t)(crc >> 8));
  picfw_eeprom_write_byte(ee, PICFW_EEPROM_CRC_OFFSET + 1u,
                           (uint8_t)(crc & 0xFFu));
}
