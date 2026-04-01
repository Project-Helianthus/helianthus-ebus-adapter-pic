#include "picfw/led.h"

/* Number of LED states — must match the state defines in led.h */
#define PICFW_LED_STATE_COUNT 9u

/* --- Blink period lookup --- */

static uint32_t led_blink_period(uint8_t state) {
  switch (state) {
  case PICFW_LED_BLINK_FAST:      return PICFW_LED_BLINK_FAST_PERIOD_MS;
  case PICFW_LED_BLINK_VERY_FAST: return PICFW_LED_BLINK_VERY_FAST_PERIOD_MS;
  default:                        return PICFW_LED_BLINK_SLOW_PERIOD_MS;
  }
}

/* --- Per-state handlers (called via const dispatch table) --- */

static picfw_bool_t led_handler_off(picfw_led_t *led, uint32_t now_ms) {
  (void)led;
  (void)now_ms;
  return PICFW_FALSE;
}

static picfw_bool_t led_handler_blink(picfw_led_t *led, uint32_t now_ms) {
  if ((int32_t)(now_ms - led->deadline_ms) >= 0) {
    led->toggle = (uint8_t)(led->toggle ^ 1u);
    led->deadline_ms = now_ms + led_blink_period(led->state);
  }
  return (picfw_bool_t)(led->toggle);
}

static picfw_bool_t led_handler_fade(picfw_led_t *led, uint32_t now_ms) {
  if ((int32_t)(now_ms - led->deadline_ms) >= 0) {
    led->fade_step++;
    led->deadline_ms = now_ms + PICFW_LED_FADE_STEP_MS;
    if (led->fade_step >= PICFW_LED_FADE_STEPS) {
      led->state = led->prev_state;
      led->ping_deadline_ms = now_ms + PICFW_LED_PING_PERIOD_MS;
    }
  }
  return (picfw_bool_t)(led->fade_step > 0u);
}

static picfw_bool_t led_handler_steady(picfw_led_t *led, uint32_t now_ms) {
  if ((int32_t)(now_ms - led->ping_deadline_ms) >= 0) {
    led->prev_state = led->state;
    led->state = PICFW_LED_PING;
    led->deadline_ms = now_ms + PICFW_LED_PING_DURATION_MS;
    led->toggle = 1u;
  }
  return PICFW_TRUE;
}

static picfw_bool_t led_handler_transient(picfw_led_t *led, uint32_t now_ms) {
  if ((int32_t)(now_ms - led->deadline_ms) >= 0) {
    led->state = led->prev_state;
    led->ping_deadline_ms = now_ms + PICFW_LED_PING_PERIOD_MS;
  }
  return PICFW_TRUE;
}

/* --- Const dispatch table (R8 permitted pattern) --- */

typedef picfw_bool_t (*picfw_led_handler_t)(picfw_led_t *, uint32_t);

static const picfw_led_handler_t LED_HANDLERS[PICFW_LED_STATE_COUNT] = {
    led_handler_off,       /* 0 = OFF */
    led_handler_blink,     /* 1 = BLINK_SLOW */
    led_handler_blink,     /* 2 = BLINK_FAST */
    led_handler_blink,     /* 3 = BLINK_VERY_FAST */
    led_handler_fade,      /* 4 = FADE_UP */
    led_handler_steady,    /* 5 = LOW */
    led_handler_steady,    /* 6 = NORMAL */
    led_handler_transient, /* 7 = BRIGHT */
    led_handler_transient, /* 8 = PING */
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(LED_HANDLERS) / sizeof(LED_HANDLERS[0]) ==
                   PICFW_LED_STATE_COUNT,
               "LED_HANDLERS size must match PICFW_LED_STATE_COUNT");
#endif

/* --- Event flag handling (edge-triggered) --- */

static void led_handle_flags(picfw_led_t *led, uint32_t now_ms,
                              uint8_t flags) {
  if ((flags & PICFW_LED_FLAG_INIT_CMD) != 0u) {
    led->prev_state = led->state;
    led->state = PICFW_LED_BRIGHT;
    led->deadline_ms = now_ms + PICFW_LED_BRIGHT_INIT_MS;
    led->toggle = 1u;
  }
  if ((flags & PICFW_LED_FLAG_ERROR) != 0u) {
    led->prev_state = led->state;
    led->state = PICFW_LED_BRIGHT;
    led->deadline_ms = now_ms + PICFW_LED_BRIGHT_ERROR_MS;
    led->toggle = 1u;
  }
}

/* --- Public API --- */

void picfw_led_init(picfw_led_t *led) {
  if (led == 0) {
    return;
  }
  led->state = PICFW_LED_OFF;
  led->prev_state = PICFW_LED_OFF;
  led->toggle = 0u;
  led->fade_step = 0u;
  led->deadline_ms = 0u;
  led->ping_deadline_ms = 0u;
}

void picfw_led_set_state(picfw_led_t *led, uint8_t state, uint32_t now_ms) {
  if (led == 0) {
    return;
  }
  led->state = state;
  led->toggle = 0u;

  switch (state) {
  case PICFW_LED_BLINK_SLOW:
  case PICFW_LED_BLINK_FAST:
  case PICFW_LED_BLINK_VERY_FAST:
    led->deadline_ms = now_ms + led_blink_period(state);
    break;
  case PICFW_LED_FADE_UP:
    led->fade_step = 0u;
    led->deadline_ms = now_ms + PICFW_LED_FADE_STEP_MS;
    break;
  case PICFW_LED_NORMAL:
  case PICFW_LED_LOW:
    led->ping_deadline_ms = now_ms + PICFW_LED_PING_PERIOD_MS;
    break;
  default:
    break;
  }
}

picfw_bool_t picfw_led_service(picfw_led_t *led, uint32_t now_ms,
                               uint8_t flags) {
  if (led == 0) {
    return PICFW_FALSE;
  }

  led_handle_flags(led, now_ms, flags);

  if (led->state >= PICFW_LED_STATE_COUNT) {
    return PICFW_FALSE;
  }

  return LED_HANDLERS[led->state](led, now_ms);
}
