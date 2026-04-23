#include "picfw/loader_config.h"

#include "picfw/ethernet.h"

#include <string.h>

static uint8_t mask_byte_for_bits(uint8_t bits) {
  if (bits >= 8u) {
    return 0xFFu;
  }
  if (bits == 0u) {
    return 0x00u;
  }
  return (uint8_t)(0xFFu ^ ((1u << (8u - bits)) - 1u));
}

static void derive_subnet_mask(uint8_t mask_len, uint8_t out[4]) {
  uint8_t idx;
  uint8_t remaining = mask_len;

  for (idx = 0u; idx < 4u; ++idx) {
    out[idx] = mask_byte_for_bits(remaining);
    if (remaining >= 8u) {
      remaining = (uint8_t)(remaining - 8u);
    } else {
      remaining = 0u;
    }
  }
}

static void derive_network_base(const picfw_loader_config_t *config,
                                uint8_t out[4]) {
  uint8_t idx;
  uint8_t remaining;

  if (config == 0 || out == 0) {
    return;
  }

  memcpy(out, config->static_ip, 4u);
  remaining = config->mask_len;
  for (idx = 0u; idx < 4u; ++idx) {
    uint8_t mask = mask_byte_for_bits(remaining);
    out[idx] &= mask;
    if (remaining >= 8u) {
      remaining = (uint8_t)(remaining - 8u);
    } else {
      remaining = 0u;
    }
  }
}

static void derive_gateway_end_of_subnet(const picfw_loader_config_t *config,
                                         uint8_t out[4]) {
  uint8_t idx;
  uint8_t remaining;
  uint8_t tail_mask;

  if (config == 0 || out == 0) {
    return;
  }

  tail_mask = config->mask_len <= 24u
                  ? 0u
                  : mask_byte_for_bits((uint8_t)(config->mask_len - 24u));
  out[3] |= (uint8_t)(((uint8_t)(~tail_mask) ^ 0x1Fu) |
                      (config->gateway_bits & 0x1Fu));

  if (config->mask_len >= 24u) {
    return;
  }

  remaining = config->mask_len;
  for (idx = 0u; idx < 3u; ++idx) {
    uint8_t mask = mask_byte_for_bits(remaining);
    out[idx] |= (uint8_t)(~mask);
    if (remaining >= 8u) {
      remaining = (uint8_t)(remaining - 8u);
    } else {
      remaining = 0u;
    }
  }
}

static void derive_gateway(const picfw_loader_config_t *config, uint8_t out[4]) {
  if (config == 0 || out == 0) {
    return;
  }

  derive_network_base(config, out);
  if (config->gateway_bits == 0x3Fu) {
    out[3] |= 0x01u;
    return;
  }

  if ((config->gateway_bits & 0x20u) != 0u) {
    derive_gateway_end_of_subnet(config, out);
    return;
  }

  out[3] |= (config->gateway_bits & 0x1Fu);
}

static void derive_mac(const picfw_loader_config_t *config, uint8_t out[6]) {
  if (config == 0 || out == 0) {
    return;
  }

  out[0] = PICFW_ETH_MAC_OUI_0;
  out[1] = PICFW_ETH_MAC_OUI_1;
  out[2] = PICFW_ETH_MAC_OUI_2;

  if (config->use_mui) {
    out[3] = config->mui[0];
    out[4] = config->mui[2];
    out[5] = config->mui[4];
  } else {
    out[3] = config->settings[2];
    out[4] = config->settings[4];
    out[5] = config->settings[6];
  }
}

void picfw_loader_storage_init_default(picfw_loader_storage_t *storage) {
  if (storage == 0) {
    return;
  }

  memset(storage->config_space, 0xFF, sizeof(storage->config_space));
  storage->config_space[0] = 0xFFu;
  storage->config_space[1] = 0x3Fu;
  storage->config_space[2] = 0xFFu;
  storage->config_space[3] = 0x3Fu;
  storage->config_space[4] = 0xFFu;
  storage->config_space[5] = 0x27u;
  storage->config_space[6] = 0xFFu;
  storage->config_space[7] = 0x3Fu;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 0u] = 0x30u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 1u] = 0x31u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 2u] = 0x32u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 3u] = 0x33u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 4u] = 0x34u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 5u] = 0x35u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 6u] = 0x36u;
  storage->config_space[PICFW_LOADER_CONFIG_MUI_OFFSET + 7u] = 0x37u;
}

uint8_t picfw_loader_storage_read_block(const picfw_loader_storage_t *storage,
                                       uint16_t address, uint8_t *out,
                                       uint8_t len) {
  uint16_t available;
  uint8_t count;

  if (storage == 0 || out == 0 || len == 0u ||
      address >= PICFW_LOADER_CONFIG_SPACE_SIZE) {
    return 0u;
  }

  available = (uint16_t)(PICFW_LOADER_CONFIG_SPACE_SIZE - address);
  count = (uint8_t)(len < available ? len : available);
  memcpy(out, &storage->config_space[address], count);
  return count;
}

