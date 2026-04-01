#include "picfw/pic16f15356_app.h"
#include "picfw/pic16f15356_hal.h"
#include "picfw/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *name, const char *message) {
  fprintf(stderr, "[FAIL] %s: %s\n", name, message);
  return 1;
}

static int expect_true(const char *name, picfw_bool_t condition,
                       const char *message) {
  if (!condition) {
    return fail(name, message);
  }
  return 0;
}

static int expect_bytes(const char *name, const uint8_t *actual,
                        const uint8_t *expected, size_t len,
                        const char *message) {
  if (memcmp(actual, expected, len) != 0) {
    return fail(name, message);
  }
  return 0;
}

static size_t enqueue_enh_frame(picfw_runtime_t *runtime, uint8_t command,
                                uint8_t data) {
  uint8_t encoded[2];
  size_t encoded_len =
      picfw_enh_encode(command, data, encoded, sizeof(encoded));
  size_t idx;

  for (idx = 0u; idx < encoded_len; ++idx) {
    if (!picfw_runtime_isr_enqueue_host_byte(runtime, encoded[idx])) {
      return 0u;
    }
  }
  return encoded_len;
}

static size_t collect_frames(uint8_t *bytes, size_t byte_len,
                             picfw_enh_frame_t *frames, size_t frame_cap) {
  picfw_enh_parser_t parser;
  size_t frame_count = 0u;
  size_t idx;

  picfw_enh_parser_init(&parser);
  for (idx = 0u; idx < byte_len; ++idx) {
    picfw_enh_frame_t frame;
    picfw_enh_parse_result_t result =
        picfw_enh_parser_feed(&parser, bytes[idx], &frame);
    if (result == PICFW_ENH_PARSE_ERROR) {
      return 0u;
    }
    if (result == PICFW_ENH_PARSE_COMPLETE) {
      if (frame_count >= frame_cap) {
        return 0u;
      }
      frames[frame_count++] = frame;
    }
  }

  return frame_count;
}

static int test_runtime_init_and_info(void) {
  const char *name = "runtime_init_and_info";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[16];
  size_t tx_len;
  size_t frame_count;

  picfw_runtime_init(&runtime, 0);
  if (expect_true(name, runtime.startup_state == PICFW_STARTUP_BOOT_INIT,
                  "startup state")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u) == 2u,
                  "init enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 0u);

  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "init response count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                  "init response command")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == runtime.config.init_features,
                  "init response features")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INFO,
                                    PICFW_ADAPTER_INFO_VERSION) == 2u,
                  "info enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 1u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 9u, "version response frame count")) {
    return 1;
  }
  if (expect_true(
          name, frames[0].command == PICFW_ENH_RES_INFO && frames[0].data == 8u,
          "version length")) {
    return 1;
  }
  if (expect_true(name,
                  frames[1].command == PICFW_ENH_RES_INFO &&
                      frames[1].data == 0x03u,
                  "version payload[0]")) {
    return 1;
  }
  if (expect_true(name,
                  frames[8].command == PICFW_ENH_RES_INFO &&
                      frames[8].data == 0xFEu,
                  "version payload[7]")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INFO,
                                    PICFW_ADAPTER_INFO_RESET_INFO) == 2u,
                  "reset enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 3u, "reset response frame count")) {
    return 1;
  }
  if (expect_true(name, frames[1].data == 0x05u && frames[2].data == 0x01u,
                  "reset payload")) {
    return 1;
  }

  return 0;
}

static int test_pic16f15356_platform_model(void) {
  const char *name = "pic16f15356_platform_model";
  uint32_t default_baud;
  uint32_t high_speed_baud;

  if (expect_true(name, PICFW_PIC16F15356_RESET_FOSC_HZ == 1000000u,
                  "reset fosc")) {
    return 1;
  }
  if (expect_true(name, PICFW_PIC16F15356_RUN_FOSC_HZ == 32000000u,
                  "run fosc")) {
    return 1;
  }
  if (expect_true(name, PICFW_PIC16F15356_TMR0_T0CON1_INIT == 0x44u,
                  "tmr0 t0con1")) {
    return 1;
  }
  if (expect_true(name, PICFW_PIC16F15356_TMR0_T0CON0_INIT == 0x80u,
                  "tmr0 t0con0")) {
    return 1;
  }
  if (expect_true(name, picfw_pic16f15356_tmr0_isr_period_us() == 500u,
                  "tmr0 isr period")) {
    return 1;
  }
  if (expect_true(name, picfw_pic16f15356_scheduler_period_ms() == 100u,
                  "coarse scheduler period")) {
    return 1;
  }

  default_baud = picfw_pic16f15356_app_eusart_async_baud(
      PICFW_PIC16F15356_APP_EUSART_DEFAULT_SPBRG);
  if (expect_true(name, default_baud == 9604u, "default uart actual baud")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_within_percent(
                      default_baud,
                      PICFW_PIC16F15356_APP_EUSART_DEFAULT_BAUD_NOMINAL,
                      1u) != PICFW_FALSE,
                  "default uart nominal tolerance")) {
    return 1;
  }

  high_speed_baud = picfw_pic16f15356_app_eusart_async_baud(
      PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_SPBRG);
  if (expect_true(name, high_speed_baud == 115942u,
                  "high-speed uart actual baud")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_within_percent(
                      high_speed_baud,
                      PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_BAUD_NOMINAL,
                      1u) != PICFW_FALSE,
                  "high-speed uart nominal tolerance")) {
    return 1;
  }

  return 0;
}

static int test_pic16f15356_hal_scaffold(void) {
  const char *name = "pic16f15356_hal_scaffold";
  picfw_pic16f15356_hal_t hal;
  picfw_runtime_t runtime;
  uint8_t encoded[2];
  uint8_t tx[32];
  picfw_enh_frame_t frames[4];
  size_t encoded_len;
  size_t frame_count;
  size_t idx;

  picfw_pic16f15356_hal_reset(&hal);
  if (expect_true(name, hal.current_fosc_hz == PICFW_PIC16F15356_RESET_FOSC_HZ,
                  "reset clock")) {
    return 1;
  }
  if (expect_true(name, picfw_pic16f15356_hal_current_spbrg(&hal) == 0u,
                  "reset spbrg")) {
    return 1;
  }

  picfw_pic16f15356_hal_runtime_init(&hal);
  if (expect_true(name, hal.current_fosc_hz == PICFW_PIC16F15356_RUN_FOSC_HZ,
                  "runtime clock")) {
    return 1;
  }
  if (expect_true(
          name, hal.regs.oscccon1 == PICFW_PIC16F15356_APP_OSCCON1_RUNTIME_INIT,
          "runtime osccon1")) {
    return 1;
  }
  if (expect_true(name,
                  hal.regs.oscfrq == PICFW_PIC16F15356_APP_OSCFRQ_RUNTIME_INIT,
                  "runtime oscfrq")) {
    return 1;
  }
  if (expect_true(name, hal.regs.t0con1 == PICFW_PIC16F15356_TMR0_T0CON1_INIT,
                  "runtime t0con1")) {
    return 1;
  }
  if (expect_true(name, hal.regs.tmr0h == PICFW_PIC16F15356_TMR0_PERIOD_REG,
                  "runtime tmr0h")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_pic16f15356_hal_current_spbrg(&hal) ==
                      PICFW_PIC16F15356_APP_EUSART_DEFAULT_SPBRG,
                  "default runtime spbrg")) {
    return 1;
  }

  picfw_pic16f15356_hal_set_uart_mode(&hal,
                                      PICFW_PIC16F15356_UART_MODE_HIGH_SPEED);
  if (expect_true(name,
                  picfw_pic16f15356_hal_current_spbrg(&hal) ==
                      PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_SPBRG,
                  "high-speed runtime spbrg")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_pic16f15356_app_eusart_async_baud(
                      picfw_pic16f15356_hal_current_spbrg(&hal)) == 115942u,
                  "high-speed runtime baud")) {
    return 1;
  }
  picfw_pic16f15356_hal_set_uart_mode(&hal,
                                      PICFW_PIC16F15356_UART_MODE_DEFAULT);

  picfw_runtime_init(&runtime, 0);
  encoded_len =
      picfw_enh_encode(PICFW_ENH_REQ_INIT, 0x11u, encoded, sizeof(encoded));
  if (expect_true(name, encoded_len == 2u, "encoded init length")) {
    return 1;
  }
  for (idx = 0u; idx < encoded_len; ++idx) {
    if (expect_true(name,
                    picfw_pic16f15356_isr_latch_host_rx(&hal, encoded[idx]) ==
                        PICFW_TRUE,
                    "host rx latch")) {
      return 1;
    }
  }
  if (expect_true(name, runtime.event_queue.count == 0u,
                  "isr should not touch runtime queue")) {
    return 1;
  }
  if (expect_true(name, runtime.startup_state == PICFW_STARTUP_BOOT_INIT,
                  "runtime unchanged before service")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_pic16f15356_mainline_service(&hal, &runtime) ==
                      PICFW_TRUE,
                  "mainline service")) {
    return 1;
  }
  if (expect_true(name, runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                  "runtime advanced by mainline")) {
    return 1;
  }
  if (expect_true(name, hal.runtime_step_count == 1u && runtime.now_ms == 0u,
                  "mainline step count")) {
    return 1;
  }

  frame_count = collect_frames(
      tx, picfw_pic16f15356_hal_drain_host_tx(&hal, tx, sizeof(tx)), frames,
      sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "hal tx frame count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                  "hal tx frame command")) {
    return 1;
  }

  for (idx = 0u; idx < PICFW_PIC16F15356_TMR0_ISR_DIVIDER - 1u; ++idx) {
    picfw_pic16f15356_isr_latch_tmr0(&hal);
  }
  if (expect_true(name, hal.latches.scheduler_pending == 0u,
                  "coarse tick not armed early")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_pic16f15356_mainline_service(&hal, &runtime) ==
                      PICFW_FALSE,
                  "no-op service")) {
    return 1;
  }

  picfw_pic16f15356_isr_latch_tmr0(&hal);
  if (expect_true(name, hal.latches.scheduler_pending == 1u,
                  "coarse tick armed")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_pic16f15356_mainline_service(&hal, &runtime) ==
                      PICFW_TRUE,
                  "timer-driven service")) {
    return 1;
  }
  if (expect_true(name, hal.runtime_now_ms == 100u && runtime.now_ms == 100u,
                  "coarse time advance")) {
    return 1;
  }
  if (expect_true(name, hal.runtime_step_count == 2u,
                  "runtime stepped on coarse tick")) {
    return 1;
  }

  return 0;
}

static int test_pic16f15356_app_shell(void) {
  const char *name = "pic16f15356_app_shell";
  picfw_pic16f15356_app_t app;
  uint8_t encoded[2];
  uint8_t tx[32];
  picfw_enh_frame_t frames[4];
  size_t encoded_len;
  size_t frame_count;
  size_t idx;

  picfw_pic16f15356_app_init(&app, 0);
  if (expect_true(name,
                  app.hal.current_fosc_hz == PICFW_PIC16F15356_RUN_FOSC_HZ,
                  "app init clock")) {
    return 1;
  }
  if (expect_true(name, app.runtime.startup_state == PICFW_STARTUP_BOOT_INIT,
                  "app init runtime state")) {
    return 1;
  }

  encoded_len =
      picfw_enh_encode(PICFW_ENH_REQ_INIT, 0x44u, encoded, sizeof(encoded));
  if (expect_true(name, encoded_len == 2u, "app encoded init length")) {
    return 1;
  }
  for (idx = 0u; idx < encoded_len; ++idx) {
    if (expect_true(name,
                    picfw_pic16f15356_app_isr_host_rx(&app, encoded[idx]) ==
                        PICFW_TRUE,
                    "app host latch")) {
      return 1;
    }
  }
  if (expect_true(name,
                  picfw_pic16f15356_app_mainline_service(&app) != PICFW_FALSE,
                  "app mainline init")) {
    return 1;
  }

  frame_count = collect_frames(
      tx, picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx)), frames,
      sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "app init frame count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                  "app init frame")) {
    return 1;
  }

  for (idx = 0u; idx < PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++idx) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  if (expect_true(name,
                  picfw_pic16f15356_app_mainline_service(&app) != PICFW_FALSE,
                  "app timer service")) {
    return 1;
  }
  if (expect_true(
          name, app.runtime.now_ms == 100u && app.hal.runtime_step_count == 2u,
          "app coarse tick")) {
    return 1;
  }

  return 0;
}

static int test_runtime_start_send_and_boundary_release(void) {
  const char *name = "runtime_start_send_and_boundary_release";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;

  picfw_runtime_init(&runtime, 0);
  if (!picfw_runtime_isr_enqueue_bus_byte(&runtime, 0x41u)) {
    return fail(name, "bus enqueue");
  }
  picfw_runtime_step(&runtime, 10u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == 1u && tx[0] == 0x41u,
                  "short-form bus echo")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x42u) == 2u,
                  "start enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 11u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "unsupported response count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_STARTED,
                  "start response command")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == 0x42u, "start response initiator")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x31u) == 2u,
                  "send enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 12u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "send echo count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_RECEIVED,
                  "send echo command")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == 0x31u, "send echo data")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND,
                                    PICFW_RUNTIME_SYN_BYTE) == 2u,
                  "sync send enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 13u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "sync echo count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_RECEIVED,
                  "sync echo command")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == PICFW_RUNTIME_SYN_BYTE,
                  "sync echo data")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x33u) == 2u,
                  "post-sync send enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 14u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "post-sync error count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_ERROR_HOST,
                  "post-sync error command")) {
    return 1;
  }
  if (expect_true(name,
                  frames[0].data == PICFW_RUNTIME_ERROR_SEND_WITHOUT_SESSION,
                  "post-sync error code")) {
    return 1;
  }
  return 0;
}

