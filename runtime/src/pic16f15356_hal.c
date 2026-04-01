#include "picfw/pic16f15356_hal.h"

#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(
    (PICFW_PIC16F15356_ISR_LATCH_CAP &
     (PICFW_PIC16F15356_ISR_LATCH_CAP - 1u)) == 0u,
    "ISR_LATCH_CAP must be power of 2 for bitmask indexing");
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

  if (mode == PICFW_PIC16F15356_UART_MODE_HIGH_SPEED) {
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

  /* Simulation: default signal-detect high (bus present) */
  hal->latches.portb_input = 0x02u; /* RB1=1 (signal present) */
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

picfw_bool_t picfw_pic16f15356_mainline_service(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime) {
  picfw_bool_t delivered;
  picfw_bool_t advanced_time = PICFW_FALSE;

  if (hal == 0 || runtime == 0) {
    return PICFW_FALSE;
  }

  delivered = picfw_pic16f15356_mainline_deliver_bytes(hal, runtime);

  if (hal->latches.scheduler_pending > 0u) {
    hal->latches.scheduler_pending--;
    hal->runtime_now_ms += PICFW_RUNTIME_PLATFORM_SCHEDULER_PERIOD_MS;
    advanced_time = PICFW_TRUE;
  }

  if (!delivered && !advanced_time) {
    return PICFW_FALSE;
  }

  picfw_runtime_step(runtime, hal->runtime_now_ms);
  hal->runtime_step_count++;
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

/* --- GPIO pin read/write (simulation model) --- */

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

  straps->enhanced_protocol = protocol_pin;  /* high = enhanced */
  straps->high_speed = (picfw_bool_t)(!speed_pin); /* low = high-speed */

  /* Variant decode: both high=RPi/USB(0), A low=WIFI(1), both low=Ethernet(2) */
  if (!variant_a && !variant_b) {
    straps->variant = 2u; /* Ethernet */
  } else if (!variant_a) {
    straps->variant = 1u; /* WIFI */
  } else {
    straps->variant = 0u; /* RPi/USB */
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
