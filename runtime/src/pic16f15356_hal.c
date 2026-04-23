#include "picfw/pic16f15356_hal_internal.h"

#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(
    (PICFW_PIC16F15356_ISR_LATCH_CAP &
     (PICFW_PIC16F15356_ISR_LATCH_CAP - 1u)) == 0u,
    "ISR_LATCH_CAP must be power of 2");
_Static_assert(
    (PICFW_PIC16F15356_HOST_TX_STAGE_CAP &
     (PICFW_PIC16F15356_HOST_TX_STAGE_CAP - 1u)) == 0u,
    "HOST_TX_STAGE_CAP must be power of 2");
_Static_assert(
    sizeof(picfw_pic16f15356_registers_t) <= 64u,
    "registers struct grew beyond expected size");
#endif

static void ring_reset(uint8_t *head, uint8_t *tail, uint8_t *count) {
  if (head == 0 || tail == 0 || count == 0) {
    return;
  }

  *head = 0u;
  *tail = 0u;
  *count = 0u;
}

static picfw_bool_t ring_push(uint8_t *items, uint8_t cap, uint8_t *tail,
                              uint8_t *count, uint8_t value) {
  if (items == 0 || tail == 0 || count == 0 || *count >= cap) {
    return PICFW_FALSE;
  }

  items[*tail] = value;
  *tail = (uint8_t)((*tail + 1u) & (cap - 1u));
  (*count)++;
  return PICFW_TRUE;
}

static picfw_bool_t ring_pop(const uint8_t *items, uint8_t cap, uint8_t *head,
                             uint8_t *count, uint8_t *value) {
  if (items == 0 || head == 0 || count == 0 || value == 0 || *count == 0u) {
    return PICFW_FALSE;
  }

  *value = items[*head];
  *head = (uint8_t)((*head + 1u) & (cap - 1u));
  (*count)--;
  return PICFW_TRUE;
}

static uint8_t ring_free_space(uint8_t cap, uint8_t count) {
  return (uint8_t)(cap - count);
}

static void byte_fifo_init(picfw_pic16f15356_byte_fifo_t *fifo) {
  if (fifo == 0) {
    return;
  }

  ring_reset(&fifo->head, &fifo->tail, &fifo->count);
}

static picfw_bool_t byte_fifo_push(picfw_pic16f15356_byte_fifo_t *fifo,
                                   uint8_t value) {
  if (fifo == 0) {
    return PICFW_FALSE;
  }

  return ring_push(fifo->items, PICFW_PIC16F15356_ISR_LATCH_CAP, &fifo->tail,
                   &fifo->count, value);
}

static picfw_bool_t byte_fifo_pop(picfw_pic16f15356_byte_fifo_t *fifo,
                                  uint8_t *value) {
  if (fifo == 0) {
    return PICFW_FALSE;
  }

  return ring_pop(fifo->items, PICFW_PIC16F15356_ISR_LATCH_CAP, &fifo->head,
                  &fifo->count, value);
}

static void hal_init_latches(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  byte_fifo_init(&hal->latches.host_rx_fifo);
  byte_fifo_init(&hal->latches.bus_rx_fifo);
  byte_fifo_init(&hal->latches.host_tx_stage);
  hal->latches.tmr0_isr_count = 0u;
  hal->latches.scheduler_subticks = 0u;
  hal->latches.scheduler_pending = 0u;
  hal->latches.host_rx_overruns = 0u;
  hal->latches.bus_rx_overruns = 0u;
  hal->latches.host_tx_overruns = 0u;
  hal->latches.host_tx_ready = PICFW_FALSE;
  hal->latches.bus_tx_ready = PICFW_FALSE;
}

void picfw_pic16f15356_nvm_init(picfw_pic16f15356_nvm_t *nvm) {
  if (nvm == 0) {
    return;
  }

  memset(nvm, 0, sizeof(*nvm));
  picfw_eeprom_init(&nvm->eeprom);
  picfw_loader_storage_init_default(&nvm->loader_storage);
  nvm->initialized = PICFW_TRUE;
}

