#ifndef PICFW_RUNTIME_H
#define PICFW_RUNTIME_H

#include "codec_enh.h"
#include "codec_ens.h"
#include "common.h"
#include "info.h"
#include "platform_model.h"

#define PICFW_RUNTIME_INFO_COUNT 8u
#define PICFW_RUNTIME_INFO_PAYLOAD_MAX 8u
#define PICFW_RUNTIME_EVENT_QUEUE_CAP 32u
#define PICFW_RUNTIME_HOST_TX_CAP 96u
#define PICFW_RUNTIME_HOST_RX_TIMEOUT_MS 64u
#define PICFW_RUNTIME_SYN_BYTE 0xAAu
#define PICFW_RUNTIME_ERROR_SEND_WITHOUT_SESSION 5u
#define PICFW_RUNTIME_STEP_EVENT_BUDGET 8u
#define PICFW_RUNTIME_STATUS_EMISSION_BUDGET 2u
#define PICFW_RUNTIME_DRAIN_BUDGET 32u
#define PICFW_RUNTIME_STATUS_FRAME_MAX 24u
#define PICFW_RUNTIME_SCAN_MIN_DELAY_MS 0x3Cu
#define PICFW_RUNTIME_SCAN_DEFAULT_TICK 0x00000140u
#define PICFW_RUNTIME_SCAN_DEFAULT_DEADLINE 0x000001A4u
#define PICFW_RUNTIME_SCAN_DEFAULT_WINDOW_LIMIT 0x00000156u
#define PICFW_RUNTIME_SCAN_DEFAULT_SEED 0x000002A4u
#define PICFW_RUNTIME_SCAN_DEFAULT_MERGED_WINDOW 0x000001A8u
#define PICFW_RUNTIME_SCAN_DEFAULT_SLOT 0x06u
#define PICFW_RUNTIME_SCAN_DEFAULT_DESCRIPTOR_CURSOR 0x00E9u
#define PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR 0x000000F0u
#define PICFW_RUNTIME_SCAN_DELAY_THRESHOLD 0x00000078u
#define PICFW_RUNTIME_SCAN_MERGED_THRESHOLD 0x000000D2u
#define PICFW_RUNTIME_DESCRIPTOR_DATA_CAP 48u
#define PICFW_RUNTIME_DESCRIPTOR_XOR_KEY 99u
#define PICFW_RUNTIME_DESCRIPTOR_VALIDATE_SEED 2u
#define PICFW_RUNTIME_PLATFORM_ISR_PERIOD_US picfw_pic16f15356_tmr0_isr_period_us()
#define PICFW_RUNTIME_PLATFORM_SCHEDULER_PERIOD_MS picfw_pic16f15356_scheduler_period_ms()

typedef enum picfw_protocol_state {
  PICFW_PROTOCOL_STATE_IDLE = 0u,
  PICFW_PROTOCOL_STATE_PENDING = 1u,
  PICFW_PROTOCOL_STATE_ARMED = 2u,
  PICFW_PROTOCOL_STATE_READY = 3u,
  PICFW_PROTOCOL_STATE_VARIANT = 5u,
  PICFW_PROTOCOL_STATE_OFFSET_SCAN = 6u,
  PICFW_PROTOCOL_STATE_SCAN = 7u,
  PICFW_PROTOCOL_STATE_RETRY = 8u,
} picfw_protocol_state_t;

typedef enum picfw_runtime_scan_phase {
  PICFW_RUNTIME_SCAN_PHASE_IDLE = 0u,
  PICFW_RUNTIME_SCAN_PHASE_PRIMED = 1u,
  PICFW_RUNTIME_SCAN_PHASE_PASS = 2u,
  PICFW_RUNTIME_SCAN_PHASE_RETRY = 3u,
} picfw_runtime_scan_phase_t;

typedef enum picfw_runtime_scan_sub_phase {
    PICFW_SCAN_SUB_PHASE_RESET = 0u,
    PICFW_SCAN_SUB_PHASE_PROBED = 1u,
    PICFW_SCAN_SUB_PHASE_POLLING = 2u,
    PICFW_SCAN_SUB_PHASE_COMPLETE = 3u,
} picfw_runtime_scan_sub_phase_t;