static int test_runtime_start_cancel_and_failure_injection(void) {
  const char *name = "runtime_start_cancel_and_failure_injection";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x21u) == 2u,
                  "start enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 20u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(
          name, frame_count == 1u && frames[0].command == PICFW_ENH_RES_STARTED,
          "initial start")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START,
                                    PICFW_RUNTIME_SYN_BYTE) == 2u,
                  "cancel enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 21u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == 0u, "cancel should not emit response")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x44u) == 2u,
                  "send after cancel")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 22u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name,
                  frame_count == 1u &&
                      frames[0].command == PICFW_ENH_RES_ERROR_HOST,
                  "send after cancel error")) {
    return 1;
  }
  if (expect_true(name,
                  frames[0].data == PICFW_RUNTIME_ERROR_SEND_WITHOUT_SESSION,
                  "send after cancel code")) {
    return 1;
  }

  picfw_runtime_config_init_default(&config);
  config.start_should_fail = PICFW_TRUE;
  config.start_failure_winner = 0x77u;
  picfw_runtime_init(&runtime, &config);

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x45u) == 2u,
                  "failed start enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 23u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name, frame_count == 1u, "failed start response count")) {
    return 1;
  }
  if (expect_true(name, frames[0].command == PICFW_ENH_RES_FAILED,
                  "failed start command")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == 0x77u, "failed start winner")) {
    return 1;
  }

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x55u) == 2u,
                  "send after failed start")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 24u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name,
                  frame_count == 1u &&
                      frames[0].command == PICFW_ENH_RES_ERROR_HOST,
                  "send after failed start error")) {
    return 1;
  }
  if (expect_true(name,
                  frames[0].data == PICFW_RUNTIME_ERROR_SEND_WITHOUT_SESSION,
                  "send after failed start code")) {
    return 1;
  }

  return 0;
}

static int test_runtime_periodic_status_and_state(void) {
  const char *name = "runtime_periodic_status_and_state";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[4];
  size_t tx_len;
  size_t frame_count;
  static const uint8_t expected_snapshot[] = {
      0x63u, 0x82u, 0x53u, 0x63u, 0x35u, 0x01u, 0x07u, 0x3Du,
      0x07u, 0x01u, 0x03u, 0x06u, 0xF4u, 0x24u, 0x3Cu, 0x56u,
      0x0Cu, 0xAAu, '2',   '4',   '3',   'c',   '5',   '6',
  };
  static const uint8_t expected_variant[] = {
      0x63u, 0x82u, 0x53u, 0x63u, 0x37u, 0x01u, 0x07u, 0x3Du,
      0x07u, 0x01u, 0x03u, 0x06u, 0xF4u, 0x24u, 0x3Cu, 0x56u,
      0x0Cu, 0xAAu, '2',   '4',   '3',   'c',   '5',   '6',
  };

  picfw_runtime_config_init_default(&config);
  config.status_emit_enabled = PICFW_TRUE;
  config.status_snapshot_period_ms = 5u;
  config.status_variant_period_ms = 9u;
  picfw_runtime_init(&runtime, &config);

  if (expect_true(name,
                  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x01u) == 2u,
                  "init enqueue")) {
    return 1;
  }
  picfw_runtime_step(&runtime, 0u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  if (expect_true(name,
                  frame_count == 1u &&
                      frames[0].command == PICFW_ENH_RES_RESETTED,
                  "init response")) {
    return 1;
  }
  if (expect_true(name, frames[0].data == runtime.config.init_features,
                  "init response features")) {
    return 1;
  }
  if (expect_true(name, runtime.protocol_state == PICFW_PROTOCOL_STATE_READY,
                  "protocol state after init")) {
    return 1;
  }
  if (expect_true(name, runtime.status_snapshot_deadline_ms == 5u,
                  "snapshot deadline armed")) {
    return 1;
  }
  if (expect_true(name, runtime.status_variant_deadline_ms == 9u,
                  "variant deadline armed")) {
    return 1;
  }

  picfw_runtime_step(&runtime, 4u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == 0u, "no periodic output before deadline")) {
    return 1;
  }

  picfw_runtime_step(&runtime, 5u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == sizeof(expected_snapshot),
                  "snapshot byte count")) {
    return 1;
  }
  if (expect_bytes(name, tx, expected_snapshot, sizeof(expected_snapshot),
                   "snapshot bytes")) {
    return 1;
  }
  if (expect_true(name, runtime.protocol_state == PICFW_PROTOCOL_STATE_SCAN,
                  "protocol state after snapshot")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.status_snapshot_count == 1u &&
                      runtime.status_tick_count == 1u,
                  "snapshot counters")) {
    return 1;
  }
  if (expect_true(name, runtime.status_snapshot_deadline_ms == 10u,
                  "snapshot deadline rearmed")) {
    return 1;
  }

  picfw_runtime_step(&runtime, 9u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == sizeof(expected_variant),
                  "variant byte count")) {
    return 1;
  }
  if (expect_bytes(name, tx, expected_variant, sizeof(expected_variant),
                   "variant bytes")) {
    return 1;
  }
  if (expect_true(name, runtime.protocol_state == PICFW_PROTOCOL_STATE_SCAN,
                  "protocol state after variant")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.status_variant_count == 1u &&
                      runtime.status_tick_count == 2u,
                  "variant counters")) {
    return 1;
  }
  if (expect_true(name, runtime.status_variant_deadline_ms == 18u,
                  "variant deadline rearmed")) {
    return 1;
  }
  if (expect_true(name, runtime.status_variant_selector == 0u,
                  "variant selector unchanged")) {
    return 1;
  }

  return 0;
}

static int test_deadline_wrap_compare(void) {
  const char *name = "deadline_wrap_compare";
  if (expect_true(name,
                  picfw_deadline_reached_u32(0x00000010u, 0xFFFFFFF0u) ==
                      PICFW_TRUE,
                  "wrapped deadline should be reached")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_deadline_reached_u32(0xFFFFFFF0u, 0x00000010u) ==
                      PICFW_FALSE,
                  "future deadline should not be reached")) {
    return 1;
  }
  return 0;
}

static int test_scan_helpers_and_step_budget(void) {
  const char *name = "scan_helpers_and_step_budget";
  picfw_runtime_t runtime;
  uint8_t tx[32];
  size_t idx;
  size_t tx_len;

  picfw_runtime_init(&runtime, 0);
  if (expect_true(name,
                  runtime.protocol_tick_ms == PICFW_RUNTIME_SCAN_DEFAULT_TICK,
                  "scan tick default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_deadline_ms ==
                      PICFW_RUNTIME_SCAN_DEFAULT_DEADLINE,
                  "scan deadline default")) {
    return 1;
  }
  if (expect_true(
          name, runtime.scan_window_delay_ms == PICFW_RUNTIME_SCAN_MIN_DELAY_MS,
          "scan delay default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.scan_window_limit_ms ==
                      PICFW_RUNTIME_SCAN_DEFAULT_WINDOW_LIMIT,
                  "scan window limit default")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_seed == PICFW_RUNTIME_SCAN_DEFAULT_SEED,
                  "scan seed default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.saved_scan_seed == PICFW_RUNTIME_SCAN_DEFAULT_SEED,
                  "saved scan seed default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.merged_window_ms ==
                      PICFW_RUNTIME_SCAN_DEFAULT_MERGED_WINDOW,
                  "merged window default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.scan_mask_seed == PICFW_RUNTIME_SCAN_DEFAULT_SEED,
                  "scan mask seed default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.saved_scan_deadline_ms ==
                      PICFW_RUNTIME_SCAN_DEFAULT_DEADLINE,
                  "saved scan deadline default")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_window_delta_ms == 0x2Eu,
                  "scan window delta default")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_pass_deadline_ms == 0u,
                  "scan pass deadline default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.active_scan_slot == PICFW_RUNTIME_SCAN_DEFAULT_SLOT,
                  "active slot default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.current_scan_slot == PICFW_RUNTIME_SCAN_DEFAULT_SLOT,
                  "current slot default")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.descriptor_cursor ==
                      PICFW_RUNTIME_SCAN_DEFAULT_DESCRIPTOR_CURSOR,
                  "descriptor cursor default")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_IDLE,
                  "scan phase default")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_runtime_normalize_scan_delay(0u) ==
                      PICFW_RUNTIME_SCAN_MIN_DELAY_MS,
                  "normalize zero delay")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_runtime_normalize_scan_delay(0x10u) ==
                      PICFW_RUNTIME_SCAN_MIN_DELAY_MS,
                  "normalize short delay")) {
    return 1;
  }
  if (expect_true(name, picfw_runtime_normalize_scan_delay(0x80u) == 0x80u,
                  "preserve explicit delay")) {
    return 1;
  }
  if (expect_true(name,
                  picfw_runtime_scan_deadline_after(0xFFFFFFF0u, 0x20u) == 44u,
                  "wrap-safe deadline")) {
    return 1;
  }

  for (idx = 0u; idx < PICFW_RUNTIME_STEP_EVENT_BUDGET + 2u; ++idx) {
    if (!picfw_runtime_isr_enqueue_bus_byte(&runtime, (uint8_t)(0x20u + idx))) {
      return fail(name, "enqueue bus byte");
    }
  }

  picfw_runtime_step(&runtime, 1u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == PICFW_RUNTIME_STEP_EVENT_BUDGET,
                  "first step event budget")) {
    return 1;
  }
  if (expect_true(name, runtime.event_queue.count == 2u,
                  "remaining events after first step")) {
    return 1;
  }

  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  if (expect_true(name, tx_len == 2u, "second step drains remainder")) {
    return 1;
  }
  if (expect_true(name, runtime.event_queue.count == 0u,
                  "event queue drained")) {
    return 1;
  }

  return 0;
}

static int test_scan_window_and_protocol_dispatch(void) {
  const char *name = "scan_window_and_protocol_dispatch";
  picfw_runtime_t runtime;
  uint8_t return_code = 0u;
  size_t idx;

  picfw_runtime_init(&runtime, 0);
  runtime.now_ms = 0x00000140u;

  picfw_runtime_start_scan_window(&runtime);
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_PRIMED,
                  "scan phase primed")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_SCAN &&
                      runtime.protocol_state_flags == 0x03u,
                  "scan window protocol state")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_probe_deadline_ms == 0x000002D0u,
                  "scan probe deadline")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_pass_deadline_ms == 0x00000208u,
                  "scan pass deadline")) {
    return 1;
  }
  if (expect_true(name, runtime.saved_scan_seed == runtime.scan_seed,
                  "scan seed saved on start")) {
    return 1;
  }

  picfw_runtime_continue_scan_window(&runtime);
  if (expect_true(name, runtime.scan_dispatch_cursor == 0u,
                  "scan cursor held before probe deadline")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_PRIMED,
                  "scan phase held before finalize")) {
    return 1;
  }

  runtime.now_ms = 0x00000208u;
  picfw_runtime_continue_scan_window(&runtime);
  if (expect_true(name, runtime.scan_dispatch_cursor == 0u,
                  "scan cursor held before probe")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_PASS,
                  "scan phase finalized")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_pass_deadline_ms == 0u,
                  "scan pass deadline consumed")) {
    return 1;
  }

  runtime.now_ms = 0x000002D0u;
  picfw_runtime_continue_scan_window(&runtime);
  if (expect_true(name, runtime.scan_dispatch_cursor == 1u,
                  "scan cursor advanced")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.active_scan_slot == 0x01u &&
                      runtime.descriptor_cursor == 0x2454u,
                  "first dispatch slot state")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_VARIANT &&
                      runtime.protocol_state_flags == 0x03u,
                  "variant protocol state")) {
    return 1;
  }

  for (idx = 0u; idx < 6u; ++idx) {
    runtime.now_ms += 400u;
    picfw_runtime_continue_scan_window(&runtime);
  }
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_RETRY,
                  "scan phase retry")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_RETRY &&
                      runtime.protocol_state_flags == 0x01u,
                  "retry protocol state")) {
    return 1;
  }

  runtime.now_ms += 100u;
  picfw_runtime_continue_scan_window(&runtime);
  if (expect_true(name, runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_IDLE,
                  "retry completion to idle")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.now_ms = 0x00000200u;
  runtime.saved_scan_seed = 0x11223344u;
  runtime.scan_seed = 0x55667788u;
  runtime.scan_window_delay_ms = 0x10u;
  if (expect_true(name,
                  picfw_runtime_protocol_state_dispatch(
                      &runtime, 0x05u, &return_code) != PICFW_FALSE,
                  "dispatch protocol code 5")) {
    return 1;
  }
  if (expect_true(name, return_code == 0x01u, "dispatch code 5 return")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_seed == 0x11223344u,
                  "dispatch code 5 restores seed")) {
    return 1;
  }
  if (expect_true(name, runtime.protocol_deadline_ms == 0x0000023Cu,
                  "dispatch code 5 deadline")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  return_code = 0xFFu;
  if (expect_true(name,
                  picfw_runtime_protocol_state_dispatch(
                      &runtime, 0x06u, &return_code) != PICFW_FALSE,
                  "dispatch protocol code 6")) {
    return 1;
  }
  if (expect_true(name, return_code == 0u, "dispatch code 6 return")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_PENDING &&
                      runtime.protocol_state_flags == 0u,
                  "dispatch code 6 pending")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_PENDING;
  runtime.protocol_state_flags = 0x01u;
  runtime.saved_scan_seed = 0xAABBCCDDu;
  runtime.scan_seed = 0x01020304u;
  return_code = 0xFFu;
  if (expect_true(name,
                  picfw_runtime_protocol_state_dispatch(
                      &runtime, 0x02u, &return_code) != PICFW_FALSE,
                  "dispatch protocol code 2")) {
    return 1;
  }
  if (expect_true(name, return_code == 0u, "dispatch code 2 return")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_seed == 0xAABBCCDDu,
                  "dispatch code 2 restores seed")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_READY &&
                      runtime.protocol_state_flags == 0u,
                  "dispatch code 2 ready")) {
    return 1;
  }

  return 0;
}

