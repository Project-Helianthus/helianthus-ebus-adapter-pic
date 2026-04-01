#include "picfw/pic16f15356_hal.h"

#include <string.h>

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
  fifo->tail = (uint8_t)((fifo->tail + 1u) % PICFW_PIC16F15356_ISR_LATCH_CAP);
  fifo->count++;
  return PICFW_TRUE;
}

static picfw_bool_t picfw_pic16f15356_byte_fifo_pop(picfw_pic16f15356_byte_fifo_t *fifo, uint8_t *value) {
  if (fifo == 0 || value == 0 || fifo->count == 0u) {
    return PICFW_FALSE;
  }

  *value = fifo->items[fifo->head];
  fifo->head = (uint8_t)((fifo->head + 1u) % PICFW_PIC16F15356_ISR_LATCH_CAP);
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
