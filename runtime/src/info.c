#include "picfw/info.h"

#include <string.h>

enum {
  PICFW_INFO_RESET_POWER_ON = 1u,
  PICFW_INFO_RESET_BROWN_OUT = 2u,
  PICFW_INFO_RESET_WATCHDOG = 3u,
  PICFW_INFO_RESET_CLEAR = 4u,
  PICFW_INFO_RESET_EXTERNAL = 5u,
  PICFW_INFO_RESET_STACK = 6u,
  PICFW_INFO_RESET_MEMORY = 7u,
};

static const char *picfw_reset_cause_name(uint8_t cause_code) {
  switch (cause_code) {
    case PICFW_INFO_RESET_POWER_ON:
      return "power_on";
    case PICFW_INFO_RESET_BROWN_OUT:
      return "brown_out";
    case PICFW_INFO_RESET_WATCHDOG:
      return "watchdog";
    case PICFW_INFO_RESET_CLEAR:
      return "clear";
    case PICFW_INFO_RESET_EXTERNAL:
      return "external_reset";
    case PICFW_INFO_RESET_STACK:
      return "stack_overflow";
    case PICFW_INFO_RESET_MEMORY:
      return "memory_failure";
    default:
      return "unknown";
  }
}

int picfw_info_parse_version(const uint8_t *input, size_t input_len, picfw_adapter_version_t *out) {
  if (input == 0 || out == 0) {
    return -1;
  }
  if (input_len != 2u && input_len != 5u && input_len != 8u) {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  out->version = input[0];
  out->features = input[1];
  out->supports_info = (picfw_bool_t)((out->features & 0x01u) != 0u);

  if (input_len >= 5u) {
    out->has_checksum = PICFW_TRUE;
    out->checksum = (uint16_t)(((uint16_t)input[2] << 8) | input[3]);
    out->jumpers = input[4];
    out->is_wifi = (picfw_bool_t)((out->jumpers & 0x08u) != 0u);
    out->is_ethernet = (picfw_bool_t)((out->jumpers & 0x04u) != 0u);
    out->is_high_speed = (picfw_bool_t)((out->jumpers & 0x02u) != 0u);
    out->is_v31 = (picfw_bool_t)((out->jumpers & 0x10u) != 0u);
  }
  if (input_len == 8u) {
    out->has_bootloader = PICFW_TRUE;
    out->bootloader_version = input[5];
    out->bootloader_checksum = (uint16_t)(((uint16_t)input[6] << 8) | input[7]);
  }

  return 0;
}

int picfw_info_parse_reset(const uint8_t *input, size_t input_len, picfw_adapter_reset_info_t *out) {
  if (input == 0 || out == 0) {
    return -1;
  }
  if (input_len < 2u) {
    return -1;
  }

  out->cause_code = input[0];
  out->restart_count = input[1];
  out->cause_name = picfw_reset_cause_name(input[0]);
  return 0;
}