static int test_scan_command_family_and_status_builders(void) {
  const char *name = "scan_command_family_and_status_builders";
  picfw_runtime_t runtime;
  uint8_t snapshot[PICFW_RUNTIME_STATUS_FRAME_MAX];
  uint8_t variant[PICFW_RUNTIME_STATUS_FRAME_MAX];

  picfw_runtime_init(&runtime, 0);
  runtime.now_ms = 0x00000140u;

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x01u) != PICFW_FALSE,
          "dispatch 0x01")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.descriptor_cursor == 0x0264u &&
                      runtime.active_scan_slot == 0x01u,
                  "dispatch 0x01 state")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x03u) != PICFW_FALSE,
          "dispatch 0x03")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.descriptor_cursor == 0x0260u &&
                      runtime.active_scan_slot == 0x03u,
                  "dispatch 0x03 state")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x36u) != PICFW_FALSE,
          "dispatch 0x36")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.descriptor_cursor == 0x0268u &&
                      runtime.active_scan_slot == 0x06u,
                  "dispatch 0x36 state")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x33u) != PICFW_FALSE,
          "dispatch 0x33")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_window_limit_ms == 0xA402A402u,
                  "dispatch 0x33 limit")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x3Au) != PICFW_FALSE,
          "dispatch 0x3a")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_window_delay_ms == 0xA402A402u,
                  "dispatch 0x3a delay")) {
    return 1;
  }
  if (expect_true(name, runtime.protocol_deadline_ms == 0xA402A542u,
                  "dispatch 0x3a deadline")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x3Bu) != PICFW_FALSE,
          "dispatch 0x3b")) {
    return 1;
  }
  if (expect_true(name, runtime.merged_window_ms == 0xA402A402u,
                  "dispatch 0x3b merged")) {
    return 1;
  }

  if (expect_true(
          name, picfw_runtime_dispatch_scan_code(&runtime, 0x35u) != PICFW_FALSE,
          "dispatch 0x35")) {
    return 1;
  }
  if (expect_true(name, runtime.status_seed_latch == 0xA4u,
                  "dispatch 0x35 latch")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_SCAN;
  runtime.protocol_state_flags = 0x03u;
  if (expect_true(name,
                  picfw_runtime_build_status_snapshot_frame(&runtime, snapshot,
                                                            sizeof(snapshot)) ==
                      PICFW_RUNTIME_STATUS_FRAME_MAX,
                  "snapshot builder")) {
    return 1;
  }
  if (expect_true(name,
                  snapshot[4] == 0x35u && snapshot[13] == 0x02u &&
                      snapshot[14] == 0x02u && snapshot[15] == 0x02u,
                  "snapshot key bytes")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_VARIANT;
  if (expect_true(name,
                  picfw_runtime_build_status_variant_frame(&runtime, variant,
                                                           sizeof(variant)) ==
                      PICFW_RUNTIME_STATUS_FRAME_MAX,
                  "variant builder")) {
    return 1;
  }
  if (expect_true(name, variant[4] == 0x37u && variant[17] == 0xAAu,
                  "variant key bytes")) {
    return 1;
  }
  if (expect_true(name,
                  variant[18] == '0' && variant[19] == '2' &&
                      variant[20] == '0' && variant[21] == '2',
                  "variant ascii trailer")) {
    return 1;
  }

  if (expect_true(name,
                  picfw_runtime_dispatch_scan_code(&runtime, 0x7Fu) ==
                      PICFW_FALSE,
                  "unsupported dispatch")) {
    return 1;
  }

  return 0;
}

static int test_compute_next_scan_cursor(void) {
  const char *name = "compute_next_scan_cursor";
  picfw_runtime_t runtime;
  uint8_t return_code = 0xFFu;

  picfw_runtime_init(&runtime, 0);
  runtime.now_ms = 0x00000140u;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.saved_scan_seed = 0x11223344u;
  runtime.scan_seed = 0x55667788u;

  if (expect_true(name,
                  picfw_runtime_compute_next_scan_cursor(
                      &runtime, 0x06u, &return_code) != PICFW_FALSE,
                  "slot 0x06 dispatch")) {
    return 1;
  }
  if (expect_true(name, return_code == 0x01u, "slot 0x06 return")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.active_scan_slot == 0x06u &&
                      runtime.descriptor_cursor == 0x0268u,
                  "slot 0x06 cursor")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_protocol_code == 0x05u,
                  "slot 0x06 protocol code")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.saved_scan_seed == 0x55667788u &&
                      runtime.scan_seed == 0x55667788u,
                  "slot 0x06 captured current seed")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.saved_scan_seed = 0xAABBCCDDu;
  runtime.scan_seed = 0x01020304u;
  return_code = 0xFFu;
  if (expect_true(name,
                  picfw_runtime_compute_next_scan_cursor(
                      &runtime, 0x01u, &return_code) != PICFW_FALSE,
                  "slot 0x01 dispatch")) {
    return 1;
  }
  if (expect_true(name, return_code == 0u, "slot 0x01 return")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.active_scan_slot == 0x01u &&
                      runtime.descriptor_cursor == 0x0264u,
                  "slot 0x01 cursor")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_protocol_code == 0x06u,
                  "slot 0x01 protocol code")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_PENDING &&
                      runtime.protocol_state_flags == 0u,
                  "slot 0x01 pending state")) {
    return 1;
  }

  runtime.protocol_state = PICFW_PROTOCOL_STATE_PENDING;
  runtime.protocol_state_flags = 0x01u;
  runtime.saved_scan_seed = 0xCAFEBABEu;
  runtime.scan_seed = 0x0BADF00Du;
  return_code = 0xFFu;
  if (expect_true(name,
                  picfw_runtime_compute_next_scan_cursor(
                      &runtime, 0x03u, &return_code) != PICFW_FALSE,
                  "slot 0x03 dispatch")) {
    return 1;
  }
  if (expect_true(name, return_code == 0u, "slot 0x03 return")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.active_scan_slot == 0x03u &&
                      runtime.descriptor_cursor == 0x0260u,
                  "slot 0x03 cursor")) {
    return 1;
  }
  if (expect_true(name, runtime.scan_protocol_code == 0x02u,
                  "slot 0x03 protocol code")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.saved_scan_seed == 0x0BADF00Du &&
                      runtime.scan_seed == 0x0BADF00Du,
                  "slot 0x03 captured current seed")) {
    return 1;
  }
  if (expect_true(name,
                  runtime.protocol_state == PICFW_PROTOCOL_STATE_READY &&
                      runtime.protocol_state_flags == 0u,
                  "slot 0x03 ready state")) {
    return 1;
  }

  return 0;
}

static int test_rotation_primitives(void) {
  const char *name = "rotation_primitives";
  int errors = 0;

  errors += expect_true(name, picfw_rot32_right(0x12345678u, 0) == 0x12345678u,
                        "rot_right 0");
  errors += expect_true(name, picfw_rot32_right(0x12345678u, 8) == 0x78123456u,
                        "rot_right 8");
  errors += expect_true(name, picfw_rot32_right(0x12345678u, 24) == 0x34567812u,
                        "rot_right 24");
  errors += expect_true(name, picfw_rot32_left(0x12345678u, 0) == 0x12345678u,
                        "rot_left 0");
  errors += expect_true(name, picfw_rot32_left(0x12345678u, 8) == 0x34567812u,
                        "rot_left 8");
  errors += expect_true(name, picfw_rot32_left(0x12345678u, 24) == 0x78123456u,
                        "rot_left 24");
  errors += expect_true(name, picfw_rot32_right(0x80000001u, 1) == 0xC0000000u,
                        "rot_right 1 carry");
  errors += expect_true(name, picfw_rot32_left(0x80000001u, 1) == 0x00000003u,
                        "rot_left 1 carry");

  return errors;
}

static int test_descriptor_read_u32(void) {
  const char *name = "descriptor_read_u32";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  uint32_t val;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);

  runtime.descriptor_data[0] = 0x78u;
  runtime.descriptor_data[1] = 0x56u;
  runtime.descriptor_data[2] = 0x34u;
  runtime.descriptor_data[3] = 0x12u;
  runtime.descriptor_data[4] = 0xAAu;
  runtime.descriptor_data[5] = 0xBBu;
  runtime.descriptor_data[6] = 0xCCu;
  runtime.descriptor_data[7] = 0xDDu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;

  val = picfw_runtime_descriptor_read_u32(&runtime);
  errors += expect_true(name, val == 0x12345678u, "first u32 value");
  errors += expect_true(name, runtime.descriptor_data_pos == 4u,
                        "pos advanced to 4");

  val = picfw_runtime_descriptor_read_u32(&runtime);
  errors += expect_true(name, val == 0xDDCCBBAAu, "second u32 value");
  errors += expect_true(name, runtime.descriptor_data_pos == 8u,
                        "pos advanced to 8");

  /* Partial read: only 3 bytes available */
  picfw_runtime_init(&runtime, &config);
  runtime.descriptor_data[0] = 0x11u;
  runtime.descriptor_data[1] = 0x22u;
  runtime.descriptor_data[2] = 0x33u;
  runtime.descriptor_data_len = 3u;
  runtime.descriptor_data_pos = 0u;
  val = picfw_runtime_descriptor_read_u32(&runtime);
  errors += expect_true(name, val == 0x00332211u,
                        "partial read returns zero-padded value");
  errors += expect_true(name, runtime.descriptor_data_pos == 3u,
                        "partial read advances pos to len");
  errors += expect_true(name, runtime.last_error == 4u,
                        "partial read sets PARSE error");

  /* Read when pos already at end */
  val = picfw_runtime_descriptor_read_u32(&runtime);
  errors += expect_true(name, val == 0u,
                        "read at end returns 0");

  return errors;
}

static int test_descriptor_merge_with_seed(void) {
  const char *name = "descriptor_merge_with_seed";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;

  runtime.scan_mask_seed = 0x00010601u;
  runtime.merged_window_ms = 0x00000000u;
  runtime.scan_seed = 0x000002A4u;

  picfw_runtime_descriptor_merge_with_seed(&runtime, 0xFFFFFFFFu,
                                            0xFFFFFFFFu);
  errors += expect_true(name, runtime.merged_window_ms != 0u,
                        "merged window non-zero after full mask");
  errors += expect_true(name, runtime.status_seed_latch == 0xA4u,
                        "seed latch captures low byte");

  picfw_runtime_init(&runtime, &config);
  runtime.scan_mask_seed = PICFW_RUNTIME_DESCRIPTOR_XOR_KEY;
  runtime.merged_window_ms = 0x00000100u;
  runtime.scan_seed = 0x000002A4u;
  picfw_runtime_descriptor_merge_with_seed(&runtime, 0x12345678u,
                                            0xFFFFFFFFu);
  errors += expect_true(name, runtime.status_seed_latch == 0xA4u,
                        "xor_key==0 delegates to merge");

  picfw_runtime_init(&runtime, &config);
  runtime.scan_mask_seed = 0x00000064u;
  runtime.merged_window_ms = 0u;
  runtime.scan_seed = 0x000002A4u;
  picfw_runtime_descriptor_merge_with_seed(&runtime, 0u, 0u);
  errors += expect_true(name,
                        runtime.merged_window_ms ==
                            PICFW_RUNTIME_SCAN_MIN_DELAY_MS,
                        "zero data clamps to min delay");

  return errors;
}

static int test_post_merge_validate(void) {
  const char *name = "post_merge_validate";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);

  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_window_limit_ms = 0x00000050u;
  runtime.scan_window_delay_ms = 0x00000080u;
  runtime.merged_window_ms = 0x00000100u;

  picfw_runtime_post_merge_validate(&runtime);

  errors += expect_true(name,
                        runtime.scan_window_limit_ms ==
                            PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR,
                        "limit clamped to floor 0xF0");

  errors += expect_true(name,
                        runtime.scan_window_delay_ms == 0x00000080u,
                        "delay in safe zone not recomputed");

  errors += expect_true(name,
                        runtime.merged_window_ms ==
                            (PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR >> 3),
                        "merged >= limit recomputed to limit/8");

  picfw_runtime_init(&runtime, &config);
  runtime.protocol_state = PICFW_PROTOCOL_STATE_IDLE;
  runtime.scan_window_limit_ms = 0x00000050u;
  picfw_runtime_post_merge_validate(&runtime);
  errors += expect_true(name, runtime.scan_window_limit_ms == 0x00000050u,
                        "non-READY state: no clamping");

  picfw_runtime_init(&runtime, &config);
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.scan_window_delay_ms = 0x00000040u;
  runtime.merged_window_ms = 0x000000A0u;
  picfw_runtime_post_merge_validate(&runtime);
  errors += expect_true(name,
                        runtime.scan_window_delay_ms ==
                            ((0x00000200u >> 3) << 2),
                        "low delay recomputed from limit");
  errors += expect_true(name,
                        runtime.merged_window_ms == (0x00000200u >> 3),
                        "low merged recomputed from limit");

  picfw_runtime_init(&runtime, &config);
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.scan_window_delay_ms = 0x000000C0u;
  runtime.merged_window_ms = 0x000000E0u;
  picfw_runtime_post_merge_validate(&runtime);
  errors += expect_true(name, runtime.scan_window_delay_ms == 0x000000C0u,
                        "mid-range delay in safe zone");
  errors += expect_true(name, runtime.merged_window_ms == 0x000000E0u,
                        "mid-range merged in safe zone");

  return errors;
}