typedef struct picfw_runtime_config {
  uint8_t init_features;
  picfw_bool_t start_should_fail;
  uint8_t start_failure_winner;
  picfw_bool_t status_emit_enabled;
  uint16_t status_snapshot_period_ms;
  uint16_t status_variant_period_ms;
  uint8_t status_snapshot_payload_len;
  uint8_t status_snapshot_payload[PICFW_RUNTIME_INFO_PAYLOAD_MAX];
  uint8_t status_variant_payload_len;
  uint8_t status_variant_payload[PICFW_RUNTIME_INFO_PAYLOAD_MAX];
  uint8_t info_payload_len[PICFW_RUNTIME_INFO_COUNT];
  uint8_t info_payload[PICFW_RUNTIME_INFO_COUNT][PICFW_RUNTIME_INFO_PAYLOAD_MAX];
} picfw_runtime_config_t;

typedef enum picfw_runtime_event_type {
  PICFW_RUNTIME_EVENT_HOST_BYTE = 1,
  PICFW_RUNTIME_EVENT_BUS_BYTE = 2,
} picfw_runtime_event_type_t;

typedef struct picfw_runtime_event {
  uint8_t type;
  uint8_t value;
} picfw_runtime_event_t;

typedef struct picfw_runtime_event_queue {
  picfw_runtime_event_t items[PICFW_RUNTIME_EVENT_QUEUE_CAP];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} picfw_runtime_event_queue_t;

typedef struct picfw_runtime_byte_queue {
  uint8_t items[PICFW_RUNTIME_HOST_TX_CAP];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} picfw_runtime_byte_queue_t;

typedef struct picfw_runtime {
  picfw_runtime_config_t config;

  /* --- Host communication --- */
  picfw_enh_parser_t enh_parser;
  picfw_runtime_event_queue_t event_queue;
  picfw_runtime_byte_queue_t host_tx_queue;

  /* --- Runtime state --- */
  picfw_startup_state_t startup_state;
  uint8_t protocol_state;
  uint8_t protocol_state_flags;
  uint32_t now_ms;
  uint32_t host_parser_deadline_ms;
  picfw_bool_t host_parser_active;
  picfw_bool_t arbitration_active;
  uint8_t arbitration_initiator;

  /* --- Scan engine --- */
  uint32_t protocol_tick_ms;
  uint32_t protocol_deadline_ms;
  uint32_t scan_window_delay_ms;
  uint32_t scan_window_limit_ms;
  uint32_t scan_seed;
  uint32_t saved_scan_seed;
  uint32_t merged_window_ms;
  uint32_t scan_mask_seed;
  uint32_t saved_scan_deadline_ms;
  uint32_t scan_window_delta_ms;
  uint32_t scan_pass_deadline_ms;
  uint32_t scan_probe_deadline_ms;
  uint32_t scan_retry_deadline_ms;
  uint8_t active_scan_slot;
  /* current_scan_slot: working copy of the active scan slot, may be
   * modified during scan pass iteration.
   * current_scan_slot_id: the slot ID for descriptor address computation,
   * set during initialize_scan_slot_full. In the current implementation
   * these are always identical, but the original firmware uses them
   * in different contexts (slot iteration vs address calculation). */
  uint8_t current_scan_slot;
  uint8_t current_scan_slot_id;
  uint16_t descriptor_cursor;
  uint8_t scan_phase;
  uint8_t scan_dispatch_cursor;
  uint8_t scan_protocol_code;
  /* Cached low byte of scan_seed, set at every scan state mutation point.
   * Used in status frame construction (byte 17 = seed_latch + 0x06).
   * Could be computed on demand, but caching ensures the status frame
   * captures the seed value at the exact moment of the last scan mutation,
   * not at the moment of frame construction. */
  uint8_t status_seed_latch;
  uint8_t status_variant_selector;
  uint32_t status_snapshot_deadline_ms;
  uint32_t status_variant_deadline_ms;
  uint32_t status_tick_count;
  uint32_t status_snapshot_count;
  uint32_t status_variant_count;

  /* --- Diagnostics --- */
  uint8_t last_error;
  uint32_t dropped_events;
  uint32_t dropped_tx_bytes;
  uint32_t validation_corrections;  /* post_merge_validate clamping counter */

  /* --- Descriptor engine --- */
  uint8_t descriptor_data[PICFW_RUNTIME_DESCRIPTOR_DATA_CAP];
  uint8_t descriptor_data_len;
  uint8_t descriptor_data_pos;
  /* Scan accumulator registers. These are persistent struct fields rather than
     local variables because the PIC16F15356 has a 16-level hardware call stack.
     Passing them as function arguments would deepen the call chain. Instead,
     the init functions write these, and recompute_scan_masks_tail reads them. */
  uint32_t scan_accum_r24;
  uint32_t scan_accum_r8;
  uint32_t scan_accum_l8;
  uint32_t scan_accum_l24;
  uint8_t scan_slot_sub_phase;
} picfw_runtime_t;

