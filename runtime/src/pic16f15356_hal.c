#include "picfw/pic16f15356_hal.h"

#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(
    (PICFW_PIC16F15356_ISR_LATCH_CAP &
     (PICFW_PIC16F15356_ISR_LATCH_CAP - 1u)) == 0u,
    "ISR_LATCH_CAP must be power of 2 for bitmask indexing");
_Static_assert(
    sizeof(picfw_pic16f15356_registers_t) <= 48u,
    "registers struct grew beyond expected size — review new fields");
#endif

static void picfw_pic16f15356_byte_fifo_init(picfw_pic16f15356_byte_fifo_t *fifo) {
  if (fifo == 0) {
    return;
  }

  fifo->head = 0u;
  fifo->tail = 0u;
  fifo->count = 0u;
}

static picfw_bool_t picfw_pic16f15356_byte_fifo_push(picfw_pic16f15356_byte_fifo_t *fifo, uint8_t value) {
  if (fifo == 0 || fifo->count >= PICFW_PIC16F15356_ISR_LATCH_CAP) {
    return PICFW_FALSE;
  }

  fifo->items[fifo->tail] = value;
  fifo->tail = (uint8_t)((fifo->tail + 1u) & (PICFW_PIC16F15356_ISR_LATCH_CAP - 1u));
  fifo->count++;
  return PICFW_TRUE;
}

static picfw_bool_t picfw_pic16f15356_byte_fifo_pop(picfw_pic16f15356_byte_fifo_t *fifo, uint8_t *value) {
  if (fifo == 0 || value == 0 || fifo->count == 0u) {
    return PICFW_FALSE;
  }

  *value = fifo->items[fifo->head];
  fifo->head = (uint8_t)((fifo->head + 1u) & (PICFW_PIC16F15356_ISR_LATCH_CAP - 1u));
  fifo->count--;
  return PICFW_TRUE;
}

static void picfw_pic16f15356_hal_init_latches(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  picfw_pic16f15356_byte_fifo_init(&hal->latches.host_rx_fifo);
  picfw_pic16f15356_byte_fifo_init(&hal->latches.bus_rx_fifo);
  picfw_pic16f15356_byte_fifo_init(&hal->latches.host_tx_fifo);
  hal->latches.tmr0_isr_count = 0u;
  hal->latches.scheduler_subticks = 0u;
  hal->latches.scheduler_pending = 0u;
  hal->latches.host_rx_overruns = 0u;
  hal->latches.bus_rx_overruns = 0u;
  hal->latches.host_tx_overruns = 0u;
  /* Simulated port inputs and TX-ready flags (test harness sets these) */
  hal->latches.porta_input = 0u;
  hal->latches.portb_input = 0u;
  hal->latches.portc_input = 0u;
  hal->latches.host_tx_ready = PICFW_FALSE;
  hal->latches.bus_tx_ready = PICFW_FALSE;
}

void picfw_pic16f15356_hal_reset(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  memset(hal, 0, sizeof(*hal));
  picfw_pic16f15356_hal_init_latches(hal);
  hal->current_fosc_hz = PICFW_PIC16F15356_RESET_FOSC_HZ;
  hal->uart_mode = PICFW_PIC16F15356_UART_MODE_DEFAULT;
}

void picfw_pic16f15356_hal_set_uart_mode(picfw_pic16f15356_hal_t *hal, picfw_pic16f15356_uart_mode_t mode) {
  uint16_t spbrg;

  if (hal == 0) {
    return;
  }

  hal->regs.baud1con = PICFW_PIC16F15356_APP_EUSART_BAUD1CON_INIT;
  hal->regs.rc1sta = PICFW_PIC16F15356_APP_EUSART_RC1STA_INIT;
  hal->regs.tx1sta = PICFW_PIC16F15356_APP_EUSART_TX1STA_INIT;
  hal->uart_mode = mode;

  if (mode == PICFW_PIC16F15356_UART_MODE_VERY_HIGH_SPEED) {
    spbrg = PICFW_PIC16F15356_APP_EUSART_VERY_HIGH_SPEED_SPBRG;
  } else if (mode == PICFW_PIC16F15356_UART_MODE_HIGH_SPEED) {
    spbrg = PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_SPBRG;
  } else {
    spbrg = PICFW_PIC16F15356_APP_EUSART_DEFAULT_SPBRG;
  }

  hal->regs.sp1brgl = (uint8_t)(spbrg & 0x00FFu);
  hal->regs.sp1brgh = (uint8_t)((spbrg >> 8) & 0x00FFu);

  /* EUSART2 (host UART) mirrors EUSART1 baud rate and control */
  hal->regs.baud2con = PICFW_PIC16F15356_APP_EUSART2_BAUD2CON_INIT;
  hal->regs.rc2sta = PICFW_PIC16F15356_APP_EUSART2_RC2STA_INIT;
  hal->regs.tx2sta = PICFW_PIC16F15356_APP_EUSART2_TX2STA_INIT;
  hal->regs.sp2brgl = hal->regs.sp1brgl;
  hal->regs.sp2brgh = hal->regs.sp1brgh;
}