static int test_load_descriptor_block(void) {
  const char *name = "load_descriptor_block";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_mask_seed = 0x00010601u;
  runtime.scan_seed = 0x000002A4u;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.scan_window_delay_ms = 0x0000003Cu;

  runtime.descriptor_data[0] = 0xFFu;
  runtime.descriptor_data[1] = 0x00u;
  runtime.descriptor_data[2] = 0xFFu;
  runtime.descriptor_data[3] = 0x00u;
  runtime.descriptor_data[4] = 0xFFu;
  runtime.descriptor_data[5] = 0xFFu;
  runtime.descriptor_data[6] = 0xFFu;
  runtime.descriptor_data[7] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;

  errors += expect_true(name,
                        picfw_runtime_load_descriptor_block(&runtime) ==
                            PICFW_TRUE,
                        "load returns true with 8 bytes");
  errors += expect_true(name, runtime.descriptor_data_pos == 8u,
                        "consumed all 8 bytes");

  picfw_runtime_init(&runtime, &config);
  runtime.descriptor_data_len = 4u;
  errors += expect_true(name,
                        picfw_runtime_load_descriptor_block(&runtime) ==
                            PICFW_FALSE,
                        "load returns false with < 8 bytes");

  return errors;
}

static int test_deep_scan_fsm_with_descriptors(void) {
  const char *name = "deep_scan_fsm_with_descriptors";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.scan_mask_seed = 0x00010601u;
  runtime.scan_seed = 0x000002A4u;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.scan_window_delay_ms = 0x0000003Cu;
  runtime.now_ms = 100u;

  runtime.descriptor_data[0] = 0xFFu;
  runtime.descriptor_data[1] = 0x00u;
  runtime.descriptor_data[2] = 0xFFu;
  runtime.descriptor_data[3] = 0x00u;
  runtime.descriptor_data[4] = 0xFFu;
  runtime.descriptor_data[5] = 0xFFu;
  runtime.descriptor_data[6] = 0xFFu;
  runtime.descriptor_data[7] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;

  picfw_runtime_run_scan_fsm(&runtime);

  errors += expect_true(name,
                        runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_PRIMED,
                        "after run_scan_fsm: phase is PRIMED");
  errors += expect_true(name,
                        runtime.protocol_state == PICFW_PROTOCOL_STATE_SCAN,
                        "after run_scan_fsm: protocol state is SCAN");

  runtime.now_ms = 103u;
  picfw_runtime_continue_scan_fsm(&runtime, 0u);

  errors += expect_true(name,
                        runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_PASS ||
                            runtime.scan_phase ==
                                PICFW_RUNTIME_SCAN_PHASE_PRIMED,
                        "after continue: phase advances");

  picfw_runtime_continue_scan_fsm(&runtime, 0x01u);

  errors += expect_true(name,
                        runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_RETRY,
                        "error triggers RETRY phase");
  errors += expect_true(name,
                        runtime.protocol_state == PICFW_PROTOCOL_STATE_RETRY,
                        "error: protocol state is RETRY");
  errors += expect_true(name, runtime.last_error == 0x01u,
                        "error code captured");

  runtime.now_ms = 203u;
  picfw_runtime_continue_scan_fsm(&runtime, 0u);

  errors += expect_true(name,
                        runtime.scan_phase == PICFW_RUNTIME_SCAN_PHASE_IDLE,
                        "retry resolved to IDLE");

  return errors;
}

static int test_scan_mask_functions(void) {
  const char *name = "scan_mask_functions";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);

  /* shift_scan_masks_by_delta: xor_result == 0 delegates to merge */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_mask_seed = 0x00010642u;
  runtime.scan_seed = 0x000002A4u;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  errors += expect_true(name,
                        picfw_runtime_shift_scan_masks_by_delta(
                            &runtime, 0x42u) != PICFW_FALSE,
                        "delta XOR seed_lo==0 delegates to merge");

  /* shift_scan_masks_by_delta: xor_result != 0, needs descriptor data */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_mask_seed = 0x00010601u;
  runtime.scan_seed = 0x000002A4u;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.descriptor_data[0] = 0xFFu;
  runtime.descriptor_data[1] = 0xFFu;
  runtime.descriptor_data[2] = 0xFFu;
  runtime.descriptor_data[3] = 0xFFu;
  runtime.descriptor_data[4] = 0xFFu;
  runtime.descriptor_data[5] = 0xFFu;
  runtime.descriptor_data[6] = 0xFFu;
  runtime.descriptor_data[7] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;
  errors += expect_true(name,
                        picfw_runtime_shift_scan_masks_by_delta(
                            &runtime, 0x05u) != PICFW_FALSE,
                        "delta with descriptor data succeeds");

  /* shift_scan_masks_by_delta: insufficient data */
  picfw_runtime_init(&runtime, &config);
  runtime.scan_mask_seed = 0x00010601u;
  runtime.descriptor_data_len = 4u;
  errors += expect_true(name,
                        picfw_runtime_shift_scan_masks_by_delta(
                            &runtime, 0x05u) == PICFW_FALSE,
                        "delta without data fails");

  /* merge_shifted_scan_masks */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_mask_seed = 0x01000000u;
  runtime.scan_seed = 0x000002A4u;
  runtime.merged_window_ms = 0x000000FFu;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_merge_shifted_scan_masks(&runtime);
  errors += expect_true(name, runtime.status_seed_latch == 0xA4u,
                        "merge_shifted sets seed latch");

  /* merge_pending_scan_masks */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_mask_seed = 0x00FF0000u;
  runtime.scan_seed = 0x00000099u;
  runtime.merged_window_ms = 0x00000100u;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_merge_pending_scan_masks(&runtime);
  errors += expect_true(name, runtime.status_seed_latch == 0x99u,
                        "merge_pending sets seed latch");

  /* shift_saved_scan_masks */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.saved_scan_seed = 0xFF000000u;
  runtime.scan_seed = 0x000000ABu;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_shift_saved_scan_masks(&runtime, 24u);
  errors += expect_true(name, runtime.merged_window_ms != 0u,
                        "shift_saved produces non-zero merged");
  errors += expect_true(name, runtime.status_seed_latch == 0xABu,
                        "shift_saved sets seed latch");

  /* shift_saved_scan_masks: shift_count == 0 (no-op shift, post_merge runs) */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.saved_scan_seed = 0xFF000000u;
  runtime.scan_seed = 0x000000CDu;
  runtime.merged_window_ms = 0x00000100u;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_shift_saved_scan_masks(&runtime, 0u);
  errors += expect_true(name, runtime.merged_window_ms == 0x00000100u,
                        "shift_count=0 preserves merged_window_ms");

  /* shift_saved_scan_masks: shift_count == 32 (UB guard => shifted=0).
   * merged |= 0 stays 0, clamped to MIN_DELAY, then post_merge_validate
   * recomputes to limit>>3. */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.saved_scan_seed = 0xFFFFFFFFu;
  runtime.scan_seed = 0x000000EFu;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_shift_saved_scan_masks(&runtime, 32u);
  errors += expect_true(name,
                        runtime.merged_window_ms == (0x00000200u >> 3),
                        "shift_count=32 produces post-validated merged");
  errors += expect_true(name, runtime.status_seed_latch == 0xEFu,
                        "shift_count=32 sets seed latch");

  /* shift_saved_scan_masks: shift_count == 33 (also UB-guarded) */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.saved_scan_seed = 0xFFFFFFFFu;
  runtime.scan_seed = 0x00000012u;
  runtime.merged_window_ms = 0u;
  runtime.scan_window_limit_ms = 0x00000200u;
  picfw_runtime_shift_saved_scan_masks(&runtime, 33u);
  errors += expect_true(name,
                        runtime.merged_window_ms == (0x00000200u >> 3),
                        "shift_count=33 produces post-validated merged");

  return errors;
}

static int test_initialize_scan_slot_full(void) {
  const char *name = "initialize_scan_slot_full";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);

  /* Slot 0x06 */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.scan_seed = 0x12345678u;
  runtime.saved_scan_seed = 0x12345678u;
  runtime.saved_scan_deadline_ms = 100u;
  runtime.scan_window_delay_ms = 0x50u;
  picfw_runtime_initialize_scan_slot_full(&runtime, 0x06u);

  errors += expect_true(name, runtime.current_scan_slot_id == 0x06u,
                        "slot 0x06: slot_id set");
  errors += expect_true(name, runtime.scan_mask_seed == 0x00060101u,
                        "slot 0x06: scan_mask_seed = 0x00060101");
  errors += expect_true(name, runtime.scan_accum_r24 == (0x12345678u >> 24),
                        "slot 0x06: accum_r24");
  errors += expect_true(name, runtime.scan_accum_r8 == (0x12345678u >> 8),
                        "slot 0x06: accum_r8");
  errors += expect_true(name, runtime.scan_accum_l8 == (0x12345678u << 8),
                        "slot 0x06: accum_l8");
  errors += expect_true(name, runtime.scan_accum_l24 == (0x12345678u << 24),
                        "slot 0x06: accum_l24");

  /* Slot 0x01 */
  picfw_runtime_init(&runtime, &config);
  runtime.scan_seed = 0xAABBCCDDu;
  runtime.saved_scan_deadline_ms = 0u;
  runtime.scan_window_delay_ms = 0x20u;
  picfw_runtime_initialize_scan_slot_full(&runtime, 0x01u);

  errors += expect_true(name, runtime.current_scan_slot_id == 0x01u,
                        "slot 0x01: slot_id set");
  errors += expect_true(name, runtime.scan_mask_seed == 0x00060101u,
                        "slot 0x01: scan_mask_seed constant");
  return errors;
}

static int test_recompute_scan_masks_tail(void) {
  const char *name = "recompute_scan_masks_tail";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  uint16_t cursor_before;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.scan_seed = 0x000000ABu;
  runtime.merged_window_ms = 0u;
  runtime.scan_accum_r24 = 0x00000011u;
  runtime.scan_accum_r8 = 0x00002200u;
  runtime.scan_accum_l8 = 0x00330000u;
  runtime.scan_accum_l24 = 0x44000000u;
  runtime.descriptor_cursor = 0x0100u;

  cursor_before = runtime.descriptor_cursor;
  picfw_runtime_recompute_scan_masks_tail(&runtime);

  errors += expect_true(name,
                        runtime.merged_window_ms ==
                            (0x00000011u | 0x00002200u | 0x00330000u |
                             0x44000000u),
                        "accumulators ORed into merged_window");
  errors += expect_true(name,
                        runtime.descriptor_cursor ==
                            (uint16_t)(cursor_before + 0x2Cu),
                        "cursor advanced by 0x2C");
  errors += expect_true(name, runtime.status_seed_latch == 0xABu,
                        "seed latch set");

  return errors;
}

static int test_seed_recomputation_chain(void) {
  const char *name = "seed_recomputation_chain";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);

  /* mask_tail_seed_and_recompute */
  picfw_runtime_init(&runtime, &config);
  runtime.saved_scan_seed = 0xAABBCCDDu;
  runtime.scan_seed = 0x00000099u;
  runtime.merged_window_ms = 0u;
  picfw_runtime_mask_tail_seed_and_recompute(&runtime);
  errors += expect_true(name, runtime.scan_accum_l8 == (0xAABBCCDDu << 8),
                        "mask_tail: accum_l8 from saved_seed");
  errors += expect_true(name, runtime.scan_accum_l24 == (0xAABBCCDDu << 24),
                        "mask_tail: accum_l24 from saved_seed");
  errors += expect_true(name, runtime.scan_accum_r8 == 0u,
                        "mask_tail: accum_r8 cleared");

  /* init_seed_accumulators_and_recompute */
  picfw_runtime_init(&runtime, &config);
  runtime.scan_seed = 0x12345678u;
  runtime.merged_window_ms = 0u;
  picfw_runtime_init_seed_accumulators_and_recompute(&runtime);
  errors += expect_true(name, runtime.scan_accum_r24 == (0x12345678u >> 24),
                        "init_seed: accum_r24 from seed");

  /* init_tail_seed_accumulators_and_recompute */
  picfw_runtime_init(&runtime, &config);
  runtime.merged_window_ms = 0u;
  picfw_runtime_init_tail_seed_accumulators_and_recompute(&runtime, 0x14u);
  errors += expect_true(name, runtime.scan_accum_r24 == 0x14u,
                        "init_tail: accum_r24 = param");
  errors += expect_true(name, runtime.scan_accum_l24 == (0x14u << 24),
                        "init_tail: accum_l24 = param<<24");

  /* load_tail_seed_and_recompute */
  picfw_runtime_init(&runtime, &config);
  runtime.saved_scan_seed = 0xFF001122u;
  runtime.merged_window_ms = 0u;
  picfw_runtime_load_tail_seed_and_recompute(&runtime, 9u);
  errors += expect_true(name, runtime.scan_accum_r24 == (0xFF001122u >> 24),
                        "load_tail: accum_r24 from saved>>24");
  errors += expect_true(name, runtime.scan_accum_r8 == 9u,
                        "load_tail: accum_r8 = param");

  return errors;
}

