#ifndef PICFW_EEPROM_H
#define PICFW_EEPROM_H

#include "common.h"

/* PIC16F15356 EEPROM: 256 bytes accessed via NVM controller.
 *
 * Simulation model: 256-byte mirror array matching the bootloader's
 * ee_data[256] approach.  On real hardware, reads go through NVM SFR
 * sequences (NVMCON1/NVMCON2/NVMADR/NVMDAT).  The simulation provides
 * the same byte-level read/write interface for testing parity. */

#define PICFW_EEPROM_SIZE 256u

/* Magic bytes at EEPROM offset 0x00-0x01 (match bootloader model) */
#define PICFW_EEPROM_MAGIC_0 0x55u
#define PICFW_EEPROM_MAGIC_1 0xAAu

typedef struct picfw_eeprom {
  uint8_t data[PICFW_EEPROM_SIZE];
} picfw_eeprom_t;

/* Initialize EEPROM with default content (magic bytes + 0xFF fill). */
void picfw_eeprom_init(picfw_eeprom_t *ee);

/* Read a byte from EEPROM. Returns 0xFF on NULL (address is always
 * in-bounds since uint8_t range [0,255] matches the 256-byte array). */
uint8_t picfw_eeprom_read_byte(const picfw_eeprom_t *ee, uint8_t address);

/* Write a byte to EEPROM. No-op on out-of-bounds or NULL. */
void picfw_eeprom_write_byte(picfw_eeprom_t *ee, uint8_t address,
                              uint8_t value);

/* Read a contiguous block from EEPROM into a buffer.
 * Returns number of bytes actually read (clamped to EEPROM bounds). */
uint8_t picfw_eeprom_read_block(const picfw_eeprom_t *ee, uint8_t address,
                                 uint8_t *out, uint8_t len);

/* Write a contiguous block to EEPROM from a buffer.
 * Returns number of bytes actually written (clamped to EEPROM bounds). */
uint8_t picfw_eeprom_write_block(picfw_eeprom_t *ee, uint8_t address,
                                  const uint8_t *data, uint8_t len);

#endif