uint16_t picfw_pic16f15356_hal_current_spbrg(const picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return 0u;
  }

  return (uint16_t)((uint16_t)hal->regs.sp1brgl | ((uint16_t)hal->regs.sp1brgh << 8));
}

void picfw_pic16f15356_hal_runtime_init(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  picfw_pic16f15356_hal_reset(hal);
  hal->regs.oscccon1 = PICFW_PIC16F15356_APP_OSCCON1_RUNTIME_INIT;
  hal->regs.oscfrq = PICFW_PIC16F15356_APP_OSCFRQ_RUNTIME_INIT;
  hal->regs.osctune = 0u;
  hal->regs.t0con1 = PICFW_PIC16F15356_TMR0_T0CON1_INIT;
  hal->regs.t0con0 = PICFW_PIC16F15356_TMR0_T0CON0_INIT;
  hal->regs.tmr0h = PICFW_PIC16F15356_TMR0_PERIOD_REG;
  hal->regs.tmr0l = 0u;
  hal->current_fosc_hz = PICFW_PIC16F15356_RUN_FOSC_HZ;
  picfw_pic16f15356_hal_set_uart_mode(hal, PICFW_PIC16F15356_UART_MODE_DEFAULT);

  /* GPIO direction and analog/digital select (from schematic) */
  hal->regs.trisa = PICFW_PIC16F15356_TRISA_INIT;
  hal->regs.trisb = PICFW_PIC16F15356_TRISB_INIT;
  hal->regs.trisc = PICFW_PIC16F15356_TRISC_INIT;
  hal->regs.ansela = PICFW_PIC16F15356_ANSELA_INIT;
  hal->regs.anselb = PICFW_PIC16F15356_ANSELB_INIT;
  hal->regs.anselc = PICFW_PIC16F15356_ANSELC_INIT;
  hal->regs.wpub = PICFW_PIC16F15356_WPUB_INIT;

  /* PPS routing for EUSART1/2 */
  picfw_pic16f15356_hal_configure_pps(hal);

  /* Initialize LED, EEPROM, and W5500 */
  picfw_led_init(&hal->led);
  picfw_eeprom_init(&hal->eeprom);
  picfw_w5500_init(&hal->w5500);

  /* Simulation defaults: pull-up high on strap and signal-detect inputs */
  hal->latches.porta_input = 0x33u; /* RA0,RA1,RA4,RA5 high (straps open) */
  hal->latches.portb_input = 0xC2u; /* RB1=1(signal), RB6=1(PGC), RB7=1(PGD) */

  /* J11 bootloader entry detection: read RB6 (PGC) + RB7 (PGD).
   * Both LOW (shorted by J11 jumper) = enter bootloader mode.
   * Note: in the simulation, this runs after PPS config (which does not
   * touch RB6/RB7).  On real hardware, the read is the first GPIO
   * operation at POR, before any peripheral configuration.
   * On real hardware, this is the first GPIO read at POR.  In simulation,
   * portb_input defaults to 0xC2 (normal boot, RB6+RB7 HIGH).  Tests
   * verify the detection logic via direct read_pin calls after init. */
  {
    picfw_bool_t pgc = picfw_pic16f15356_hal_read_pin(
        hal, PICFW_PIN_J11_PGC_PORT, PICFW_PIN_J11_PGC_BIT);
    picfw_bool_t pgd = picfw_pic16f15356_hal_read_pin(
        hal, PICFW_PIN_J11_PGD_PORT, PICFW_PIN_J11_PGD_BIT);
    hal->bootloader_entry = (picfw_bool_t)(!pgc && !pgd);
  }

  /* Determine WiFi variant from straps and set initial LED state */
  {
    picfw_pic16f15356_straps_t straps;
    picfw_pic16f15356_hal_read_straps(hal, &straps);
    hal->wifi_variant = (picfw_bool_t)(straps.variant == PICFW_VARIANT_WIFI);
    hal->wifi_ready = PICFW_FALSE;
    if (hal->wifi_variant) {
      picfw_led_set_state(&hal->led, PICFW_LED_BLINK_SLOW, 0u);
    }
  }
}