void picfw_pic16f15356_hal_reset(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  memset(hal, 0, sizeof(*hal));
  hal_init_latches(hal);
  picfw_pic16f15356_hal_profile_reset(hal);
  hal->current_fosc_hz = PICFW_PIC16F15356_RESET_FOSC_HZ;
  hal->uart_mode = PICFW_PIC16F15356_UART_MODE_DEFAULT;
}

static void hal_cache_loader_config(picfw_pic16f15356_hal_t *hal) {
  picfw_ip_config_t ip_config;

  if (hal == 0) {
    return;
  }

  picfw_loader_config_to_ip_config(&hal->loader_config, &ip_config);
  picfw_eeprom_write_ip_config(&hal->eeprom, &ip_config);
}

void picfw_pic16f15356_hal_set_uart_mode(
    picfw_pic16f15356_hal_t *hal, picfw_pic16f15356_uart_mode_t mode) {
  uint16_t bus_spbrg = PICFW_PIC16F15356_BUS_EUSART1_SPBRG;
  uint16_t host_spbrg;

  if (hal == 0) {
    return;
  }

  hal->regs.baud1con = PICFW_PIC16F15356_BUS_EUSART1_BAUD1CON_INIT;
  hal->regs.rc1sta = PICFW_PIC16F15356_BUS_EUSART1_RC1STA_INIT;
  hal->regs.tx1sta = PICFW_PIC16F15356_BUS_EUSART1_TX1STA_INIT;
  hal->regs.sp1brgl = (uint8_t)(bus_spbrg & 0x00FFu);
  hal->regs.sp1brgh = (uint8_t)((bus_spbrg >> 8) & 0x00FFu);
  hal->uart_mode = mode;

  if (mode == PICFW_PIC16F15356_UART_MODE_HIGH_SPEED) {
    host_spbrg = PICFW_PIC16F15356_HOST_EUSART2_HIGH_SPEED_SPBRG;
  } else {
    host_spbrg = PICFW_PIC16F15356_HOST_EUSART2_DEFAULT_SPBRG;
  }

  hal->regs.baud2con = PICFW_PIC16F15356_HOST_EUSART2_BAUD2CON_INIT;
  hal->regs.rc2sta = PICFW_PIC16F15356_HOST_EUSART2_RC2STA_INIT;
  hal->regs.tx2sta = PICFW_PIC16F15356_HOST_EUSART2_TX2STA_INIT;
  hal->regs.sp2brgl = (uint8_t)(host_spbrg & 0x00FFu);
  hal->regs.sp2brgh = (uint8_t)((host_spbrg >> 8) & 0x00FFu);
}

uint16_t picfw_pic16f15356_hal_current_bus_spbrg(
    const picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return 0u;
  }

  return (uint16_t)((uint16_t)hal->regs.sp1brgl |
                    ((uint16_t)hal->regs.sp1brgh << 8));
}

uint16_t picfw_pic16f15356_hal_current_host_spbrg(
    const picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return 0u;
  }

  return (uint16_t)((uint16_t)hal->regs.sp2brgl |
                    ((uint16_t)hal->regs.sp2brgh << 8));
}

