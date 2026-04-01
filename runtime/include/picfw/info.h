#ifndef PICFW_INFO_H
#define PICFW_INFO_H

#include "common.h"

typedef enum picfw_adapter_info_id {
  PICFW_ADAPTER_INFO_VERSION = 0x00,
  PICFW_ADAPTER_INFO_HARDWARE_ID = 0x01,
  PICFW_ADAPTER_INFO_HARDWARE_CONF = 0x02,
  PICFW_ADAPTER_INFO_TEMPERATURE = 0x03,
  PICFW_ADAPTER_INFO_SUPPLY_VOLT = 0x04,
  PICFW_ADAPTER_INFO_BUS_VOLT = 0x05,
  PICFW_ADAPTER_INFO_RESET_INFO = 0x06,
  PICFW_ADAPTER_INFO_WIFI_RSSI = 0x07,
} picfw_adapter_info_id_t;

typedef struct picfw_adapter_version {
  uint8_t version;
  uint8_t features;
  uint16_t checksum;
  uint8_t jumpers;
  uint8_t bootloader_version;
  uint16_t bootloader_checksum;
  picfw_bool_t has_checksum;
  picfw_bool_t has_bootloader;
  picfw_bool_t supports_info;
  picfw_bool_t is_wifi;
  picfw_bool_t is_ethernet;
  picfw_bool_t is_high_speed;
  picfw_bool_t is_v31;
} picfw_adapter_version_t;

typedef struct picfw_adapter_reset_info {
  const char *cause_name;
  uint8_t cause_code;
  uint8_t restart_count;
} picfw_adapter_reset_info_t;

int picfw_info_parse_version(const uint8_t *input, size_t input_len, picfw_adapter_version_t *out);
int picfw_info_parse_reset(const uint8_t *input, size_t input_len, picfw_adapter_reset_info_t *out);

#endif