static int test_slot_level_operations(void) {
  const char *name = "slot_level_operations";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;

  /* probe with no data fails */
  runtime.descriptor_data_len = 0u;
  errors += expect_true(name,
                        picfw_runtime_probe_register_window(&runtime) ==
                            PICFW_FALSE,
                        "probe fails without data");

  /* probe with data succeeds */
  runtime.descriptor_data[0] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;
  errors += expect_true(name,
                        picfw_runtime_probe_register_window(&runtime) ==
                            PICFW_TRUE,
                        "probe succeeds with data");
  errors += expect_true(name, runtime.scan_slot_sub_phase == 1u,
                        "probe sets sub_phase=1");

  /* prime sets SCAN state */
  picfw_runtime_prime_scan_slot(&runtime);
  errors += expect_true(name,
                        runtime.protocol_state == PICFW_PROTOCOL_STATE_SCAN,
                        "prime sets SCAN state");

  /* poll with data remaining returns false */
  runtime.descriptor_data_pos = 0u;
  errors += expect_true(name,
                        picfw_runtime_poll_scan_slot(&runtime) == PICFW_FALSE,
                        "poll returns false with data remaining");

  /* poll with data consumed returns true */
  runtime.descriptor_data_pos = 8u;
  errors += expect_true(name,
                        picfw_runtime_poll_scan_slot(&runtime) != PICFW_FALSE,
                        "poll returns true when data consumed");
  errors += expect_true(name, runtime.scan_slot_sub_phase == 3u,
                        "poll sets sub_phase=3 (done)");

  /* run_scan_slot_sequence */
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.scan_mask_seed = 0x00010601u;
  runtime.scan_seed = 0x000002A4u;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.descriptor_data[0] = 0xFFu;
  runtime.descriptor_data[1] = 0xFFu;
  runtime.descriptor_data[2] = 0xFFu;
  runtime.descriptor_data[3] = 0xFFu;
  runtime.descriptor_data[4] = 0xFFu;
  runtime.descriptor_data[5] = 0xFFu;
  runtime.descriptor_data[6] = 0xFFu;
  runtime.descriptor_data[7] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;
  picfw_runtime_run_scan_slot_sequence(&runtime);
  errors += expect_true(name, runtime.scan_slot_sub_phase == 3u,
                        "sequence completes to sub_phase=3");

  return errors;
}

static int test_app_main_loop(void) {
  const char *name = "app_main_loop";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);
  runtime.startup_state = PICFW_STARTUP_LIVE_READY;
  runtime.scan_seed = 0x000002A4u;
  runtime.saved_scan_seed = 0x000002A4u;

  picfw_runtime_app_main_loop_init(&runtime);
  errors += expect_true(name,
                        runtime.status_seed_latch == 0xA4u,
                        "init sets seed latch");

  /* Step with sub_phase >= 3 should return false */
  runtime.scan_slot_sub_phase = 3u;
  errors += expect_true(name,
                        picfw_runtime_app_main_loop_step(&runtime) ==
                            PICFW_FALSE,
                        "step returns false when done");

  /* Step with sub_phase < 3 and data should run sequence */
  runtime.scan_slot_sub_phase = 0u;
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x03u;
  runtime.scan_mask_seed = 0x00010601u;
  runtime.scan_window_limit_ms = 0x00000200u;
  runtime.descriptor_data[0] = 0xAAu;
  runtime.descriptor_data[1] = 0xBBu;
  runtime.descriptor_data[2] = 0xCCu;
  runtime.descriptor_data[3] = 0xDDu;
  runtime.descriptor_data[4] = 0xFFu;
  runtime.descriptor_data[5] = 0xFFu;
  runtime.descriptor_data[6] = 0xFFu;
  runtime.descriptor_data[7] = 0xFFu;
  runtime.descriptor_data_len = 8u;
  runtime.descriptor_data_pos = 0u;
  runtime.now_ms = 200u;
  errors += expect_true(name,
                        picfw_runtime_app_main_loop_step(&runtime) ==
                            PICFW_TRUE,
                        "step returns true with data");

  return errors;
}

static int test_bus_byte_forwarding_full_range(void) {
  const char *name = "bus_byte_forwarding_full_range";
  int errors = 0;
  static const uint8_t probes[] = {0x00, 0x41, 0x7F, 0x80, 0xA9, 0xAA, 0xFF};
  size_t p;

  for (p = 0u; p < sizeof(probes); ++p) {
    picfw_runtime_t runtime;
    uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
    picfw_enh_frame_t frames[4];
    size_t tx_len;
    size_t frame_count;
    uint8_t byte = probes[p];

    picfw_runtime_init(&runtime, 0);
    if (!picfw_runtime_isr_enqueue_bus_byte(&runtime, byte)) {
      return fail(name, "bus enqueue");
    }
    picfw_runtime_step(&runtime, 0u);
    tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

    if (byte < 0x80u) {
      /* Short-form: single byte output matching the bus byte */
      errors += expect_true(name, tx_len == 1u, "short-form length");
      errors += expect_true(name, tx[0] == byte, "short-form value");
    } else {
      /* 2-byte ENH encoded RECEIVED frame */
      errors += expect_true(name, tx_len == 2u, "enh encoded length");
      frame_count =
          collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
      errors += expect_true(name, frame_count == 1u, "enh frame count");
      errors += expect_true(name, frames[0].command == PICFW_ENH_RES_RECEIVED,
                            "enh command RECEIVED");
      errors += expect_true(name, frames[0].data == byte, "enh data match");
    }

    /* SYN byte should clear arbitration but still forward */
    if (byte == PICFW_RUNTIME_SYN_BYTE) {
      errors += expect_true(name, runtime.arbitration_active == PICFW_FALSE,
                            "SYN clears arbitration");
    }
  }

  return errors;
}

static int test_host_parser_timeout(void) {
  const char *name = "host_parser_timeout";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;
  uint8_t byte1;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  /* Get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* Send only byte1 of a 2-byte ENH frame (first byte of REQ_INFO) */
  {
    uint8_t encoded[2];
    picfw_enh_encode(PICFW_ENH_REQ_INFO, PICFW_ADAPTER_INFO_VERSION, encoded,
                     sizeof(encoded));
    byte1 = encoded[0];
  }
  picfw_runtime_isr_enqueue_host_byte(&runtime, byte1);
  /* Step at t=1 to start the parser, deadline = 1 + 64 = 65 */
  picfw_runtime_step(&runtime, 1u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 0u, "no output on partial frame");
  errors += expect_true(name, runtime.host_parser_active != PICFW_FALSE,
                        "parser active");

  /* Step at t=64 (should NOT timeout: deadline is 65) */
  picfw_runtime_step(&runtime, 64u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 0u, "no timeout at t=64");
  errors += expect_true(name,
                        runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                        "still LIVE_READY at t=64");

  /* Step at t=66 (should timeout: deadline=65 is reached) */
  picfw_runtime_step(&runtime, 66u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "timeout error frame count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_ERROR_HOST,
                        "timeout error command");
  errors += expect_true(name, frames[0].data == 6u, "timeout error code=6");
  errors += expect_true(name, runtime.startup_state == PICFW_STARTUP_DEGRADED,
                        "state DEGRADED after timeout");

  /* Verify parser reset: send a complete REQ_INIT, verify it works */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 67u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "recovery frame count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                        "recovery RESETTED response");
  errors += expect_true(name,
                        runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                        "recovered to LIVE_READY");

  return errors;
}

static int test_event_queue_overflow(void) {
  const char *name = "event_queue_overflow";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  size_t idx;
  size_t total_tx = 0u;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);

  /* Enqueue 32 bus bytes (fills queue to capacity) */
  for (idx = 0u; idx < PICFW_RUNTIME_EVENT_QUEUE_CAP; ++idx) {
    errors += expect_true(name,
                          picfw_runtime_isr_enqueue_bus_byte(
                              &runtime, (uint8_t)(idx & 0x7Fu)) != PICFW_FALSE,
                          "enqueue within capacity");
  }

  /* 33rd byte should fail */
  errors += expect_true(name,
                        picfw_runtime_isr_enqueue_bus_byte(&runtime, 0x00u) ==
                            PICFW_FALSE,
                        "33rd enqueue fails");
  errors += expect_true(name, runtime.dropped_events == 1u,
                        "dropped_events == 1");

  /* Drain all events by stepping multiple times (budget is 8 per step) */
  while (runtime.event_queue.count > 0u) {
    picfw_runtime_step(&runtime, 0u);
    total_tx += picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  }
  errors += expect_true(name, total_tx == PICFW_RUNTIME_EVENT_QUEUE_CAP,
                        "32 RECEIVED frames produced");

  return errors;
}

static int test_tx_queue_overflow_and_degraded(void) {
  const char *name = "tx_queue_overflow_and_degraded";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[4];
  size_t tx_len;
  size_t frame_count;
  size_t idx;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  /* Get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /*
   * Fill the TX queue by injecting bus bytes in batches (event queue
   * capacity is 32) and stepping WITHOUT draining. Each bus byte < 0x80
   * produces 1 TX byte. TX capacity is PICFW_RUNTIME_HOST_TX_CAP (128).
   */
  /* Batches 1-5: fill TX queue beyond capacity */
  for (idx = 0u; idx < 5u; ++idx) {
    size_t j;
    for (j = 0u; j < PICFW_RUNTIME_EVENT_QUEUE_CAP; ++j) {
      picfw_runtime_isr_enqueue_bus_byte(&runtime, (uint8_t)(j & 0x7Fu));
    }
    { size_t k; for (k = 0u; k < 4u; ++k) { picfw_runtime_step(&runtime, 1u); } }
  }
  /* TX queue should be full now — do NOT drain.
   * Some of batch 5 may have already overflowed. */

  /* Enqueue more and step to ensure TX overflow */
  for (idx = 0u; idx < 8u; ++idx) {
    picfw_runtime_isr_enqueue_bus_byte(&runtime, 0x10u);
  }
  picfw_runtime_step(&runtime, 2u);

  errors += expect_true(name, runtime.dropped_tx_bytes > 0u,
                        "dropped_tx_bytes > 0");
  errors += expect_true(name, runtime.startup_state == PICFW_STARTUP_DEGRADED,
                        "state DEGRADED after TX overflow");

  /* Verify REQ_INIT recovers from DEGRADED */
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 3u);
  /* Drain everything */
  while (runtime.host_tx_queue.count > 0u) {
    picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  }
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  (void)tx_len;
  errors += expect_true(name,
                        runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                        "INIT recovers to LIVE_READY");

  /* Verify periodic status stops while DEGRADED */
  {
    picfw_runtime_config_t config;
    picfw_runtime_config_init_default(&config);
    config.status_emit_enabled = PICFW_TRUE;
    config.status_snapshot_period_ms = 1u;
    picfw_runtime_init(&runtime, &config);
    enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
    picfw_runtime_step(&runtime, 0u);
    picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
    /* Force DEGRADED */
    runtime.startup_state = PICFW_STARTUP_DEGRADED;
    picfw_runtime_step(&runtime, 100u);
    tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
    errors += expect_true(name, tx_len == 0u,
                          "no periodic status while DEGRADED");
    /* Recover with INIT */
    enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
    picfw_runtime_step(&runtime, 101u);
    tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
    frame_count =
        collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
    errors += expect_true(name, frame_count >= 1u, "INIT recovery response");
    errors += expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                          "INIT recovery command");
    errors += expect_true(name,
                          runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                          "INIT restores LIVE_READY");
  }

  return errors;
}

static int test_ens_codec_roundtrip(void) {
  const char *name = "ens_codec_roundtrip";
  static const uint8_t probes[] = {0x00, 0x41, 0x7F, 0x80, 0xA9, 0xAA, 0xFF};
  int errors = 0;
  size_t p;

  for (p = 0u; p < sizeof(probes); ++p) {
    uint8_t byte = probes[p];
    uint8_t encoded[4];
    uint8_t decoded[2];
    size_t enc_len;
    int dec_len;

    enc_len = picfw_ens_encode(&byte, 1u, encoded, sizeof(encoded));

    if (byte == 0xA9u) {
      errors += expect_true(name, enc_len == 2u, "0xA9 produces 2 bytes");
      errors += expect_true(name, encoded[0] == 0xA9u && encoded[1] == 0x00u,
                            "0xA9 escape sequence");
    } else if (byte == 0xAAu) {
      errors += expect_true(name, enc_len == 2u, "0xAA produces 2 bytes");
      errors += expect_true(name, encoded[0] == 0xA9u && encoded[1] == 0x01u,
                            "0xAA escape sequence");
    } else {
      errors += expect_true(name, enc_len == 1u, "normal byte produces 1");
      errors += expect_true(name, encoded[0] == byte, "normal byte identity");
    }

    /* Round-trip decode */
    dec_len = picfw_ens_decode(encoded, enc_len, decoded, sizeof(decoded));
    errors += expect_true(name, dec_len == 1, "decode produces 1 byte");
    errors += expect_true(name, decoded[0] == byte, "round-trip identity");
  }

  return errors;
}

