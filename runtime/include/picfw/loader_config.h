#ifndef PICFW_LOADER_CONFIG_H
#define PICFW_LOADER_CONFIG_H

#include "common.h"
#include "eeprom_layout.h"

#define PICFW_LOADER_CONFIG_SPACE_SIZE 512u
#define PICFW_LOADER_CONFIG_SETTINGS_OFFSET 0x0000u
#define PICFW_LOADER_CONFIG_SETTINGS_LEN 8u
#define PICFW_LOADER_CONFIG_MUI_OFFSET 0x0106u
#define PICFW_LOADER_CONFIG_MUI_LEN 8u

#define PICFW_LOADER_VARIANT_NON_ENHANCED 0u
#define PICFW_LOADER_VARIANT_ETHERNET 1u
#define PICFW_LOADER_VARIANT_WIFI 2u
#define PICFW_LOADER_VARIANT_RPI_USB_HIGH 3u

typedef struct picfw_loader_storage {
  uint8_t config_space[PICFW_LOADER_CONFIG_SPACE_SIZE];
} picfw_loader_storage_t;

typedef struct picfw_loader_config {
  uint8_t settings[PICFW_LOADER_CONFIG_SETTINGS_LEN];
  uint8_t mui[PICFW_LOADER_CONFIG_MUI_LEN];
  uint8_t user_id[4];
  uint8_t static_ip[4];
  uint8_t subnet_mask[4];
  uint8_t gateway_ip[4];
  uint8_t mac[6];
  uint8_t mask_len;
  uint8_t gateway_bits;
  uint8_t variant_mode;
  picfw_bool_t valid;
  picfw_bool_t use_mui;
  picfw_bool_t dhcp_enabled;
  picfw_bool_t visual_ping;
  picfw_bool_t allow_hardware_jumpers;
  uint16_t arbitration_delay_us;
} picfw_loader_config_t;

typedef struct picfw_loader_effective_mode {
  uint8_t variant;
  picfw_bool_t enhanced_protocol;
  picfw_bool_t high_speed;
} picfw_loader_effective_mode_t;

void picfw_loader_storage_init_default(picfw_loader_storage_t *storage);
uint8_t picfw_loader_storage_read_block(const picfw_loader_storage_t *storage,
                                       uint16_t address, uint8_t *out,
                                       uint8_t len);
uint8_t picfw_loader_storage_write_block(picfw_loader_storage_t *storage,
                                        uint16_t address,
                                        const uint8_t *data, uint8_t len);
picfw_bool_t picfw_loader_config_decode(const picfw_loader_storage_t *storage,
                                        picfw_loader_config_t *out);
void picfw_loader_config_resolve_mode(
    const picfw_loader_config_t *config, uint8_t strap_variant,
    picfw_bool_t strap_enhanced_protocol, picfw_bool_t strap_high_speed,
    picfw_loader_effective_mode_t *out);
uint8_t picfw_loader_effective_jumpers(
    const picfw_loader_effective_mode_t *mode);
void picfw_loader_config_to_ip_config(const picfw_loader_config_t *config,
                                      picfw_ip_config_t *out);

#endif