picfw_bool_t picfw_pic16f15356_isr_latch_host_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  if (!picfw_pic16f15356_byte_fifo_push(&hal->latches.host_rx_fifo, byte)) {
    hal->latches.host_rx_overruns++;
    return PICFW_FALSE;
  }
  return PICFW_TRUE;
}

picfw_bool_t picfw_pic16f15356_isr_latch_bus_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  if (!picfw_pic16f15356_byte_fifo_push(&hal->latches.bus_rx_fifo, byte)) {
    hal->latches.bus_rx_overruns++;
    return PICFW_FALSE;
  }
  return PICFW_TRUE;
}

void picfw_pic16f15356_isr_latch_tmr0(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  hal->latches.tmr0_isr_count++;
  hal->latches.scheduler_subticks++;
  if (hal->latches.scheduler_subticks >= PICFW_PIC16F15356_TMR0_ISR_DIVIDER) {
    hal->latches.scheduler_subticks = 0u;
    if (hal->latches.scheduler_pending != UINT16_MAX) {
      hal->latches.scheduler_pending++;
    }
  }
}

static picfw_bool_t picfw_pic16f15356_mainline_deliver_bytes(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime) {
  uint8_t processed;
  picfw_bool_t delivered = PICFW_FALSE;

  for (processed = 0u; processed < PICFW_PIC16F15356_MAINLINE_BYTE_BUDGET; ++processed) {
    uint8_t value;

    if (picfw_pic16f15356_byte_fifo_pop(&hal->latches.host_rx_fifo, &value)) {
      if (!picfw_runtime_isr_enqueue_host_byte(runtime, value)) {
        hal->latches.host_rx_overruns++;
      } else {
        delivered = PICFW_TRUE;
      }
      continue;
    }

    if (picfw_pic16f15356_byte_fifo_pop(&hal->latches.bus_rx_fifo, &value)) {
      if (!picfw_runtime_isr_enqueue_bus_byte(runtime, value)) {
        hal->latches.bus_rx_overruns++;
      } else {
        delivered = PICFW_TRUE;
      }
      continue;
    }

    break;
  }

  return delivered;
}

static void picfw_pic16f15356_mainline_flush_host_tx(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime) {
  uint8_t buffer[PICFW_RUNTIME_DRAIN_BUDGET];
  size_t idx;
  size_t count = picfw_runtime_drain_host_tx(runtime, buffer, sizeof(buffer));

  for (idx = 0u; idx < count && idx < PICFW_RUNTIME_DRAIN_BUDGET; ++idx) {
    if (!picfw_pic16f15356_byte_fifo_push(&hal->latches.host_tx_fifo, buffer[idx])) {
      hal->latches.host_tx_overruns += (uint32_t)(count - idx);
      break;
    }
  }
}

/* WiFi startup gate: blink LED until Wemos drives RB0 HIGH.
 * Returns TRUE if the gate is active (skip runtime processing). */
static picfw_bool_t mainline_wifi_gate(picfw_pic16f15356_hal_t *hal) {
  if (!hal->wifi_variant || hal->wifi_ready) {
    return PICFW_FALSE; /* gate not active */
  }

  if (picfw_pic16f15356_hal_wifi_check(hal)) {
    hal->wifi_ready = PICFW_TRUE;
    picfw_led_set_state(&hal->led, PICFW_LED_FADE_UP, hal->runtime_now_ms);
    hal->led.prev_state = PICFW_LED_NORMAL;
  }
  /* Service LED even while waiting (for blink animation) */
  if (hal->latches.scheduler_pending > 0u) {
    hal->latches.scheduler_pending--;
    hal->runtime_now_ms += PICFW_RUNTIME_PLATFORM_SCHEDULER_PERIOD_MS;
  }
  {
    picfw_bool_t led_out =
        picfw_led_service(&hal->led, hal->runtime_now_ms, 0u);
    picfw_pic16f15356_hal_write_pin(hal, PICFW_PIN_LED2_PORT,
                                     PICFW_PIN_LED2_BIT, led_out);
  }
  return (picfw_bool_t)(!hal->wifi_ready);
}