static int test_ens_codec_errors(void) {
  const char *name = "ens_codec_errors";
  picfw_ens_parser_t parser;
  uint8_t decoded;
  int result;
  int errors = 0;

  /* 1. Truncated escape: feed only 0xA9, verify incomplete (return 0) */
  picfw_ens_parser_init(&parser);
  result = picfw_ens_parser_feed(&parser, 0xA9u, &decoded);
  errors += expect_true(name, result == 0, "escape byte returns 0 (need more)");
  errors += expect_true(name, parser.escape_pending == 1u,
                        "escape_pending set");

  /* 2. Invalid escape: feed {0xA9, 0x02} — verify error */
  picfw_ens_parser_init(&parser);
  picfw_ens_parser_feed(&parser, 0xA9u, &decoded);
  result = picfw_ens_parser_feed(&parser, 0x02u, &decoded);
  errors += expect_true(name, result == -1, "invalid escape returns -1");

  /* 3. Bare SYN: feed 0xAA unescaped — verify error (frame boundary) */
  picfw_ens_parser_init(&parser);
  result = picfw_ens_parser_feed(&parser, 0xAAu, &decoded);
  errors += expect_true(name, result == -1, "bare SYN returns -1");

  return errors;
}

static int test_enh_codec_invalid_patterns(void) {
  const char *name = "enh_codec_invalid_patterns";
  picfw_enh_parser_t parser;
  picfw_enh_frame_t frame;
  picfw_enh_parse_result_t result;
  int errors = 0;

  /* 1. Bare continuation byte (0x80-0xBF) as first byte: ERROR */
  picfw_enh_parser_init(&parser);
  result = picfw_enh_parser_feed(&parser, 0x80u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_ERROR,
                        "bare 0x80 as first byte is ERROR");

  picfw_enh_parser_init(&parser);
  result = picfw_enh_parser_feed(&parser, 0xBFu, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_ERROR,
                        "bare 0xBF as first byte is ERROR");

  /* 2. Header-header (0xC0 then 0xC8): after F07, second header re-syncs */
  picfw_enh_parser_init(&parser);
  result = picfw_enh_parser_feed(&parser, 0xC0u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_NEED_MORE,
                        "first header returns NEED_MORE");
  result = picfw_enh_parser_feed(&parser, 0xC8u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_NEED_MORE,
                        "second header re-syncs (NEED_MORE)");
  /* Feed valid byte2, verify frame decoded */
  result = picfw_enh_parser_feed(&parser, 0x80u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_COMPLETE,
                        "valid byte2 after re-sync completes");
  errors += expect_true(name, frame.command == ((0xC8u >> 2) & 0x0Fu),
                        "re-synced command from 0xC8");

  /* 3. Short-form during pending: 0xC0 then 0x41 */
  picfw_enh_parser_init(&parser);
  result = picfw_enh_parser_feed(&parser, 0xC0u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_NEED_MORE,
                        "header starts pending");
  result = picfw_enh_parser_feed(&parser, 0x41u, &frame);
  errors += expect_true(name, result == PICFW_ENH_PARSE_COMPLETE,
                        "short-form during pending completes");
  errors += expect_true(name, frame.command == PICFW_ENH_RES_RECEIVED,
                        "short-form during pending is RECEIVED");
  errors += expect_true(name, frame.data == 0x41u,
                        "short-form during pending data=0x41");

  return errors;
}

static int test_multi_session_arbitration_isolation(void) {
  const char *name = "multi_session_arbitration_isolation";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  /* Get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* Session 1: START(0x42) */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x42u);
  picfw_runtime_step(&runtime, 1u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "session1 start count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_STARTED,
                        "session1 STARTED");
  errors += expect_true(name, frames[0].data == 0x42u,
                        "session1 initiator 0x42");

  /* SEND(0x31) */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x31u);
  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "session1 send count");
  errors += expect_true(name, frames[0].data == 0x31u,
                        "session1 echo 0x31");

  /* SEND(0xAA) — SYN ends session */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, PICFW_RUNTIME_SYN_BYTE);
  picfw_runtime_step(&runtime, 3u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, runtime.arbitration_active == PICFW_FALSE,
                        "session1 ended");

  /* Session 2: START(0x43) */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x43u);
  picfw_runtime_step(&runtime, 4u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "session2 start count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_STARTED,
                        "session2 STARTED");
  errors += expect_true(name, frames[0].data == 0x43u,
                        "session2 initiator 0x43");

  /* SEND(0x32) — should be 0x32, not 0x31 (no state leak) */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0x32u);
  picfw_runtime_step(&runtime, 5u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "session2 send count");
  errors += expect_true(name, frames[0].data == 0x32u,
                        "session2 echo 0x32 (no leak)");

  /* SEND(0xAA) — end second session */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, PICFW_RUNTIME_SYN_BYTE);
  picfw_runtime_step(&runtime, 6u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, runtime.arbitration_active == PICFW_FALSE,
                        "session2 ended");
  errors += expect_true(name, runtime.arbitration_initiator == 0u,
                        "initiator cleared after session2");

  return errors;
}

static int test_arbitration_bus_echo_suppression(void) {
  const char *name = "arbitration_bus_echo_suppression";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  /* Get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* START(0x42) — arbitration active with initiator 0x42 */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x42u);
  picfw_runtime_step(&runtime, 1u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, runtime.arbitration_active != PICFW_FALSE,
                        "arbitration active");
  errors += expect_true(name, runtime.arbitration_initiator == 0x42u,
                        "initiator is 0x42");

  /* Inject bus byte 0x42 (echo of our address) — should NOT produce RECEIVED */
  picfw_runtime_isr_enqueue_bus_byte(&runtime, 0x42u);
  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 0u,
                        "bus echo 0x42 suppressed (F22)");

  /* Inject bus byte 0x31 (different byte) — SHOULD produce RECEIVED */
  picfw_runtime_isr_enqueue_bus_byte(&runtime, 0x31u);
  picfw_runtime_step(&runtime, 3u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 1u && tx[0] == 0x31u,
                        "non-echo 0x31 forwarded");

  /* Inject bus byte 0xAA (SYN) — SHOULD produce RECEIVED, clears arbitration */
  picfw_runtime_isr_enqueue_bus_byte(&runtime, PICFW_RUNTIME_SYN_BYTE);
  picfw_runtime_step(&runtime, 4u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "SYN forwarded count");
  errors += expect_true(name, frames[0].data == PICFW_RUNTIME_SYN_BYTE,
                        "SYN forwarded data");
  errors += expect_true(name, runtime.arbitration_active == PICFW_FALSE,
                        "arbitration cleared after SYN");

  return errors;
}

static int test_invalid_info_id(void) {
  const char *name = "invalid_info_id";
  picfw_runtime_t runtime;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  picfw_enh_frame_t frames[8];
  size_t tx_len;
  size_t frame_count;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  /* Get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name,
                        runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                        "initially LIVE_READY");

  /* Send REQ_INFO with data=8 (>= PICFW_RUNTIME_INFO_COUNT=8) */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INFO, 8u);
  picfw_runtime_step(&runtime, 1u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "error response count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_ERROR_HOST,
                        "error command ERROR_HOST");
  errors += expect_true(name, frames[0].data == 2u,
                        "error code UNSUPPORTED_INFO=2");
  errors += expect_true(name, runtime.startup_state == PICFW_STARTUP_DEGRADED,
                        "state DEGRADED");

  /* Send REQ_INIT to recover */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x55u);
  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  frame_count =
      collect_frames(tx, tx_len, frames, sizeof(frames) / sizeof(frames[0]));
  errors += expect_true(name, frame_count == 1u, "recovery frame count");
  errors += expect_true(name, frames[0].command == PICFW_ENH_RES_RESETTED,
                        "recovery RESETTED");
  errors += expect_true(name,
                        runtime.startup_state == PICFW_STARTUP_LIVE_READY,
                        "recovered to LIVE_READY");

  return errors;
}

static int test_short_form_send_remap(void) {
  const char *name = "short_form_send_remap";
  picfw_runtime_t runtime;
  picfw_runtime_config_t config;
  uint8_t tx[32];
  size_t tx_len;
  int errors = 0;

  picfw_runtime_config_init_default(&config);
  picfw_runtime_init(&runtime, &config);

  /* Init to get to LIVE_READY */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_INIT, 0x01u);
  picfw_runtime_step(&runtime, 0u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* Start arbitration session */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_START, 0x42u);
  picfw_runtime_step(&runtime, 1u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* Send short-form byte 0x31 (raw, not ENH-encoded) */
  picfw_runtime_isr_enqueue_host_byte(&runtime, 0x31u);
  picfw_runtime_step(&runtime, 2u);
  tx_len = picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));

  /* Should produce RECEIVED echo of 0x31 (short-form: 1 byte) */
  errors += expect_true(name, tx_len == 1u,
                        "short-form SEND produces 1-byte echo");
  errors += expect_true(name, tx[0] == 0x31u,
                        "echo matches sent byte");

  /* End session with ENH-encoded SEND(0xAA) — 0xAA >= 0x80 so it uses
     2-byte ENH encoding, not the short-form path */
  enqueue_enh_frame(&runtime, PICFW_ENH_REQ_SEND, 0xAAu);
  picfw_runtime_step(&runtime, 3u);
  picfw_runtime_drain_host_tx(&runtime, tx, sizeof(tx));
  errors += expect_true(name, runtime.arbitration_active == PICFW_FALSE,
                        "session ended after SEND(SYN)");

  return errors;
}

static int test_protocol_dispatch_error_paths(void) {
  const char *name = "protocol_dispatch_error_paths";
  picfw_runtime_t runtime;
  uint8_t return_code;
  int errors = 0;

  picfw_runtime_init(&runtime, 0);
  runtime.now_ms = 0x00000100u;

  /* Test 1: dispatch_flags_scan with invalid flags (not RETRY, not SCAN).
   * flags=0x00 falls to dispatch_flags_scan, which rejects flags != 0x03. */
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0x00u;
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            &runtime, 0x05u, &return_code) == PICFW_FALSE,
                        "invalid flags rejected");
  errors += expect_true(name, return_code == (0x00u ^ 0x03u),
                        "invalid flags return_code = flags ^ SCAN");

  /* Test 2: dispatch_flags_retry with state=RETRY but wrong protocol_code.
   * flags=0x01, state=RETRY(8), code=0x06 (not DEFAULT=0x05) -> rejected. */
  runtime.protocol_state = PICFW_PROTOCOL_STATE_RETRY;
  runtime.protocol_state_flags = 0x01u;
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            &runtime, 0x06u, &return_code) == PICFW_FALSE,
                        "retry wrong code rejected");
  errors += expect_true(name, return_code == (0x06u ^ 0x05u),
                        "retry wrong code return_code = code ^ DEFAULT");

  /* Test 3: dispatch_flags_retry with unexpected state (IDLE=0, not
   * PENDING/READY/RETRY). flags=0x01, state=IDLE -> rejected. */
  runtime.protocol_state = PICFW_PROTOCOL_STATE_IDLE;
  runtime.protocol_state_flags = 0x01u;
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            &runtime, 0x05u, &return_code) == PICFW_FALSE,
                        "retry unexpected state rejected");
  errors += expect_true(name, return_code == (0x00u ^ 0x08u),
                        "retry unexpected state return_code = IDLE ^ RETRY");

  /* Test 4: NULL runtime returns FALSE with return_code = 0xFF. */
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            NULL, 0x05u, &return_code) == PICFW_FALSE,
                        "null runtime rejected");
  errors += expect_true(name, return_code == 0xFFu,
                        "null runtime return_code = 0xFF");

  /* Test 5: dispatch_flags_retry PENDING + wrong protocol_code.
   * flags=0x01(RETRY), state=PENDING(1), code=0x05(DEFAULT, not SLOT_03=0x02)
   * -> rejected with return_code = 0x05 ^ 0x02 = 0x07. */
  runtime.protocol_state = PICFW_PROTOCOL_STATE_PENDING;
  runtime.protocol_state_flags = 0x01u;
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            &runtime, 0x05u, &return_code) == PICFW_FALSE,
                        "retry pending wrong code rejected");
  errors += expect_true(name, return_code == (0x05u ^ 0x02u),
                        "retry pending wrong code return_code = code ^ SLOT_03");

  /* Test 6: dispatch_flags_scan with flags=SCAN but state != READY.
   * flags=0x03(SCAN), state=IDLE(0) -> rejected with
   * return_code = 0x00 ^ 0x03 = 0x03 (state ^ READY). */
  runtime.protocol_state = PICFW_PROTOCOL_STATE_IDLE;
  runtime.protocol_state_flags = 0x03u;
  return_code = 0u;
  errors += expect_true(name,
                        picfw_runtime_protocol_state_dispatch(
                            &runtime, 0x05u, &return_code) == PICFW_FALSE,
                        "scan wrong state rejected");
  errors += expect_true(name, return_code == (0x00u ^ 0x03u),
                        "scan wrong state return_code = IDLE ^ READY");

  return errors;
}