void picfw_pic16f15356_hal_runtime_init_with_nvm(
    picfw_pic16f15356_hal_t *hal, picfw_pic16f15356_nvm_t *nvm) {
  picfw_pic16f15356_straps_t straps;
  picfw_loader_storage_t default_loader_storage;
  const picfw_loader_storage_t *loader_storage;
  picfw_bool_t pgc;
  picfw_bool_t pgd;

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

  picfw_pic16f15356_hal_profile_apply_power_on_defaults(hal);

  pgc = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_PIN_J11_PGC_PORT, PICFW_PIN_J11_PGC_BIT);
  pgd = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_PIN_J11_PGD_PORT, PICFW_PIN_J11_PGD_BIT);
  hal->bootloader_entry = (picfw_bool_t)(!pgc && !pgd);

  if (nvm != 0 && nvm->initialized != PICFW_FALSE) {
    hal->eeprom = nvm->eeprom;
    loader_storage = &nvm->loader_storage;
  } else {
    picfw_eeprom_init(&hal->eeprom);
    picfw_loader_storage_init_default(&default_loader_storage);
    loader_storage = &default_loader_storage;
  }

  picfw_pic16f15356_hal_read_straps(hal, &straps);
  picfw_loader_config_decode(loader_storage, &hal->loader_config);
  picfw_loader_config_resolve_mode(&hal->loader_config, straps.variant,
                                   straps.enhanced_protocol,
                                   straps.high_speed, &hal->loader_mode);
  hal->wifi_variant =
      (picfw_bool_t)(hal->loader_mode.variant == PICFW_VARIANT_WIFI);
  hal->ethernet_variant =
      (picfw_bool_t)(hal->loader_mode.variant == PICFW_VARIANT_ETHERNET);
  hal->wifi_ready = PICFW_FALSE;
  picfw_pic16f15356_hal_set_uart_mode(
      hal, hal->loader_mode.high_speed
               ? PICFW_PIC16F15356_UART_MODE_HIGH_SPEED
               : PICFW_PIC16F15356_UART_MODE_DEFAULT);

  hal->regs.trisa = PICFW_PIC16F15356_TRISA_INIT;
  hal->regs.trisb = PICFW_PIC16F15356_TRISB_INIT;
  hal->regs.trisc = PICFW_PIC16F15356_TRISC_INIT;
  hal->regs.ansela = PICFW_PIC16F15356_ANSELA_INIT;
  hal->regs.anselb = PICFW_PIC16F15356_ANSELB_INIT;
  hal->regs.anselc = PICFW_PIC16F15356_ANSELC_INIT;
  hal->regs.wpub = PICFW_PIC16F15356_WPUB_INIT;

  picfw_pic16f15356_hal_configure_pps(hal);

  picfw_led_init(&hal->led);
  hal_cache_loader_config(hal);
  picfw_w5500_init(&hal->w5500);

  if (hal->wifi_variant) {
    picfw_led_set_state(&hal->led, PICFW_LED_BLINK_SLOW, 0u);
  }
  picfw_ethernet_init(&hal->ethernet, hal->loader_mode.variant,
                      hal->loader_config.mac);

  if (nvm != 0) {
    nvm->eeprom = hal->eeprom;
    if (nvm->initialized == PICFW_FALSE) {
      nvm->loader_storage = *loader_storage;
    }
    nvm->initialized = PICFW_TRUE;
  }
}

void picfw_pic16f15356_hal_runtime_init(picfw_pic16f15356_hal_t *hal) {
  picfw_pic16f15356_hal_runtime_init_with_nvm(hal, 0);
}

picfw_bool_t picfw_pic16f15356_isr_latch_host_rx(picfw_pic16f15356_hal_t *hal,
                                                 uint8_t byte) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  if (!byte_fifo_push(&hal->latches.host_rx_fifo, byte)) {
    hal->latches.host_rx_overruns++;
    return PICFW_FALSE;
  }
  return PICFW_TRUE;
}