picfw_bool_t picfw_pic16f15356_mainline_service(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime) {
  picfw_bool_t delivered;
  picfw_bool_t advanced_time = PICFW_FALSE;

  if (hal == 0 || runtime == 0) {
    return PICFW_FALSE;
  }

  if (mainline_wifi_gate(hal)) {
    return PICFW_FALSE;
  }

  delivered = picfw_pic16f15356_mainline_deliver_bytes(hal, runtime);

  if (hal->latches.scheduler_pending > 0u) {
    hal->latches.scheduler_pending--;
    hal->runtime_now_ms += PICFW_RUNTIME_PLATFORM_SCHEDULER_PERIOD_MS;
    advanced_time = PICFW_TRUE;
  }

  /* Always sample bus-busy signal so the field is current even on idle cycles.
   * RB1 HIGH = another device transmitting = defer frame emission. */
  runtime->bus_busy = picfw_pic16f15356_hal_signal_detect(hal);

  if (!delivered && !advanced_time) {
    return PICFW_FALSE;
  }

  picfw_runtime_step(runtime, hal->runtime_now_ms);
  hal->runtime_step_count++;
  /* Consume TX-ready flags (set by ISR, cleared here each mainline cycle).
   * Ordering: flags are cleared BEFORE flush because the simulation model
   * pushes to an intermediate FIFO, not directly to TXREG.  On real hardware,
   * the TX mechanism will be redesigned as interrupt-driven byte-by-byte
   * TXREG writes where the ISR re-sets the flag after each shift-out. */
  hal->latches.host_tx_ready = PICFW_FALSE;
  hal->latches.bus_tx_ready = PICFW_FALSE;

  /* LED state machine: derive flags from runtime state, service, write pin */
  {
    uint8_t led_flags = 0u;
    picfw_bool_t led_out;
    if (runtime->last_error != 0u) {
      led_flags |= PICFW_LED_FLAG_ERROR;
      runtime->last_error = 0u; /* consume error edge */
    }
    led_out = picfw_led_service(&hal->led, hal->runtime_now_ms, led_flags);
    picfw_pic16f15356_hal_write_pin(hal, PICFW_PIN_LED2_PORT,
                                     PICFW_PIN_LED2_BIT, led_out);
  }

  picfw_pic16f15356_mainline_flush_host_tx(hal, runtime);
  return PICFW_TRUE;
}

size_t picfw_pic16f15356_hal_drain_host_tx(picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap) {
  size_t out_len = 0u;

  if (hal == 0 || out == 0) {
    return 0u;
  }

  while (out_len < out_cap && hal->latches.host_tx_fifo.count > 0u) {
    if (!picfw_pic16f15356_byte_fifo_pop(&hal->latches.host_tx_fifo, &out[out_len])) {
      break;
    }
    out_len++;
  }

  return out_len;
}

/* --- GPIO pin read/write (simulation model) ---
 *
 * Simulation note: read_pin always returns portX_input regardless of TRIS
 * direction, and write_pin always writes to LATx regardless of TRIS.
 * On real PIC16F hardware, reading a PORT register for an output pin returns
 * the actual pin level (normally matches LAT), and writing LAT on an input
 * pin is a valid "pre-staging" operation.  The simulation intentionally
 * decouples input stimuli (portX_input, set by test harness) from output
 * state (latX, set by write_pin).  On the real hardware port, read_pin
 * will be replaced with direct SFR reads (e.g. PORTAbits.RA4). */

picfw_bool_t picfw_pic16f15356_hal_read_pin(const picfw_pic16f15356_hal_t *hal,
                                             uint8_t port, uint8_t bit) {
  uint8_t port_val;

  if (hal == 0 || bit > 7u) {
    return PICFW_FALSE;
  }

  switch (port) {
  case PICFW_PORT_A:
    port_val = hal->latches.porta_input;
    break;
  case PICFW_PORT_B:
    port_val = hal->latches.portb_input;
    break;
  case PICFW_PORT_C:
    port_val = hal->latches.portc_input;
    break;
  default:
    return PICFW_FALSE;
  }

  return (picfw_bool_t)((port_val >> bit) & 1u);
}