static int test_signal_detect_gates_status_emission(void) {
  const char *name = "signal_detect_gates_status";
  picfw_pic16f15356_app_t app;
  picfw_runtime_config_t config;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  size_t tx_len;
  int errors = 0;
  uint16_t tick;

  picfw_runtime_config_init_default(&config);
  config.status_emit_enabled = PICFW_TRUE;
  config.status_snapshot_period_ms = 100u;
  config.status_variant_period_ms = 100u;
  config.init_features = 0x01u;
  picfw_pic16f15356_app_init(&app, &config);

  /* Send INIT to reach LIVE_READY state */
  picfw_pic16f15356_app_isr_host_rx(&app, 0xC0u); /* INIT byte1 */
  picfw_pic16f15356_app_isr_host_rx(&app, 0x81u); /* INIT byte2 (cmd=0, data=1) */
  /* Advance one scheduler tick to process INIT */
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  picfw_pic16f15356_app_mainline_service(&app);
  /* Drain RESETTED response */
  picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));

  /* Advance time past the status deadline (2 scheduler ticks = 200ms) */
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }

  /* Test 1: Bus busy (RB1 HIGH) — status emission should be deferred */
  app.hal.latches.portb_input = 0x02u; /* RB1=1 = bus busy */
  picfw_pic16f15356_app_mainline_service(&app);
  tx_len = picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 0u,
                        "no status frame when bus busy");

  /* Test 2: Bus idle (RB1 LOW) — status emission should proceed */
  app.hal.latches.portb_input = 0x00u; /* RB1=0 = bus idle */
  picfw_pic16f15356_app_mainline_service(&app);
  tx_len = picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));
  errors += expect_true(name, tx_len > 0u,
                        "status frame emitted when bus idle");

  /* Test 3: bus_busy field is correctly set from signal detect */
  app.hal.latches.portb_input = 0x02u;
  picfw_pic16f15356_app_mainline_service(&app);
  errors += expect_true(name, app.runtime.bus_busy == PICFW_TRUE,
                        "bus_busy set when RB1 high");
  app.hal.latches.portb_input = 0x00u;
  picfw_pic16f15356_app_mainline_service(&app);
  errors += expect_true(name, app.runtime.bus_busy == PICFW_FALSE,
                        "bus_busy cleared when RB1 low");

  return errors;
}

static int test_wifi_check_startup_gate(void) {
  const char *name = "wifi_check_startup_gate";
  picfw_pic16f15356_app_t app;
  picfw_runtime_config_t config;
  uint8_t tx[PICFW_RUNTIME_HOST_TX_CAP];
  size_t tx_len;
  int errors = 0;
  uint16_t tick;

  picfw_runtime_config_init_default(&config);
  config.status_emit_enabled = PICFW_TRUE;
  config.init_features = 0x01u;

  /* --- WiFi variant: startup should be gated --- */
  picfw_pic16f15356_app_init(&app, &config);
  /* Directly set WiFi variant state (strap decode tested in test_gpio) */
  app.hal.wifi_variant = PICFW_TRUE;
  app.hal.wifi_ready = PICFW_FALSE;
  picfw_led_set_state(&app.hal.led, PICFW_LED_BLINK_SLOW, 0u);

  errors += expect_true(name, app.hal.wifi_variant == PICFW_TRUE,
                        "WiFi variant detected from straps");
  errors += expect_true(name, app.hal.wifi_ready == PICFW_FALSE,
                        "WiFi not ready at init");
  errors += expect_true(name, app.hal.led.state == PICFW_LED_BLINK_SLOW,
                        "LED blinks slow while waiting");

  /* Send INIT — should NOT be processed (gate blocks runtime_step) */
  picfw_pic16f15356_app_isr_host_rx(&app, 0xC0u);
  picfw_pic16f15356_app_isr_host_rx(&app, 0x81u);
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  picfw_pic16f15356_app_mainline_service(&app);
  tx_len = picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));
  errors += expect_true(name, tx_len == 0u,
                        "no TX output while WiFi gate active");

  /* Wemos becomes ready: set RB0 HIGH */
  app.hal.latches.portb_input |= 0x01u; /* RB0=1 */
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  picfw_pic16f15356_app_mainline_service(&app);
  errors += expect_true(name, app.hal.wifi_ready == PICFW_TRUE,
                        "WiFi ready after RB0 HIGH");
  errors += expect_true(name, app.hal.led.state == PICFW_LED_FADE_UP,
                        "LED transitions to FADE_UP");

  /* Now runtime_step should run — re-send INIT */
  picfw_pic16f15356_app_isr_host_rx(&app, 0xC0u);
  picfw_pic16f15356_app_isr_host_rx(&app, 0x81u);
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  picfw_pic16f15356_app_mainline_service(&app);
  tx_len = picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));
  errors += expect_true(name, tx_len > 0u,
                        "INIT processed after WiFi ready");

  /* --- Non-WiFi variant: no gate --- */
  picfw_pic16f15356_app_init(&app, &config);
  /* Default straps: RA0=1,RA1=1 = RPi/USB (not WiFi) */
  errors += expect_true(name, app.hal.wifi_variant == PICFW_FALSE,
                        "non-WiFi variant (RPi/USB)");

  /* INIT should be processed immediately */
  picfw_pic16f15356_app_isr_host_rx(&app, 0xC0u);
  picfw_pic16f15356_app_isr_host_rx(&app, 0x81u);
  for (tick = 0u; tick <= PICFW_PIC16F15356_TMR0_ISR_DIVIDER; ++tick) {
    picfw_pic16f15356_app_isr_tmr0(&app);
  }
  picfw_pic16f15356_app_mainline_service(&app);
  tx_len = picfw_pic16f15356_app_drain_host_tx(&app, tx, sizeof(tx));
  errors += expect_true(name, tx_len > 0u,
                        "non-WiFi: INIT processed immediately");

  /* wifi_check null guard */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_wifi_check(0) == PICFW_FALSE,
                        "wifi_check null guard");

  return errors;
}

static int test_led_state_machine(void) {
  const char *name = "led_state_machine";
  picfw_led_t led;
  picfw_bool_t out;
  int errors = 0;
  uint32_t now = 1000u;

  /* Init: OFF state, output = false */
  picfw_led_init(&led);
  errors += expect_true(name, led.state == PICFW_LED_OFF, "init state OFF");
  out = picfw_led_service(&led, now, 0u);
  errors += expect_true(name, out == PICFW_FALSE, "OFF output is false");

  /* Null guard */
  out = picfw_led_service(0, now, 0u);
  errors += expect_true(name, out == PICFW_FALSE, "null guard returns false");
  picfw_led_init(0); /* no crash */
  picfw_led_set_state(0, PICFW_LED_NORMAL, now); /* no crash */

  /* BLINK_SLOW: toggle at 500ms intervals */
  picfw_led_set_state(&led, PICFW_LED_BLINK_SLOW, now);
  out = picfw_led_service(&led, now, 0u);
  errors += expect_true(name, out == PICFW_FALSE, "blink_slow starts off");
  out = picfw_led_service(&led, now + 500u, 0u);
  errors += expect_true(name, out == PICFW_TRUE, "blink_slow toggles on at 500ms");
  out = picfw_led_service(&led, now + 1000u, 0u);
  errors += expect_true(name, out == PICFW_FALSE, "blink_slow toggles off at 1000ms");

  /* BLINK_FAST: toggle at 100ms */
  picfw_led_set_state(&led, PICFW_LED_BLINK_FAST, now);
  out = picfw_led_service(&led, now + 100u, 0u);
  errors += expect_true(name, out == PICFW_TRUE, "blink_fast toggles at 100ms");

  /* BLINK_VERY_FAST: toggle at 50ms */
  picfw_led_set_state(&led, PICFW_LED_BLINK_VERY_FAST, now);
  out = picfw_led_service(&led, now + 50u, 0u);
  errors += expect_true(name, out == PICFW_TRUE, "blink_very_fast toggles at 50ms");

  /* FADE_UP: steps through, then transitions to prev_state */
  led.prev_state = PICFW_LED_NORMAL;
  picfw_led_set_state(&led, PICFW_LED_FADE_UP, now);
  errors += expect_true(name, led.fade_step == 0u, "fade starts at step 0");
  /* Advance through all fade steps */
  {
    uint8_t step;
    for (step = 0u; step < PICFW_LED_FADE_STEPS; step++) {
      now += PICFW_LED_FADE_STEP_MS;
      out = picfw_led_service(&led, now, 0u);
    }
  }
  errors += expect_true(name, led.state == PICFW_LED_NORMAL,
                        "fade complete -> NORMAL");

  /* NORMAL: always on, ping after 4s */
  now = 10000u;
  picfw_led_set_state(&led, PICFW_LED_NORMAL, now);
  out = picfw_led_service(&led, now, 0u);
  errors += expect_true(name, out == PICFW_TRUE, "NORMAL is on");
  /* Advance to ping deadline */
  out = picfw_led_service(&led, now + PICFW_LED_PING_PERIOD_MS, 0u);
  errors += expect_true(name, led.state == PICFW_LED_PING,
                        "NORMAL -> PING after 4s");
  /* Ping expires */
  out = picfw_led_service(&led, now + PICFW_LED_PING_PERIOD_MS +
                                PICFW_LED_PING_DURATION_MS, 0u);
  errors += expect_true(name, led.state == PICFW_LED_NORMAL,
                        "PING -> NORMAL after 100ms");

  /* LOW: same as NORMAL but conceptually dim */
  now = 20000u;
  picfw_led_set_state(&led, PICFW_LED_LOW, now);
  out = picfw_led_service(&led, now, 0u);
  errors += expect_true(name, out == PICFW_TRUE, "LOW is on (sim: always on)");
  out = picfw_led_service(&led, now + PICFW_LED_PING_PERIOD_MS, 0u);
  errors += expect_true(name, led.state == PICFW_LED_PING,
                        "LOW -> PING after 4s");
  /* Verify PING returns to LOW (not NORMAL) */
  out = picfw_led_service(&led, now + PICFW_LED_PING_PERIOD_MS +
                                PICFW_LED_PING_DURATION_MS, 0u);
  errors += expect_true(name, led.state == PICFW_LED_LOW,
                        "PING -> LOW (not NORMAL)");

  /* Out-of-range state: bounds check returns FALSE */
  led.state = 99u;
  out = picfw_led_service(&led, now, 0u);
  errors += expect_true(name, out == PICFW_FALSE,
                        "out-of-range state returns false");

  /* BRIGHT via INIT flag: 2 seconds */
  now = 30000u;
  picfw_led_set_state(&led, PICFW_LED_NORMAL, now);
  out = picfw_led_service(&led, now, PICFW_LED_FLAG_INIT_CMD);
  errors += expect_true(name, led.state == PICFW_LED_BRIGHT,
                        "INIT flag -> BRIGHT");
  errors += expect_true(name, out == PICFW_TRUE, "BRIGHT is on");
  /* Before deadline: still BRIGHT */
  out = picfw_led_service(&led, now + 1999u, 0u);
  errors += expect_true(name, led.state == PICFW_LED_BRIGHT,
                        "BRIGHT persists before 2s");
  /* At deadline: returns to prev_state (NORMAL) */
  out = picfw_led_service(&led, now + 2000u, 0u);
  errors += expect_true(name, led.state == PICFW_LED_NORMAL,
                        "BRIGHT -> NORMAL after 2s");

  /* BRIGHT via ERROR flag: 5 seconds */
  now = 40000u;
  picfw_led_set_state(&led, PICFW_LED_NORMAL, now);
  out = picfw_led_service(&led, now, PICFW_LED_FLAG_ERROR);
  errors += expect_true(name, led.state == PICFW_LED_BRIGHT,
                        "ERROR flag -> BRIGHT");
  out = picfw_led_service(&led, now + 5000u, 0u);
  errors += expect_true(name, led.state == PICFW_LED_NORMAL,
                        "BRIGHT -> NORMAL after 5s (error)");

  /* Dual-flag: both INIT + ERROR simultaneously — ERROR wins (5s),
   * prev_state must be NORMAL (not corrupted to BRIGHT) */
  now = 50000u;
  picfw_led_set_state(&led, PICFW_LED_NORMAL, now);
  out = picfw_led_service(&led, now,
                          PICFW_LED_FLAG_INIT_CMD | PICFW_LED_FLAG_ERROR);
  errors += expect_true(name, led.state == PICFW_LED_BRIGHT,
                        "dual flags -> BRIGHT");
  errors += expect_true(name, led.prev_state == PICFW_LED_NORMAL,
                        "dual flags: prev_state = NORMAL (not BRIGHT)");
  /* After 5s (ERROR duration), should return to NORMAL */
  out = picfw_led_service(&led, now + 5000u, 0u);
  errors += expect_true(name, led.state == PICFW_LED_NORMAL,
                        "dual flags: BRIGHT -> NORMAL after 5s");

  return errors;
}