void picfw_runtime_config_init_default(picfw_runtime_config_t *config);
void picfw_runtime_init(picfw_runtime_t *runtime, const picfw_runtime_config_t *config);
picfw_bool_t picfw_runtime_isr_enqueue_host_byte(picfw_runtime_t *runtime, uint8_t byte);
picfw_bool_t picfw_runtime_isr_enqueue_bus_byte(picfw_runtime_t *runtime, uint8_t byte);
uint32_t picfw_runtime_normalize_scan_delay(uint32_t requested_delay_ms);
uint32_t picfw_runtime_scan_deadline_after(uint32_t now_ms, uint32_t requested_delay_ms);
picfw_bool_t picfw_runtime_dispatch_scan_code(picfw_runtime_t *runtime, uint8_t code);
picfw_bool_t picfw_runtime_compute_next_scan_cursor(picfw_runtime_t *runtime, uint8_t requested_slot, uint8_t *return_code);
picfw_bool_t picfw_runtime_protocol_state_dispatch(picfw_runtime_t *runtime, uint8_t protocol_code, uint8_t *return_code);
void picfw_runtime_start_scan_window(picfw_runtime_t *runtime);
void picfw_runtime_continue_scan_window(picfw_runtime_t *runtime);
void picfw_runtime_run_scan_fsm(picfw_runtime_t *runtime);
void picfw_runtime_continue_scan_fsm(picfw_runtime_t *runtime, uint8_t reason_code);
size_t picfw_runtime_build_status_snapshot_frame(const picfw_runtime_t *runtime, uint8_t *out, size_t out_cap);
size_t picfw_runtime_build_status_variant_frame(const picfw_runtime_t *runtime, uint8_t *out, size_t out_cap);
void picfw_runtime_step(picfw_runtime_t *runtime, uint32_t now_ms);
size_t picfw_runtime_drain_host_tx(picfw_runtime_t *runtime, uint8_t *out, size_t out_cap);
void picfw_runtime_descriptor_merge_with_seed(picfw_runtime_t *runtime,
                                               uint32_t descriptor_data,
                                               uint32_t descriptor_mask);
void picfw_runtime_post_merge_validate(picfw_runtime_t *runtime);
uint32_t picfw_runtime_descriptor_read_u32(picfw_runtime_t *runtime);
picfw_bool_t picfw_runtime_load_descriptor_block(picfw_runtime_t *runtime);
picfw_bool_t picfw_runtime_read_indexed_descriptor(picfw_runtime_t *runtime,
                                                    uint8_t index);
picfw_bool_t picfw_runtime_shift_scan_masks_by_delta(picfw_runtime_t *runtime,
                                                      uint8_t delta);
void picfw_runtime_shift_saved_scan_masks(picfw_runtime_t *runtime,
                                           uint8_t shift_count);
void picfw_runtime_merge_shifted_scan_masks(picfw_runtime_t *runtime);
void picfw_runtime_merge_pending_scan_masks(picfw_runtime_t *runtime);
void picfw_runtime_recompute_scan_masks_tail(picfw_runtime_t *runtime);
void picfw_runtime_initialize_scan_slot_full(picfw_runtime_t *runtime,
                                              uint8_t slot_id);
void picfw_runtime_mask_tail_seed_and_recompute(picfw_runtime_t *runtime);
void picfw_runtime_init_seed_accumulators_and_recompute(
    picfw_runtime_t *runtime);
void picfw_runtime_init_tail_seed_accumulators_and_recompute(
    picfw_runtime_t *runtime, uint8_t param);
void picfw_runtime_load_tail_seed_and_recompute(picfw_runtime_t *runtime,
                                                 uint8_t param);
void picfw_runtime_recompute_scan_masks(picfw_runtime_t *runtime, uint8_t param);
picfw_bool_t picfw_runtime_probe_register_window(picfw_runtime_t *runtime);
void picfw_runtime_prime_scan_slot(picfw_runtime_t *runtime);
picfw_bool_t picfw_runtime_poll_scan_slot(picfw_runtime_t *runtime);
void picfw_runtime_run_scan_slot_sequence(picfw_runtime_t *runtime);
void picfw_runtime_retry_scan_slot_sequence(picfw_runtime_t *runtime);
void picfw_runtime_app_main_loop_init(picfw_runtime_t *runtime);
picfw_bool_t picfw_runtime_app_main_loop_step(picfw_runtime_t *runtime);

#endif
