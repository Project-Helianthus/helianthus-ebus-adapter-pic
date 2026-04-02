#include "picfw/ethernet.h"

#include <string.h>

/* Apply IP/mask/gateway to W5500 hardware registers. */
static void apply_ip_config(picfw_ethernet_t *eth, picfw_w5500_t *w5500) {
  picfw_w5500_set_mac(w5500, eth->mac);
  picfw_w5500_set_ip(w5500, eth->ip);
  picfw_w5500_set_subnet(w5500, eth->mask);
  picfw_w5500_set_gateway(w5500, eth->gateway);
}

/* Open TCP listen socket on ebusd port (3333). Returns TRUE on success. */
static picfw_bool_t open_tcp_listen(picfw_w5500_t *w5500) {
  if (!picfw_w5500_socket_open_tcp(w5500, PICFW_W5500_EBUSD_PORT)) {
    return PICFW_FALSE;
  }
  return picfw_w5500_socket_listen(w5500);
}

/* Apply config and transition to TCP_LISTEN or ERROR. */
static void apply_and_listen(picfw_ethernet_t *eth, picfw_w5500_t *w5500,
                             picfw_led_t *led, uint32_t now_ms) {
  apply_ip_config(eth, w5500);
  if (open_tcp_listen(w5500)) {
    eth->state = PICFW_ETH_STATE_TCP_LISTEN;
    picfw_led_set_state(led, PICFW_LED_FADE_UP, now_ms);
  } else {
    eth->state = PICFW_ETH_STATE_ERROR;
  }
}

/* Load simulation DHCP defaults into eth IP fields. */
static void dhcp_sim_load_defaults(picfw_ethernet_t *eth) {
  eth->ip[0] = PICFW_ETH_DHCP_SIM_IP_0;
  eth->ip[1] = PICFW_ETH_DHCP_SIM_IP_1;
  eth->ip[2] = PICFW_ETH_DHCP_SIM_IP_2;
  eth->ip[3] = PICFW_ETH_DHCP_SIM_IP_3;
  eth->mask[0] = PICFW_ETH_DHCP_SIM_MASK_0;
  eth->mask[1] = PICFW_ETH_DHCP_SIM_MASK_1;
  eth->mask[2] = PICFW_ETH_DHCP_SIM_MASK_2;
  eth->mask[3] = PICFW_ETH_DHCP_SIM_MASK_3;
  eth->gateway[0] = PICFW_ETH_DHCP_SIM_GW_0;
  eth->gateway[1] = PICFW_ETH_DHCP_SIM_GW_1;
  eth->gateway[2] = PICFW_ETH_DHCP_SIM_GW_2;
  eth->gateway[3] = PICFW_ETH_DHCP_SIM_GW_3;
}

/* Simplified DHCP service (simulation model).
 * Real DHCP would use W5500 UDP socket for DISCOVER/OFFER/REQUEST/ACK.
 * Simulation: immediately transitions IDLE -> DISCOVERING -> BOUND with
 * default IP config, then opens TCP listen socket. */
static void dhcp_service(picfw_ethernet_t *eth, picfw_w5500_t *w5500,
                         picfw_led_t *led, uint32_t now_ms) {
  switch (eth->dhcp_state) {
  case PICFW_ETH_DHCP_IDLE:
    eth->dhcp_state = PICFW_ETH_DHCP_DISCOVERING;
    eth->deadline_ms = now_ms + PICFW_ETH_DHCP_TIMEOUT_MS;
    picfw_led_set_state(led, PICFW_LED_BLINK_VERY_FAST, now_ms);
    break;

  case PICFW_ETH_DHCP_DISCOVERING:
    eth->dhcp_state = PICFW_ETH_DHCP_BOUND;
    dhcp_sim_load_defaults(eth);
    break;

  case PICFW_ETH_DHCP_REQUESTING:
    eth->dhcp_state = PICFW_ETH_DHCP_BOUND;
    break;

  case PICFW_ETH_DHCP_BOUND:
    apply_and_listen(eth, w5500, led, now_ms);
    break;

  default:
    eth->state = PICFW_ETH_STATE_ERROR;
    break;
  }
}