void picfw_pic16f15356_hal_write_pin(picfw_pic16f15356_hal_t *hal,
                                      uint8_t port, uint8_t bit,
                                      picfw_bool_t value) {
  uint8_t *lat;

  if (hal == 0 || bit > 7u) {
    return;
  }

  switch (port) {
  case PICFW_PORT_A: lat = &hal->regs.lata; break;
  case PICFW_PORT_B: lat = &hal->regs.latb; break;
  case PICFW_PORT_C: lat = &hal->regs.latc; break;
  default: return;
  }

  if (value) {
    *lat = (uint8_t)(*lat | (1u << bit));
  } else {
    *lat = (uint8_t)(*lat & ~(1u << bit));
  }
}

picfw_bool_t picfw_pic16f15356_hal_signal_detect(
    const picfw_pic16f15356_hal_t *hal) {
  return picfw_pic16f15356_hal_read_pin(
      hal, PICFW_PIN_SIGNAL_DETECT_PORT, PICFW_PIN_SIGNAL_DETECT_BIT);
}

picfw_bool_t picfw_pic16f15356_hal_wifi_check(
    const picfw_pic16f15356_hal_t *hal) {
  return picfw_pic16f15356_hal_read_pin(
      hal, PICFW_PIN_WIFI_CHECK_PORT, PICFW_PIN_WIFI_CHECK_BIT);
}

/* --- PPS configuration --- */

void picfw_pic16f15356_hal_configure_pps(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  /* EUSART1 RX: RB2 → RX1 input */
  hal->regs.rx1pps = PICFW_PPS_RB2_INPUT;
  /* EUSART1 TX: RB3 → TX1 output */
  hal->regs.rb3pps = PICFW_PPS_EUSART1_TX;
  /* EUSART2 RX: RC0 → RX2 input */
  hal->regs.rx2pps = PICFW_PPS_RC0_INPUT;
  /* EUSART2 TX: RC1 → TX2 output */
  hal->regs.rc1pps_out = PICFW_PPS_EUSART2_TX;
}

/* --- J12 AUX strap read --- */

void picfw_pic16f15356_hal_read_straps(const picfw_pic16f15356_hal_t *hal,
                                        picfw_pic16f15356_straps_t *straps) {
  picfw_bool_t protocol_pin;
  picfw_bool_t speed_pin;
  picfw_bool_t variant_a;
  picfw_bool_t variant_b;

  if (straps != 0) {
    straps->enhanced_protocol = PICFW_FALSE;
    straps->high_speed = PICFW_FALSE;
    straps->variant = 0u;
  }
  if (hal == 0 || straps == 0) {
    return;
  }

  /* Active-low: open (pulled high) = feature enabled */
  protocol_pin = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_PROTOCOL_PORT, PICFW_STRAP_PROTOCOL_BIT);
  speed_pin = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_SPEED_PORT, PICFW_STRAP_SPEED_BIT);
  variant_a = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_VARIANT_PORT, PICFW_STRAP_VARIANT_BIT);
  variant_b = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_VARIANT2_PORT, PICFW_STRAP_VARIANT2_BIT);

  /* J12 strap polarity — asymmetry is hardware design intent:
   * - Protocol (Pin 2/RA4): "open = enhanced" — pull-up HIGH = feature active.
   *   Enhanced is the safe default for ebusd users, so direct mapping.
   * - Speed (Pin 7/RA5): "open = normal-speed" — pull-up HIGH = safe default.
   *   High-speed requires deliberate grounding, so inverted mapping. */
  straps->enhanced_protocol = protocol_pin;            /* HIGH = enhanced */
  straps->high_speed = (picfw_bool_t)(!speed_pin);     /* LOW  = high-speed */

  /* Variant decode from J12 Pin 5 (RA0/RA1):
   *   both high (open)     = RPi/USB  (0) — default, no jumper
   *   A low, B high        = WIFI     (1) — Pin 5 to Pin 4 (v3.0)
   *   both low             = Ethernet (2) — Pin 5 to GND
   *   A high, B low        = impossible physical state (J12 wiring pulls
   *                          both pins together); falls to RPi/USB default */
  if (!variant_a && !variant_b) {
    straps->variant = PICFW_VARIANT_ETHERNET;
  } else if (!variant_a) {
    straps->variant = PICFW_VARIANT_WIFI;
  } else {
    straps->variant = PICFW_VARIANT_RPI_USB;
  }
}

/* --- TX ISR handlers (EUSART TX register empty) --- */

void picfw_pic16f15356_isr_latch_host_tx_ready(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }
  hal->latches.host_tx_ready = PICFW_TRUE;
}

void picfw_pic16f15356_isr_latch_bus_tx_ready(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }
  hal->latches.bus_tx_ready = PICFW_TRUE;
}