uint8_t picfw_loader_storage_write_block(picfw_loader_storage_t *storage,
                                        uint16_t address,
                                        const uint8_t *data, uint8_t len) {
  uint16_t available;
  uint8_t count;

  if (storage == 0 || data == 0 || len == 0u ||
      address >= PICFW_LOADER_CONFIG_SPACE_SIZE) {
    return 0u;
  }

  available = (uint16_t)(PICFW_LOADER_CONFIG_SPACE_SIZE - address);
  count = (uint8_t)(len < available ? len : available);
  memcpy(&storage->config_space[address], data, count);
  return count;
}

picfw_bool_t picfw_loader_config_decode(const picfw_loader_storage_t *storage,
                                        picfw_loader_config_t *out) {
  uint8_t idx;

  if (storage == 0 || out == 0) {
    return PICFW_FALSE;
  }

  memset(out, 0, sizeof(*out));
  picfw_loader_storage_read_block(storage, PICFW_LOADER_CONFIG_SETTINGS_OFFSET,
                                  out->settings,
                                  PICFW_LOADER_CONFIG_SETTINGS_LEN);
  picfw_loader_storage_read_block(storage, PICFW_LOADER_CONFIG_MUI_OFFSET,
                                  out->mui, PICFW_LOADER_CONFIG_MUI_LEN);

  for (idx = 0u; idx < 4u; ++idx) {
    out->user_id[idx] = out->settings[idx * 2u];
    out->static_ip[idx] = out->settings[idx * 2u];
  }

  out->use_mui = (picfw_bool_t)((out->settings[1] & 0x20u) != 0u);
  out->mask_len = (uint8_t)(out->settings[1] & 0x1Fu);
  out->gateway_bits = (uint8_t)(out->settings[7] & 0x3Fu);
  out->visual_ping = (picfw_bool_t)((out->settings[5] & 0x20u) != 0u);
  out->variant_mode = (uint8_t)(out->settings[5] & 0x03u);
  out->allow_hardware_jumpers =
      (picfw_bool_t)((out->settings[5] & 0x04u) != 0u);
  out->dhcp_enabled = (picfw_bool_t)(
      out->mask_len == 0x1Fu ||
      (uint8_t)(out->static_ip[0] | out->static_ip[1] | out->static_ip[2] |
                out->static_ip[3]) == 0u);
  if ((out->settings[3] & 0x3Fu) == 0x3Fu) {
    out->arbitration_delay_us = 200u;
  } else {
    out->arbitration_delay_us =
        (uint16_t)((uint16_t)(out->settings[3] & 0x3Fu) * 10u);
  }

  derive_subnet_mask(out->mask_len, out->subnet_mask);
  if (!out->dhcp_enabled) {
    derive_gateway(out, out->gateway_ip);
  }
  derive_mac(out, out->mac);
  out->valid = PICFW_TRUE;
  return PICFW_TRUE;
}

void picfw_loader_config_resolve_mode(
    const picfw_loader_config_t *config, uint8_t strap_variant,
    picfw_bool_t strap_enhanced_protocol, picfw_bool_t strap_high_speed,
    picfw_loader_effective_mode_t *out) {
  if (out == 0) {
    return;
  }

  out->variant = strap_variant;
  out->enhanced_protocol = strap_enhanced_protocol;
  out->high_speed = strap_high_speed;

  if (config == 0 || !config->valid || config->allow_hardware_jumpers) {
    return;
  }

  switch (config->variant_mode) {
    case PICFW_LOADER_VARIANT_ETHERNET:
      out->variant = PICFW_VARIANT_ETHERNET;
      out->enhanced_protocol = PICFW_TRUE;
      out->high_speed = PICFW_FALSE;
      break;
    case PICFW_LOADER_VARIANT_WIFI:
      out->variant = PICFW_VARIANT_WIFI;
      out->enhanced_protocol = PICFW_TRUE;
      out->high_speed = PICFW_FALSE;
      break;
    case PICFW_LOADER_VARIANT_RPI_USB_HIGH:
      out->variant = PICFW_VARIANT_RPI_USB;
      out->enhanced_protocol = PICFW_TRUE;
      out->high_speed = PICFW_TRUE;
      break;
    default:
      out->enhanced_protocol = PICFW_FALSE;
      break;
  }
}

uint8_t picfw_loader_effective_jumpers(
    const picfw_loader_effective_mode_t *mode) {
  uint8_t jumpers = 0x10u;

  if (mode == 0) {
    return jumpers;
  }

  if (mode->variant == PICFW_VARIANT_ETHERNET) {
    jumpers |= 0x04u;
  } else if (mode->variant == PICFW_VARIANT_WIFI) {
    jumpers |= 0x08u;
  }
  if (mode->high_speed) {
    jumpers |= 0x02u;
  }
  return jumpers;
}

void picfw_loader_config_to_ip_config(const picfw_loader_config_t *config,
                                      picfw_ip_config_t *out) {
  if (out == 0) {
    return;
  }

  memset(out, 0, sizeof(*out));
  if (config == 0 || !config->valid) {
    return;
  }

  memcpy(out->ip, config->static_ip, 4u);
  memcpy(out->mask, config->subnet_mask, 4u);
  memcpy(out->gateway, config->gateway_ip, 4u);
  out->dhcp_enabled = config->dhcp_enabled;
  out->valid = PICFW_TRUE;
}