/* Handle LINK_WAIT state: poll PHY, decide DHCP vs fixed IP. */
static void service_link_wait(picfw_ethernet_t *eth,
                              const picfw_w5500_t *w5500,
                              const picfw_eeprom_t *eeprom, picfw_led_t *led,
                              uint32_t now_ms) {
  picfw_ip_config_t ip_cfg;
  picfw_bool_t have_config = PICFW_FALSE;
  memset(&ip_cfg, 0, sizeof(ip_cfg));

  picfw_led_set_state(led, PICFW_LED_BLINK_FAST, now_ms);
  if (!picfw_w5500_link_up(w5500)) {
    return;
  }

  if (eeprom != 0) {
    have_config = picfw_eeprom_read_ip_config(eeprom, &ip_cfg);
  }

  if (have_config && ip_cfg.valid && !ip_cfg.dhcp_enabled) {
    eth->state = PICFW_ETH_STATE_FIXED_IP;
    eth->dhcp_enabled = PICFW_FALSE;
  } else {
    eth->state = PICFW_ETH_STATE_DHCP;
    eth->dhcp_state = PICFW_ETH_DHCP_IDLE;
    eth->dhcp_enabled = PICFW_TRUE;
    eth->retry_count = 0u;
  }
}

/* Handle FIXED_IP state: read EEPROM, apply config, open TCP. */
static void service_fixed_ip(picfw_ethernet_t *eth, picfw_w5500_t *w5500,
                             const picfw_eeprom_t *eeprom, picfw_led_t *led,
                             uint32_t now_ms) {
  picfw_ip_config_t ip_cfg;
  picfw_bool_t ok = PICFW_FALSE;

  if (eeprom != 0) {
    ok = picfw_eeprom_read_ip_config(eeprom, &ip_cfg);
  }

  if (ok && ip_cfg.valid) {
    memcpy(eth->ip, ip_cfg.ip, 4u);
    memcpy(eth->mask, ip_cfg.mask, 4u);
    memcpy(eth->gateway, ip_cfg.gateway, 4u);
    apply_and_listen(eth, w5500, led, now_ms);
  } else {
    eth->state = PICFW_ETH_STATE_ERROR;
  }
}

/* Handle TCP_LISTEN state: poll for incoming connection. */
static void service_tcp_listen(picfw_ethernet_t *eth,
                               const picfw_w5500_t *w5500) {
  if (picfw_w5500_socket_status(w5500) == PICFW_W5500_SN_SR_ESTABLISHED) {
    eth->state = PICFW_ETH_STATE_TCP_CONNECTED;
  }
}

/* Handle TCP_CONNECTED state: detect peer disconnect, re-listen. */
static void service_tcp_connected(picfw_ethernet_t *eth,
                                  picfw_w5500_t *w5500) {
  if (picfw_w5500_socket_status(w5500) != PICFW_W5500_SN_SR_CLOSE_WAIT) {
    return;
  }
  picfw_w5500_socket_close(w5500);
  if (open_tcp_listen(w5500)) {
    eth->state = PICFW_ETH_STATE_TCP_LISTEN;
  } else {
    eth->state = PICFW_ETH_STATE_ERROR;
  }
}

void picfw_ethernet_init(picfw_ethernet_t *eth, uint8_t variant) {
  if (eth == 0) {
    return;
  }

  memset(eth, 0, sizeof(*eth));

  if (variant == PICFW_VARIANT_ETHERNET) {
    eth->state = PICFW_ETH_STATE_LINK_WAIT;
    {
      uint8_t seed[3] = {0x01u, 0x00u, 0x01u};
      picfw_ethernet_derive_mac(seed, eth->mac);
    }
  } else {
    eth->state = PICFW_ETH_STATE_DISABLED;
  }
}

void picfw_ethernet_service(picfw_ethernet_t *eth, picfw_w5500_t *w5500,
                            const picfw_eeprom_t *eeprom, picfw_led_t *led,
                            uint32_t now_ms) {
  if (eth == 0 || w5500 == 0 || led == 0) {
    return;
  }

  switch (eth->state) {
  case PICFW_ETH_STATE_LINK_WAIT:
    service_link_wait(eth, w5500, eeprom, led, now_ms);
    break;
  case PICFW_ETH_STATE_DHCP:
    dhcp_service(eth, w5500, led, now_ms);
    break;
  case PICFW_ETH_STATE_FIXED_IP:
    service_fixed_ip(eth, w5500, eeprom, led, now_ms);
    break;
  case PICFW_ETH_STATE_TCP_LISTEN:
    service_tcp_listen(eth, w5500);
    break;
  case PICFW_ETH_STATE_TCP_CONNECTED:
    service_tcp_connected(eth, w5500);
    break;
  default:
    /* DISABLED, ERROR, or invalid state: no-op */
    break;
  }
}

void picfw_ethernet_derive_mac(const uint8_t seed[3], uint8_t mac_out[6]) {
  if (seed == 0 || mac_out == 0) {
    return;
  }

  mac_out[0] = PICFW_ETH_MAC_OUI_0;
  mac_out[1] = PICFW_ETH_MAC_OUI_1;
  mac_out[2] = PICFW_ETH_MAC_OUI_2;
  mac_out[3] = seed[0];
  mac_out[4] = seed[1];
  mac_out[5] = seed[2];
}
