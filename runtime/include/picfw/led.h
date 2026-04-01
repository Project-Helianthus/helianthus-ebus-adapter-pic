#ifndef PICFW_LED_H
#define PICFW_LED_H

#include "common.h"

/* LED2 (Info Blue) state machine for PIC16F15356 eBUS adapter.
 *
 * Behavior from picfirmware.md (eBUS/adapter repo):
 * - Power-on: OFF (LED dark)
 * - WiFi variant waiting for Wemos: BLINK_SLOW (1 Hz)
 * - Wemos ready / non-WiFi boot: FADE_UP (gradual brightness ramp)
 * - Enhanced protocol active: NORMAL brightness
 * - Standard protocol (enhanced disabled): LOW brightness
 * - INIT command received: BRIGHT for 2 seconds
 * - Protocol error: BRIGHT for 5 seconds
 * - Sign-of-life: brief PING pulse every 4 seconds
 * - Ethernet link wait: BLINK_FAST (5 Hz)
 * - DHCP negotiation: BLINK_VERY_FAST (10 Hz) */

/* LED state identifiers */
#define PICFW_LED_OFF             0u
#define PICFW_LED_BLINK_SLOW      1u  /* 1 Hz toggle (WiFi wait) */
#define PICFW_LED_BLINK_FAST      2u  /* 5 Hz toggle (Ethernet link wait) */
#define PICFW_LED_BLINK_VERY_FAST 3u  /* 10 Hz toggle (DHCP) */
#define PICFW_LED_FADE_UP         4u  /* gradual brightness ramp */
#define PICFW_LED_LOW             5u  /* standard protocol (dim) */
#define PICFW_LED_NORMAL          6u  /* enhanced protocol (normal) */
#define PICFW_LED_BRIGHT          7u  /* init flash (2s) or error flash (5s) */
#define PICFW_LED_PING            8u  /* brief bright pulse (sign-of-life) */

/* Timing constants (milliseconds) */
#define PICFW_LED_BLINK_SLOW_PERIOD_MS      500u
#define PICFW_LED_BLINK_FAST_PERIOD_MS      100u
#define PICFW_LED_BLINK_VERY_FAST_PERIOD_MS  50u
#define PICFW_LED_BRIGHT_INIT_MS           2000u
#define PICFW_LED_BRIGHT_ERROR_MS          5000u
#define PICFW_LED_PING_PERIOD_MS           4000u
#define PICFW_LED_PING_DURATION_MS          100u
#define PICFW_LED_FADE_STEP_MS               50u
#define PICFW_LED_FADE_STEPS                 16u

/* Event flags (bitmask, passed to led_service) */
#define PICFW_LED_FLAG_INIT_CMD   0x01u  /* INIT command just received */
#define PICFW_LED_FLAG_ERROR      0x02u  /* protocol error active */

typedef struct picfw_led {
  uint8_t state;            /* current LED state (PICFW_LED_*) */
  uint8_t prev_state;       /* return-to state after BRIGHT or PING */
  uint8_t toggle;           /* blink toggle (0 or 1) */
  uint8_t fade_step;        /* fade-up counter (0..FADE_STEPS) */
  uint32_t deadline_ms;     /* timeout for current transient state */
  uint32_t ping_deadline_ms;/* next scheduled sign-of-life pulse */
} picfw_led_t;

/* Initialize LED to OFF state. */
void picfw_led_init(picfw_led_t *led);

/* Set LED to a specific state. Resets deadline for transient states. */
void picfw_led_set_state(picfw_led_t *led, uint8_t state, uint32_t now_ms);

/* Service the LED state machine. Called once per mainline cycle.
 * Returns the output pin value (PICFW_TRUE = on, PICFW_FALSE = off).
 * flags: bitmask of PICFW_LED_FLAG_* for edge-triggered events. */
picfw_bool_t picfw_led_service(picfw_led_t *led, uint32_t now_ms,
                               uint8_t flags);

#endif