static int test_gpio_and_pin_model(void) {
  const char *name = "gpio_and_pin_model";
  picfw_pic16f15356_hal_t hal;
  picfw_pic16f15356_straps_t straps;
  int errors = 0;

  picfw_pic16f15356_hal_runtime_init(&hal);

  /* GPIO register initialization */
  errors += expect_true(name, hal.regs.trisa == 0x37u, "TRISA init");
  errors += expect_true(name, hal.regs.trisb == 0x07u, "TRISB init");
  errors += expect_true(name, hal.regs.trisc == 0x09u, "TRISC init");
  errors += expect_true(name, hal.regs.ansela == 0x04u, "ANSELA init");
  errors += expect_true(name, hal.regs.anselb == 0x00u, "ANSELB init");
  errors += expect_true(name, hal.regs.anselc == 0x00u, "ANSELC init");
  errors += expect_true(name, hal.regs.wpub == 0x02u, "WPUB init (RB1 pull-up)");

  /* PPS configuration */
  errors += expect_true(name, hal.regs.rx1pps == PICFW_PPS_RB2_INPUT,
                        "RX1PPS routes to RB2");
  errors += expect_true(name, hal.regs.rb3pps == PICFW_PPS_EUSART1_TX,
                        "RB3PPS routes EUSART1 TX");
  errors += expect_true(name, hal.regs.rx2pps == PICFW_PPS_RC0_INPUT,
                        "RX2PPS routes to RC0");
  errors += expect_true(name, hal.regs.rc1pps_out == PICFW_PPS_EUSART2_TX,
                        "RC1PPS routes EUSART2 TX");

  /* EUSART2 registers */
  errors += expect_true(name, hal.regs.baud2con == 0x08u, "BAUD2CON init");
  errors += expect_true(name, hal.regs.rc2sta == 0x90u, "RC2STA init");
  errors += expect_true(name, hal.regs.tx2sta == 0x24u, "TX2STA init");
  errors += expect_true(name, hal.regs.sp2brgl == hal.regs.sp1brgl,
                        "EUSART2 baud matches EUSART1");

  /* GPIO read: default signal-detect (RB1=1, bus present) */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_signal_detect(&hal) == PICFW_TRUE,
                        "signal detect default high");

  /* GPIO read: simulate no signal (clear RB1) */
  hal.latches.portb_input = 0x00u;
  errors += expect_true(name,
                        picfw_pic16f15356_hal_signal_detect(&hal) == PICFW_FALSE,
                        "signal detect low after clear");

  /* GPIO write: set LATB bit 0 */
  picfw_pic16f15356_hal_write_pin(&hal, PICFW_PORT_B, 0u, PICFW_TRUE);
  errors += expect_true(name, (hal.regs.latb & 1u) == 1u, "write pin sets LAT bit");
  picfw_pic16f15356_hal_write_pin(&hal, PICFW_PORT_B, 0u, PICFW_FALSE);
  errors += expect_true(name, (hal.regs.latb & 1u) == 0u, "write pin clears LAT bit");

  /* GPIO read: pin from PORTA */
  hal.latches.porta_input = 0x10u; /* RA4 = 1 */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, PICFW_PORT_A, 4u) == PICFW_TRUE,
                        "read RA4 high");
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, PICFW_PORT_A, 0u) == PICFW_FALSE,
                        "read RA0 low");

  /* Null guards */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(0, PICFW_PORT_A, 0u) == PICFW_FALSE,
                        "read pin null guard");
  errors += expect_true(name,
                        picfw_pic16f15356_hal_signal_detect(0) == PICFW_FALSE,
                        "signal detect null guard");

  /* Strap read: default (all high = enhanced, normal speed, RPi/USB) */
  hal.latches.porta_input = 0x33u; /* RA0=1, RA1=1, RA4=1, RA5=1 */
  picfw_pic16f15356_hal_read_straps(&hal, &straps);
  errors += expect_true(name, straps.enhanced_protocol == PICFW_TRUE,
                        "strap: enhanced protocol (pin high)");
  errors += expect_true(name, straps.high_speed == PICFW_FALSE,
                        "strap: normal speed (pin high -> !high_speed)");
  errors += expect_true(name, straps.variant == PICFW_VARIANT_RPI_USB,
                        "strap: RPi/USB variant");

  /* Strap read: high-speed + ethernet */
  hal.latches.porta_input = 0x10u; /* RA4=1(enhanced), RA5=0(high-speed), RA0=0, RA1=0 */
  picfw_pic16f15356_hal_read_straps(&hal, &straps);
  errors += expect_true(name, straps.high_speed == PICFW_TRUE,
                        "strap: high-speed (pin low)");
  errors += expect_true(name, straps.variant == PICFW_VARIANT_ETHERNET,
                        "strap: Ethernet (both low)");

  /* TX ISR handlers */
  errors += expect_true(name, hal.latches.host_tx_ready == PICFW_FALSE,
                        "host_tx_ready initially false");
  picfw_pic16f15356_isr_latch_host_tx_ready(&hal);
  errors += expect_true(name, hal.latches.host_tx_ready == PICFW_TRUE,
                        "host_tx_ready set by ISR");
  picfw_pic16f15356_isr_latch_bus_tx_ready(&hal);
  errors += expect_true(name, hal.latches.bus_tx_ready == PICFW_TRUE,
                        "bus_tx_ready set by ISR");

  /* TX ISR null guards */
  picfw_pic16f15356_isr_latch_host_tx_ready(0);
  picfw_pic16f15356_isr_latch_bus_tx_ready(0);
  /* (no crash = pass) */

  /* TX-ready consumption by mainline service */
  {
    picfw_pic16f15356_app_t app;
    picfw_runtime_config_t cfg;
    picfw_runtime_config_init_default(&cfg);
    picfw_pic16f15356_app_init(&app, &cfg);
    picfw_pic16f15356_isr_latch_host_tx_ready(&app.hal);
    picfw_pic16f15356_isr_latch_bus_tx_ready(&app.hal);
    errors += expect_true(name, app.hal.latches.host_tx_ready == PICFW_TRUE,
                          "host_tx_ready before service");
    /* Trigger a mainline service cycle */
    picfw_pic16f15356_isr_latch_tmr0(&app.hal);
    app.hal.latches.scheduler_subticks = PICFW_PIC16F15356_TMR0_ISR_DIVIDER;
    picfw_pic16f15356_isr_latch_tmr0(&app.hal);
    picfw_pic16f15356_app_mainline_service(&app);
    errors += expect_true(name, app.hal.latches.host_tx_ready == PICFW_FALSE,
                          "host_tx_ready cleared after service");
    errors += expect_true(name, app.hal.latches.bus_tx_ready == PICFW_FALSE,
                          "bus_tx_ready cleared after service");
  }

  /* App-level TX ISR wrappers */
  {
    picfw_pic16f15356_app_t app;
    picfw_runtime_config_t cfg;
    picfw_runtime_config_init_default(&cfg);
    picfw_pic16f15356_app_init(&app, &cfg);
    picfw_pic16f15356_app_isr_host_tx_ready(&app);
    errors += expect_true(name, app.hal.latches.host_tx_ready == PICFW_TRUE,
                          "app host_tx_ready forwarded");
    picfw_pic16f15356_app_isr_bus_tx_ready(&app);
    errors += expect_true(name, app.hal.latches.bus_tx_ready == PICFW_TRUE,
                          "app bus_tx_ready forwarded");
    /* Null guards */
    picfw_pic16f15356_app_isr_host_tx_ready(0);
    picfw_pic16f15356_app_isr_bus_tx_ready(0);
  }

  /* PPS constants (DS40001866 Table 15-2) */
  errors += expect_true(name, PICFW_PPS_EUSART1_TX == 0x0Fu,
                        "PPS TX1/CK1 = 0x0F per datasheet");
  errors += expect_true(name, PICFW_PPS_EUSART2_TX == 0x11u,
                        "PPS TX2/CK2 = 0x11 per datasheet");

  /* GPIO edge cases */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, 99u, 0u) == PICFW_FALSE,
                        "read_pin invalid port returns false");
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, PICFW_PORT_A, 8u) == PICFW_FALSE,
                        "read_pin bit>7 returns false");
  picfw_pic16f15356_hal_write_pin(&hal, 99u, 0u, PICFW_TRUE); /* no crash */
  picfw_pic16f15356_hal_write_pin(&hal, PICFW_PORT_A, 8u, PICFW_TRUE); /* no crash */
  picfw_pic16f15356_hal_write_pin(0, PICFW_PORT_A, 0u, PICFW_TRUE); /* null guard */

  /* GPIO read Port C */
  hal.latches.portc_input = 0x01u; /* RC0=1 */
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, PICFW_PORT_C, 0u) == PICFW_TRUE,
                        "read RC0 high");
  errors += expect_true(name,
                        picfw_pic16f15356_hal_read_pin(&hal, PICFW_PORT_C, 1u) == PICFW_FALSE,
                        "read RC1 low");

  /* GPIO write Port A and Port C */
  picfw_pic16f15356_hal_write_pin(&hal, PICFW_PORT_A, 3u, PICFW_TRUE);
  errors += expect_true(name, (hal.regs.lata & 0x08u) != 0u, "write LATA bit 3");
  picfw_pic16f15356_hal_write_pin(&hal, PICFW_PORT_C, 1u, PICFW_TRUE);
  errors += expect_true(name, (hal.regs.latc & 0x02u) != 0u, "write LATC bit 1");

  /* Strap: WIFI variant (A low, B high) */
  hal.latches.porta_input = 0x32u; /* RA0=0, RA1=1, RA4=1, RA5=1 */
  picfw_pic16f15356_hal_read_straps(&hal, &straps);
  errors += expect_true(name, straps.variant == PICFW_VARIANT_WIFI,
                        "strap: WIFI variant (A low, B high)");

  /* Strap: standard protocol (RA4 low) */
  hal.latches.porta_input = 0x23u; /* RA0=1, RA1=1, RA4=0, RA5=1 */
  picfw_pic16f15356_hal_read_straps(&hal, &straps);
  errors += expect_true(name, straps.enhanced_protocol == PICFW_FALSE,
                        "strap: standard protocol (RA4 low)");

  /* Strap: null HAL guard (output pre-zeroed) */
  memset(&straps, 0xFF, sizeof(straps));
  picfw_pic16f15356_hal_read_straps(0, &straps);
  errors += expect_true(name, straps.enhanced_protocol == PICFW_FALSE,
                        "strap: null HAL zeroes output");

  /* PPS null guard */
  picfw_pic16f15356_hal_configure_pps(0); /* no crash */

  /* Strap defaults after fresh init (should be enhanced/normal/RPi) */
  picfw_pic16f15356_hal_runtime_init(&hal);
  picfw_pic16f15356_hal_read_straps(&hal, &straps);
  errors += expect_true(name, straps.enhanced_protocol == PICFW_TRUE,
                        "fresh init strap: enhanced protocol");
  errors += expect_true(name, straps.high_speed == PICFW_FALSE,
                        "fresh init strap: normal speed");
  errors += expect_true(name, straps.variant == 0u,
                        "fresh init strap: RPi/USB");

  return errors;
}

int main(void) {
  if (test_pic16f15356_platform_model() != 0) {
    return 1;
  }
  if (test_pic16f15356_hal_scaffold() != 0) {
    return 1;
  }
  if (test_pic16f15356_app_shell() != 0) {
    return 1;
  }
  if (test_runtime_init_and_info() != 0) {
    return 1;
  }
  if (test_runtime_start_send_and_boundary_release() != 0) {
    return 1;
  }
  if (test_runtime_start_cancel_and_failure_injection() != 0) {
    return 1;
  }
  if (test_runtime_periodic_status_and_state() != 0) {
    return 1;
  }
  if (test_deadline_wrap_compare() != 0) {
    return 1;
  }
  if (test_scan_helpers_and_step_budget() != 0) {
    return 1;
  }
  if (test_scan_window_and_protocol_dispatch() != 0) {
    return 1;
  }
  if (test_scan_command_family_and_status_builders() != 0) {
    return 1;
  }
  if (test_compute_next_scan_cursor() != 0) {
    return 1;
  }
  if (test_rotation_primitives() != 0) {
    return 1;
  }
  if (test_descriptor_read_u32() != 0) {
    return 1;
  }
  if (test_descriptor_merge_with_seed() != 0) {
    return 1;
  }
  if (test_post_merge_validate() != 0) {
    return 1;
  }
  if (test_load_descriptor_block() != 0) {
    return 1;
  }
  if (test_scan_mask_functions() != 0) {
    return 1;
  }
  if (test_deep_scan_fsm_with_descriptors() != 0) {
    return 1;
  }
  if (test_initialize_scan_slot_full() != 0) {
    return 1;
  }
  if (test_recompute_scan_masks_tail() != 0) {
    return 1;
  }
  if (test_seed_recomputation_chain() != 0) {
    return 1;
  }
  if (test_slot_level_operations() != 0) {
    return 1;
  }
  if (test_app_main_loop() != 0) {
    return 1;
  }
  if (test_bus_byte_forwarding_full_range() != 0) {
    return 1;
  }
  if (test_host_parser_timeout() != 0) {
    return 1;
  }
  if (test_event_queue_overflow() != 0) {
    return 1;
  }
  if (test_tx_queue_overflow_and_degraded() != 0) {
    return 1;
  }
  if (test_ens_codec_roundtrip() != 0) {
    return 1;
  }
  if (test_ens_codec_errors() != 0) {
    return 1;
  }
  if (test_enh_codec_invalid_patterns() != 0) {
    return 1;
  }
  if (test_multi_session_arbitration_isolation() != 0) {
    return 1;
  }
  if (test_arbitration_bus_echo_suppression() != 0) {
    return 1;
  }
  if (test_invalid_info_id() != 0) {
    return 1;
  }
  if (test_short_form_send_remap() != 0) {
    return 1;
  }
  if (test_protocol_dispatch_error_paths() != 0) {
    return 1;
  }
  if (test_gpio_and_pin_model() != 0) {
    return 1;
  }
  if (test_signal_detect_gates_status_emission() != 0) {
    return 1;
  }
  if (test_led_state_machine() != 0) {
    return 1;
  }
  if (test_wifi_check_startup_gate() != 0) {
    return 1;
  }
  printf("runtime tests passed\n");
  return 0;
}