picfw_bool_t picfw_pic16f15356_isr_latch_bus_rx(picfw_pic16f15356_hal_t *hal,
                                                uint8_t byte) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  if (!byte_fifo_push(&hal->latches.bus_rx_fifo, byte)) {
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

static picfw_bool_t mainline_deliver_bytes(picfw_pic16f15356_hal_t *hal,
                                           picfw_runtime_t *runtime) {
  uint8_t processed;
  picfw_bool_t delivered = PICFW_FALSE;

  for (processed = 0u; processed < PICFW_PIC16F15356_MAINLINE_BYTE_BUDGET;
       ++processed) {
    uint8_t value;

    if (byte_fifo_pop(&hal->latches.host_rx_fifo, &value)) {
      if (!picfw_runtime_isr_enqueue_host_byte(runtime, value)) {
        hal->latches.host_rx_overruns++;
      } else {
        delivered = PICFW_TRUE;
      }
      continue;
    }

    if (byte_fifo_pop(&hal->latches.bus_rx_fifo, &value)) {
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

static void mainline_stage_host_tx(picfw_pic16f15356_hal_t *hal,
                                   picfw_runtime_t *runtime) {
  uint8_t buffer[PICFW_PIC16F15356_HOST_TX_STAGE_BUDGET];
  uint8_t free_space;
  size_t count;
  size_t idx;

  if (hal == 0 || runtime == 0) {
    return;
  }

  free_space = ring_free_space(PICFW_PIC16F15356_HOST_TX_STAGE_CAP,
                               hal->latches.host_tx_stage.count);
  if (free_space == 0u) {
    return;
  }

  if (free_space > PICFW_PIC16F15356_HOST_TX_STAGE_BUDGET) {
    free_space = PICFW_PIC16F15356_HOST_TX_STAGE_BUDGET;
  }

  count = picfw_runtime_drain_host_tx(runtime, buffer, free_space);
  for (idx = 0u; idx < count; ++idx) {
    if (!byte_fifo_push(&hal->latches.host_tx_stage, buffer[idx])) {
      hal->latches.host_tx_overruns += (uint32_t)(count - idx);
      break;
    }
  }
}

static picfw_bool_t service_host_tx_if_ready(picfw_pic16f15356_hal_t *hal) {
  uint8_t value;

  if (hal == 0 || !hal->latches.host_tx_ready) {
    return PICFW_FALSE;
  }
  if (!picfw_pic16f15356_hal_profile_host_tx_sink_ready(hal)) {
    return PICFW_FALSE;
  }
  if (!byte_fifo_pop(&hal->latches.host_tx_stage, &value)) {
    return PICFW_FALSE;
  }

  picfw_pic16f15356_hal_profile_emit_host_tx_byte(hal, value);
  hal->latches.host_tx_ready = PICFW_FALSE;
  return PICFW_TRUE;
}

static picfw_bool_t should_step_runtime(const picfw_runtime_t *runtime,
                                        picfw_bool_t delivered,
                                        picfw_bool_t advanced_time) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }

  return (picfw_bool_t)(delivered || advanced_time ||
                        runtime->event_queue.count > 0u);
}

static picfw_bool_t mainline_wifi_gate(picfw_pic16f15356_hal_t *hal,
                                       picfw_runtime_t *runtime) {
  if (!hal->wifi_variant || hal->wifi_ready) {
    return PICFW_FALSE;
  }

  if (runtime != 0) {
    (void)mainline_deliver_bytes(hal, runtime);
  }

  if (picfw_pic16f15356_hal_wifi_check(hal)) {
    hal->wifi_ready = PICFW_TRUE;
    picfw_led_set_state(&hal->led, PICFW_LED_FADE_UP, hal->runtime_now_ms);
    hal->led.prev_state = PICFW_LED_NORMAL;
  }
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

picfw_bool_t picfw_pic16f15356_mainline_service(picfw_pic16f15356_hal_t *hal,
                                                picfw_runtime_t *runtime) {
  picfw_bool_t delivered;
  picfw_bool_t advanced_time = PICFW_FALSE;

  if (hal == 0 || runtime == 0) {
    return PICFW_FALSE;
  }

  if (mainline_wifi_gate(hal, runtime)) {
    return PICFW_FALSE;
  }

  delivered = mainline_deliver_bytes(hal, runtime);

  if (hal->latches.scheduler_pending > 0u) {
    hal->latches.scheduler_pending--;
    hal->runtime_now_ms += PICFW_RUNTIME_PLATFORM_SCHEDULER_PERIOD_MS;
    advanced_time = PICFW_TRUE;
  }

  runtime->bus_busy = picfw_pic16f15356_hal_signal_detect(hal);

  if (!should_step_runtime(runtime, delivered, advanced_time)) {
    (void)service_host_tx_if_ready(hal);
    return PICFW_FALSE;
  }

  picfw_runtime_step(runtime, hal->runtime_now_ms);
  hal->runtime_step_count++;

  {
    uint8_t led_flags = 0u;
    picfw_bool_t led_out;
    if (runtime->last_error != 0u) {
      led_flags |= PICFW_LED_FLAG_ERROR;
      runtime->last_error = 0u;
    }
    led_out = picfw_led_service(&hal->led, hal->runtime_now_ms, led_flags);
    picfw_pic16f15356_hal_write_pin(hal, PICFW_PIN_LED2_PORT,
                                    PICFW_PIN_LED2_BIT, led_out);
  }

  if (hal->ethernet_variant) {
    picfw_ethernet_service(&hal->ethernet, &hal->w5500, &hal->eeprom,
                           &hal->led, hal->runtime_now_ms);
  }

  mainline_stage_host_tx(hal, runtime);
  (void)service_host_tx_if_ready(hal);
  hal->latches.bus_tx_ready = PICFW_FALSE;
  return PICFW_TRUE;
}

size_t picfw_pic16f15356_hal_drain_host_tx(picfw_pic16f15356_hal_t *hal,
                                           uint8_t *out, size_t out_cap) {
  return picfw_pic16f15356_hal_profile_drain_host_tx(hal, out, out_cap);
}

picfw_bool_t picfw_pic16f15356_hal_read_pin(
    const picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit) {
  if (hal == 0 || bit > 7u) {
    return PICFW_FALSE;
  }

  return picfw_pic16f15356_hal_profile_read_pin(hal, port, bit);
}

void picfw_pic16f15356_hal_write_pin(picfw_pic16f15356_hal_t *hal, uint8_t port,
                                     uint8_t bit, picfw_bool_t value) {
  if (hal == 0 || bit > 7u) {
    return;
  }

  picfw_pic16f15356_hal_profile_write_pin(hal, port, bit, value);
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

void picfw_pic16f15356_hal_configure_pps(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  hal->regs.rx1pps = PICFW_PPS_RB2_INPUT;
  hal->regs.rb3pps = PICFW_PPS_EUSART1_TX;
  hal->regs.rx2pps = PICFW_PPS_RC0_INPUT;
  hal->regs.rc1pps_out = PICFW_PPS_EUSART2_TX;
}

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

  protocol_pin = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_PROTOCOL_PORT, PICFW_STRAP_PROTOCOL_BIT);
  speed_pin = picfw_pic16f15356_hal_read_pin(hal, PICFW_STRAP_SPEED_PORT,
                                             PICFW_STRAP_SPEED_BIT);
  variant_a = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_VARIANT_PORT, PICFW_STRAP_VARIANT_BIT);
  variant_b = picfw_pic16f15356_hal_read_pin(
      hal, PICFW_STRAP_VARIANT2_PORT, PICFW_STRAP_VARIANT2_BIT);

  straps->enhanced_protocol = protocol_pin;
  straps->high_speed = (picfw_bool_t)(!speed_pin);

  if (!variant_a && !variant_b) {
    straps->variant = PICFW_VARIANT_ETHERNET;
  } else if (!variant_a) {
    straps->variant = PICFW_VARIANT_WIFI;
  } else {
    straps->variant = PICFW_VARIANT_RPI_USB;
  }
}

void picfw_pic16f15356_isr_latch_host_tx_ready(picfw_pic16f15356_hal_t *hal) {
  uint8_t value;

  if (hal == 0) {
    return;
  }
  hal->latches.host_tx_ready = PICFW_TRUE;
  if (hal->latches.host_tx_stage.count == 0u) {
    return;
  }
#if PICFW_HAL_ACTIVE_PROFILE == PICFW_HAL_PROFILE_SIM
  if (hal->profile.host_tx_outbox.count >= PICFW_RUNTIME_HOST_TX_CAP) {
    hal->latches.host_tx_overruns++;
    return;
  }
#else
  if (hal->profile.host_txreg_loaded != PICFW_FALSE) {
    return;
  }
#endif
  value = hal->latches.host_tx_stage.items[hal->latches.host_tx_stage.head];
  hal->latches.host_tx_stage.head = (uint8_t)(
      (hal->latches.host_tx_stage.head + 1u) &
      (PICFW_PIC16F15356_HOST_TX_STAGE_CAP - 1u));
  hal->latches.host_tx_stage.count--;
#if PICFW_HAL_ACTIVE_PROFILE == PICFW_HAL_PROFILE_SIM
  hal->regs.tx2reg = value; hal->profile.host_tx_outbox.items[hal->profile.host_tx_outbox.tail] = value;
  hal->profile.host_tx_outbox.tail = (uint8_t)(
      (hal->profile.host_tx_outbox.tail + 1u) &
      (PICFW_RUNTIME_HOST_TX_CAP - 1u)); hal->profile.host_tx_outbox.count++;
#else
  hal->regs.tx2reg = value; hal->profile.host_txreg_loaded = PICFW_TRUE;
#endif
  hal->latches.host_tx_ready = PICFW_FALSE;
}

void picfw_pic16f15356_isr_latch_bus_tx_ready(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }
  hal->latches.bus_tx_ready = PICFW_TRUE;
}
