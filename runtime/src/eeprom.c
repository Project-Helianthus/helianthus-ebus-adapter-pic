#include "picfw/eeprom.h"

#include <string.h>

void picfw_eeprom_init(picfw_eeprom_t *ee) {
  if (ee == 0) {
    return;
  }
  /* Fill with 0xFF (erased state), then write magic bytes */
  memset(ee->data, 0xFFu, PICFW_EEPROM_SIZE);
  ee->data[0] = PICFW_EEPROM_MAGIC_0;
  ee->data[1] = PICFW_EEPROM_MAGIC_1;
}

uint8_t picfw_eeprom_read_byte(const picfw_eeprom_t *ee, uint8_t address) {
  if (ee == 0) {
    return 0xFFu;
  }
  return ee->data[address];
}

void picfw_eeprom_write_byte(picfw_eeprom_t *ee, uint8_t address,
                              uint8_t value) {
  if (ee == 0) {
    return;
  }
  ee->data[address] = value;
}

uint8_t picfw_eeprom_read_block(const picfw_eeprom_t *ee, uint8_t address,
                                 uint8_t *out, uint8_t len) {
  uint8_t avail;
  uint8_t count;

  if (ee == 0 || out == 0 || len == 0u) {
    return 0u;
  }

  /* Clamp to EEPROM bounds */
  avail = (uint8_t)(PICFW_EEPROM_SIZE - address);
  count = (len < avail) ? len : avail;
  memcpy(out, &ee->data[address], count);
  return count;
}

uint8_t picfw_eeprom_write_block(picfw_eeprom_t *ee, uint8_t address,
                                  const uint8_t *data, uint8_t len) {
  uint8_t avail;
  uint8_t count;

  if (ee == 0 || data == 0 || len == 0u) {
    return 0u;
  }

  avail = (uint8_t)(PICFW_EEPROM_SIZE - address);
  count = (len < avail) ? len : avail;
  memcpy(&ee->data[address], data, count);
  return count;
}
