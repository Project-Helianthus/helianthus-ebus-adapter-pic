#ifndef PICFW_ETHERNET_H
#define PICFW_ETHERNET_H

#include "common.h"
#include "eeprom.h"
#include "eeprom_layout.h"
#include "led.h"
#include "platform_model.h"
#include "w5500.h"

/* Ethernet stack state machine for PIC16F15356 eBUS adapter.
 *
 * States:
 *   DISABLED    — non-Ethernet variant, stack inactive
 *   LINK_WAIT   — waiting for PHY link (cable plugged in)
 *   DHCP        — negotiating IP via DHCP (simplified for simulation)
 *   FIXED_IP    — applying static IP from EEPROM
 *   TCP_LISTEN  — socket open, waiting for ebusd TCP connection
 *   TCP_CONNECTED — ebusd connected, data flows via ENH protocol
 *   ERROR       — unrecoverable error (bad config, HW fault)
 *
 * LED integration:
 *   LINK_WAIT → BLINK_FAST (5 Hz)
 *   DHCP      → BLINK_VERY_FAST (10 Hz)
 *   TCP_LISTEN/CONNECTED → caller manages LED (FADE_UP → NORMAL) */

/* Ethernet state identifiers */
#define PICFW_ETH_STATE_DISABLED      0u
#define PICFW_ETH_STATE_LINK_WAIT     1u
#define PICFW_ETH_STATE_DHCP          2u
#define PICFW_ETH_STATE_FIXED_IP      3u
#define PICFW_ETH_STATE_TCP_LISTEN    4u
#define PICFW_ETH_STATE_TCP_CONNECTED 5u
#define PICFW_ETH_STATE_ERROR         6u

/* DHCP sub-state identifiers (simplified for simulation) */
#define PICFW_ETH_DHCP_IDLE        0u
#define PICFW_ETH_DHCP_DISCOVERING 1u
#define PICFW_ETH_DHCP_REQUESTING  2u
#define PICFW_ETH_DHCP_BOUND       3u

/* DHCP timeout (ms) — retry deadline for each DHCP phase */
#define PICFW_ETH_DHCP_TIMEOUT_MS  5000u

/* Maximum DHCP retry attempts before falling back to error */
#define PICFW_ETH_DHCP_MAX_RETRIES 3u

/* MAC OUI prefix: AE:B0:53 (locally administered, unicast) */
#define PICFW_ETH_MAC_OUI_0 0xAEu
#define PICFW_ETH_MAC_OUI_1 0xB0u
#define PICFW_ETH_MAC_OUI_2 0x53u

/* Default DHCP-simulation IP config (used when real DHCP is not implemented) */
#define PICFW_ETH_DHCP_SIM_IP_0   192u
#define PICFW_ETH_DHCP_SIM_IP_1   168u
#define PICFW_ETH_DHCP_SIM_IP_2   1u
#define PICFW_ETH_DHCP_SIM_IP_3   200u
#define PICFW_ETH_DHCP_SIM_MASK_0 255u
#define PICFW_ETH_DHCP_SIM_MASK_1 255u
#define PICFW_ETH_DHCP_SIM_MASK_2 255u
#define PICFW_ETH_DHCP_SIM_MASK_3 0u
#define PICFW_ETH_DHCP_SIM_GW_0   192u
#define PICFW_ETH_DHCP_SIM_GW_1   168u
#define PICFW_ETH_DHCP_SIM_GW_2   1u
#define PICFW_ETH_DHCP_SIM_GW_3   1u

typedef struct picfw_ethernet {
  uint8_t state;         /* PICFW_ETH_STATE_* */
  uint8_t dhcp_state;    /* PICFW_ETH_DHCP_* (only valid when state==DHCP) */
  uint8_t retry_count;   /* DHCP retry counter */
  uint32_t deadline_ms;  /* timeout for current phase */
  uint8_t mac[6];        /* derived MAC address */
  uint8_t ip[4];         /* assigned IP (DHCP result or EEPROM static) */
  uint8_t mask[4];       /* subnet mask */
  uint8_t gateway[4];    /* default gateway */
  picfw_bool_t dhcp_enabled; /* from EEPROM config */
} picfw_ethernet_t;

/* Initialize ethernet state based on platform variant.
 * If variant == PICFW_VARIANT_ETHERNET, sets state to LINK_WAIT.
 * Otherwise, sets state to DISABLED. */
void picfw_ethernet_init(picfw_ethernet_t *eth, uint8_t variant);

/* Service the ethernet state machine. Called once per mainline cycle.
 * Drives W5500 link detection, DHCP, IP configuration, and TCP listen.
 * Updates LED state for link-wait and DHCP phases. */
void picfw_ethernet_service(picfw_ethernet_t *eth, picfw_w5500_t *w5500,
                            const picfw_eeprom_t *eeprom, picfw_led_t *led,
                            uint32_t now_ms);

/* Derive a locally-administered MAC from 3 seed bytes.
 * Result: AE:B0:53:seed[0]:seed[1]:seed[2]
 * Writes 6 bytes into mac_out. */
void picfw_ethernet_derive_mac(const uint8_t seed[3], uint8_t mac_out[6]);

#endif
