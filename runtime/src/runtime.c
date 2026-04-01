#include "picfw/runtime.h"

_Static_assert(sizeof(picfw_runtime_t) <= 600u,
               "picfw_runtime_t must fit in PIC16F15356 RAM with headroom");

enum {
  PICFW_RUNTIME_EVENT_INVALID = 0u,
  PICFW_RUNTIME_ERROR_NONE = 0u,
  PICFW_RUNTIME_ERROR_UNSUPPORTED_COMMAND = 1u,
  PICFW_RUNTIME_ERROR_UNSUPPORTED_INFO = 2u,
  PICFW_RUNTIME_ERROR_QUEUE_OVERFLOW = 3u,
  PICFW_RUNTIME_ERROR_PARSE = 4u,
  PICFW_RUNTIME_ERROR_NO_ACTIVE_SESSION = 5u,
  PICFW_RUNTIME_ERROR_HOST_TIMEOUT = 6u,
  PICFW_RUNTIME_STATUS_KIND_SNAPSHOT = 0x35u,
  PICFW_RUNTIME_STATUS_KIND_VARIANT = 0x37u,
  PICFW_RUNTIME_DESCRIPTOR_BASE_0X01 = 0x0264u,
  PICFW_RUNTIME_DESCRIPTOR_BASE_0X03 = 0x0260u,
  PICFW_RUNTIME_DESCRIPTOR_BASE_0X36 = 0x0268u,
  PICFW_RUNTIME_SCAN_WINDOW_DELTA_DEFAULT = 0x0000002Eu,
  PICFW_RUNTIME_SCAN_PASS_DELAY_MS = 200u,
  PICFW_RUNTIME_SCAN_RETRY_DELAY_MS = 100u,
  PICFW_RUNTIME_SCAN_PROBE_DELAY_MS = 400u,
  PICFW_RUNTIME_WINDOW_LIMIT_MIN = 0x000000F0u,
  /* Number of variant codes cycled during scan window status emission.
   * Matches the size of picfw_runtime_variant_codes[] array below. */
  PICFW_RUNTIME_VARIANT_CODE_COUNT = 7u,
  PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT = 0x05u,
  PICFW_RUNTIME_PROTOCOL_CODE_SLOT_03 = 0x02u,
  PICFW_RUNTIME_PROTOCOL_CODE_SLOT_01 = 0x06u,
  PICFW_RUNTIME_FLAGS_IDLE = 0x00u,
  PICFW_RUNTIME_FLAGS_RETRY = 0x01u,
  PICFW_RUNTIME_FLAGS_SCAN = 0x03u,
  PICFW_RUNTIME_DESCRIPTOR_VALIDATE_XOR = 0x43u,
  PICFW_RUNTIME_MERGED_THRESHOLD_CHECK = 0x3Bu,
  PICFW_RUNTIME_ADDR_HI_BASE = 0x24u,
  PICFW_RUNTIME_MERGED_FLOOR = 7u,
  PICFW_RUNTIME_SCAN_MASK_SEED_DEFAULT = 0x00060101u,
};

static const uint8_t
    picfw_runtime_variant_codes[PICFW_RUNTIME_VARIANT_CODE_COUNT] = {
        0x01u, 0x33u, 0x35u, 0x36u, 0x3Au, 0x3Bu, 0x03u,
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(picfw_runtime_variant_codes) /
                       sizeof(picfw_runtime_variant_codes[0]) ==
                   PICFW_RUNTIME_VARIANT_CODE_COUNT,
               "variant_codes array size must match VARIANT_CODE_COUNT");
#endif

static void picfw_runtime_event_queue_init(picfw_runtime_event_queue_t *queue) {
  queue->head = 0u;
  queue->tail = 0u;
  queue->count = 0u;
}

static void picfw_runtime_byte_queue_init(picfw_runtime_byte_queue_t *queue) {
  queue->head = 0u;
  queue->tail = 0u;
  queue->count = 0u;
}

static picfw_bool_t
picfw_runtime_event_queue_push(picfw_runtime_event_queue_t *queue, uint8_t type,
                               uint8_t value) {
  if (queue->count >= PICFW_RUNTIME_EVENT_QUEUE_CAP) {
    return PICFW_FALSE;
  }
  queue->items[queue->tail].type = type;
  queue->items[queue->tail].value = value;
  queue->tail = (uint8_t)((queue->tail + 1u) & (PICFW_RUNTIME_EVENT_QUEUE_CAP - 1u));
  queue->count++;
  return PICFW_TRUE;
}

static picfw_bool_t
picfw_runtime_event_queue_pop(picfw_runtime_event_queue_t *queue,
                              picfw_runtime_event_t *event) {
  if (queue->count == 0u) {
    return PICFW_FALSE;
  }
  *event = queue->items[queue->head];
  queue->head = (uint8_t)((queue->head + 1u) & (PICFW_RUNTIME_EVENT_QUEUE_CAP - 1u));
  queue->count--;
  return PICFW_TRUE;
}

static picfw_bool_t
picfw_runtime_byte_queue_push(picfw_runtime_byte_queue_t *queue,
                              uint8_t value) {
  if (queue->count >= PICFW_RUNTIME_HOST_TX_CAP) {
    return PICFW_FALSE;
  }
  queue->items[queue->tail] = value;
  queue->tail = (uint8_t)((queue->tail + 1u) & (PICFW_RUNTIME_HOST_TX_CAP - 1u));
  queue->count++;
  return PICFW_TRUE;
}

static picfw_bool_t
picfw_runtime_byte_queue_pop(picfw_runtime_byte_queue_t *queue,
                             uint8_t *value) {
  if (queue->count == 0u) {
    return PICFW_FALSE;
  }
  *value = queue->items[queue->head];
  queue->head = (uint8_t)((queue->head + 1u) & (PICFW_RUNTIME_HOST_TX_CAP - 1u));
  queue->count--;
  return PICFW_TRUE;
}

static picfw_bool_t is_valid_protocol_state(uint8_t state) {
  switch (state) {
  case PICFW_PROTOCOL_STATE_IDLE:
  case PICFW_PROTOCOL_STATE_PENDING:
  case PICFW_PROTOCOL_STATE_ARMED:
  case PICFW_PROTOCOL_STATE_READY:
  case PICFW_PROTOCOL_STATE_VARIANT:
  case PICFW_PROTOCOL_STATE_OFFSET_SCAN:
  case PICFW_PROTOCOL_STATE_SCAN:
  case PICFW_PROTOCOL_STATE_RETRY:
    return PICFW_TRUE;
  default:
    return PICFW_FALSE;
  }
}

static void picfw_runtime_set_protocol_state(picfw_runtime_t *runtime,
                                             uint8_t state, uint8_t flags) {
  if (runtime == 0) {
    return;
  }

  if (!is_valid_protocol_state(state)) {
    return; /* reject invalid state */
  }

  runtime->protocol_state = state;
  runtime->protocol_state_flags = flags;
}

static void picfw_runtime_set_protocol_state_pending(picfw_runtime_t *runtime) {
  picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_PENDING,
                                   PICFW_RUNTIME_FLAGS_IDLE);
}

static void picfw_runtime_set_protocol_state_ready(picfw_runtime_t *runtime) {
  picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_READY,
                                   PICFW_RUNTIME_FLAGS_IDLE);
}

static void picfw_runtime_set_protocol_state_scan(picfw_runtime_t *runtime,
                                                  uint8_t state) {
  picfw_runtime_set_protocol_state(runtime, state, PICFW_RUNTIME_FLAGS_SCAN);
}

static uint16_t picfw_runtime_scan_descriptor_base_for_slot(uint8_t slot) {
  switch (slot) {
  case 0x01u:
    return PICFW_RUNTIME_DESCRIPTOR_BASE_0X01;
  case 0x03u:
    return PICFW_RUNTIME_DESCRIPTOR_BASE_0X03;
  case 0x06u:
  default:
    return PICFW_RUNTIME_DESCRIPTOR_BASE_0X36;
  }
}

static uint8_t picfw_runtime_protocol_code_for_slot(uint8_t slot) {
  switch (slot) {
  case 0x01u:
    return PICFW_RUNTIME_PROTOCOL_CODE_SLOT_01;
  case 0x03u:
    return PICFW_RUNTIME_PROTOCOL_CODE_SLOT_03;
  case 0x06u:
  default:
    return PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
  }
}

static void picfw_runtime_save_scan_context(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->saved_scan_seed = runtime->scan_seed;
  runtime->saved_scan_deadline_ms = runtime->protocol_deadline_ms;
}

static void picfw_runtime_restore_scan_seed(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_seed = runtime->saved_scan_seed;
}

/* Intentional floor clamping: delays below SCAN_MIN_DELAY_MS (60ms) are
 * unsafe on the eBUS because the bus needs minimum idle time between
 * transmissions. Zero delay is the most common case (initial state). */
uint32_t picfw_runtime_normalize_scan_delay(uint32_t requested_delay_ms) {
  if (requested_delay_ms == 0u ||
      requested_delay_ms < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    return PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  return requested_delay_ms;
}

uint32_t picfw_runtime_scan_deadline_after(uint32_t now_ms,
                                           uint32_t requested_delay_ms) {
  return now_ms + picfw_runtime_normalize_scan_delay(requested_delay_ms);
}

static uint32_t
picfw_runtime_normalize_scan_limit(uint32_t requested_limit_ms) {
  if (requested_limit_ms < PICFW_RUNTIME_WINDOW_LIMIT_MIN) {
    return PICFW_RUNTIME_WINDOW_LIMIT_MIN;
  }
  return requested_limit_ms;
}

static uint32_t picfw_runtime_seed_window_transform(uint32_t seed) {
  uint32_t r8 = seed >> 8;
  uint32_t l8 = seed << 8;
  uint32_t l24 = seed << 24;
  return r8 | l8 | l24;
}

static uint8_t picfw_runtime_ascii_hex(uint8_t nibble) {
  nibble &= 0x0Fu;
  if (nibble < 10u) {
    return (uint8_t)('0' + nibble);
  }
  return (uint8_t)('a' + (nibble - 10u));
}

static void picfw_runtime_prepare_scan_deadline(picfw_runtime_t *runtime,
                                                uint32_t delay_ms) {
  if (runtime == 0) {
    return;
  }

  runtime->protocol_deadline_ms = runtime->now_ms + delay_ms;
  runtime->saved_scan_deadline_ms = runtime->protocol_deadline_ms;
}

static void
picfw_runtime_prepare_scan_probe_deadline(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_probe_deadline_ms =
      runtime->now_ms + PICFW_RUNTIME_SCAN_PROBE_DELAY_MS;
}

static void picfw_runtime_prepare_scan_pass_deadline(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_pass_deadline_ms =
      runtime->now_ms + PICFW_RUNTIME_SCAN_PASS_DELAY_MS;
}

static void
picfw_runtime_prepare_scan_retry_deadline(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_retry_deadline_ms =
      runtime->now_ms + PICFW_RUNTIME_SCAN_RETRY_DELAY_MS;
}

static void picfw_runtime_initialize_scan_slot(picfw_runtime_t *runtime,
                                               uint8_t slot) {
  if (runtime == 0) {
    return;
  }

  runtime->current_scan_slot = slot;
  runtime->current_scan_slot_id = slot;
  runtime->active_scan_slot = slot;
  runtime->descriptor_cursor =
      picfw_runtime_scan_descriptor_base_for_slot(slot);
  runtime->scan_window_delta_ms = PICFW_RUNTIME_SCAN_WINDOW_DELTA_DEFAULT;
}

static void picfw_runtime_merge_scan_masks_and_state(picfw_runtime_t *runtime) {
  uint32_t transformed;
  uint32_t masked;
  uint32_t merged;

  if (runtime == 0) {
    return;
  }

  transformed = picfw_runtime_seed_window_transform(runtime->scan_seed);
  masked = transformed & runtime->scan_mask_seed;
  merged = runtime->merged_window_ms | masked;
  if (merged < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    merged = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  runtime->merged_window_ms = merged;
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
}

uint32_t picfw_runtime_descriptor_read_u32(picfw_runtime_t *runtime) {
  uint32_t value = 0u;
  uint8_t pos;
  uint8_t i;

  if (runtime == 0) {
    return 0u;
  }

  pos = runtime->descriptor_data_pos;
  if (pos >= PICFW_RUNTIME_DESCRIPTOR_DATA_CAP || pos + 4u > PICFW_RUNTIME_DESCRIPTOR_DATA_CAP) {
    return 0u;
  }
  for (i = 0u; i < 4u; ++i) {
    if (pos + i < runtime->descriptor_data_len) {
      value |= ((uint32_t)runtime->descriptor_data[pos + i]) << (i * 8u);
    }
  }
  if (pos + 4u <= runtime->descriptor_data_len) {
    runtime->descriptor_data_pos = (uint8_t)(pos + 4u);
  } else {
    /* Partial read: advance to end, flag error */
    runtime->descriptor_data_pos = runtime->descriptor_data_len;
    runtime->last_error = PICFW_RUNTIME_ERROR_PARSE;
  }
  return value;
}

void picfw_runtime_descriptor_merge_with_seed(picfw_runtime_t *runtime,
                                               uint32_t descriptor_data,
                                               uint32_t descriptor_mask) {
  uint32_t seed;
  uint8_t xor_key;
  uint32_t shifted;
  uint32_t acc_r8;
  uint32_t acc_l8;
  uint32_t acc_l24;
  uint32_t acc;
  uint32_t merged;

  if (runtime == 0) {
    return;
  }

  seed = runtime->scan_mask_seed;
  xor_key = (uint8_t)(seed & 0xFFu) ^ PICFW_RUNTIME_DESCRIPTOR_XOR_KEY;

  if (xor_key == 0u) {
    picfw_runtime_merge_scan_masks_and_state(runtime);
    return;
  }

  {
    uint8_t shift_amt = (uint8_t)(xor_key - 1u);
    shifted = (shift_amt >= 32u) ? 0u : (descriptor_mask >> shift_amt);
  }
  descriptor_data &= shifted;

  acc_r8 = (seed >> 8) & descriptor_data;
  acc_l8 = (seed << 8) & descriptor_data;
  acc_l24 = (seed << 24) & descriptor_data;
  acc = acc_r8 | acc_l8 | acc_l24;

  merged = runtime->merged_window_ms | acc;
  if (merged < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    merged = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  runtime->merged_window_ms = merged;
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
}

void picfw_runtime_post_merge_validate(picfw_runtime_t *runtime) {
  uint32_t limit;
  uint32_t delay;
  uint32_t merged;
  uint32_t recomputed;

  if (runtime == 0) {
    return;
  }

  if (runtime->protocol_state != PICFW_PROTOCOL_STATE_READY) {
    return;
  }

  limit = runtime->scan_window_limit_ms;
  if (limit < PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR) {
    limit = PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR;
    runtime->scan_window_limit_ms = limit;
    runtime->validation_corrections++;
  }

  delay = runtime->scan_window_delay_ms;
  if (delay <= PICFW_RUNTIME_SCAN_DELAY_THRESHOLD || delay >= limit) {
    recomputed = (limit >> 3) << 2;
    runtime->scan_window_delay_ms = recomputed;
    runtime->validation_corrections++;
  }

  merged = runtime->merged_window_ms;
  if (merged <= PICFW_RUNTIME_SCAN_MERGED_THRESHOLD || merged >= limit) {
    recomputed = limit >> 3;
    if (recomputed < PICFW_RUNTIME_MERGED_FLOOR) {
      recomputed = PICFW_RUNTIME_MERGED_FLOOR;
    }
    runtime->merged_window_ms = recomputed;
    runtime->validation_corrections++;
  }
}

picfw_bool_t picfw_runtime_load_descriptor_block(picfw_runtime_t *runtime) {
  uint32_t desc_data;
  uint32_t desc_mask;

  if (runtime == 0) {
    return PICFW_FALSE;
  }

  if (runtime->descriptor_data_len < 8u) {
    return PICFW_FALSE;
  }

  desc_data = picfw_runtime_descriptor_read_u32(runtime);
  desc_mask = picfw_runtime_descriptor_read_u32(runtime);

  if (runtime->last_error != PICFW_RUNTIME_ERROR_NONE) {
    return PICFW_FALSE;
  }

  picfw_runtime_descriptor_merge_with_seed(runtime, desc_data, desc_mask);
  picfw_runtime_post_merge_validate(runtime);
  return PICFW_TRUE;
}

picfw_bool_t picfw_runtime_read_indexed_descriptor(picfw_runtime_t *runtime,
                                                    uint8_t index) {
  uint32_t header;
  uint32_t desc_data;
  uint32_t four_pass;
  uint8_t hi;
  uint8_t lo;

  if (runtime == 0) {
    return PICFW_FALSE;
  }

  (void)index;

  if (!picfw_runtime_load_descriptor_block(runtime)) {
    return PICFW_FALSE;
  }

  if (runtime->descriptor_data_pos + 8u > runtime->descriptor_data_len) {
    return PICFW_FALSE;
  }

  header = picfw_runtime_descriptor_read_u32(runtime);
  hi = (uint8_t)((header >> 8) & 0xFFu);
  lo = (uint8_t)(header & 0xFFu);

  if (hi == 0u && lo < 8u) {
    return PICFW_FALSE;
  }

  if (((hi ^ PICFW_RUNTIME_DESCRIPTOR_VALIDATE_XOR) | lo) != 0u) {
    return PICFW_FALSE;
  }

  if ((uint8_t)(runtime->scan_mask_seed & 0xFFu) !=
      PICFW_RUNTIME_DESCRIPTOR_VALIDATE_SEED) {
    return PICFW_FALSE;
  }

  desc_data = picfw_runtime_descriptor_read_u32(runtime);

  four_pass = (desc_data >> 24) | (desc_data >> 8) |
              (desc_data << 8) | (desc_data << 24);

  picfw_runtime_descriptor_merge_with_seed(runtime, four_pass, four_pass);
  picfw_runtime_post_merge_validate(runtime);

  return PICFW_TRUE;
}

picfw_bool_t picfw_runtime_shift_scan_masks_by_delta(picfw_runtime_t *runtime,
                                                      uint8_t delta) {
  uint8_t xor_result;
  uint32_t desc_data;
  uint32_t desc_mask;

  if (runtime == 0) {
    return PICFW_FALSE;
  }

  xor_result = delta ^ (uint8_t)(runtime->scan_mask_seed & 0xFFu);
  if (xor_result == 0u) {
    picfw_runtime_merge_scan_masks_and_state(runtime);
    picfw_runtime_post_merge_validate(runtime);
    return PICFW_TRUE;
  }

  if (runtime->descriptor_data_len < 8u) {
    return PICFW_FALSE;
  }

  desc_mask = picfw_runtime_descriptor_read_u32(runtime);
  desc_data = picfw_runtime_descriptor_read_u32(runtime);

  {
    uint8_t shift_amt = (uint8_t)(xor_result - 1u);
    desc_mask = (shift_amt >= 32u) ? 0u : (desc_mask >> shift_amt);
  }
  desc_data &= desc_mask;

  picfw_runtime_descriptor_merge_with_seed(runtime, desc_data, desc_mask);
  picfw_runtime_post_merge_validate(runtime);
  return PICFW_TRUE;
}

void picfw_runtime_shift_saved_scan_masks(picfw_runtime_t *runtime,
                                           uint8_t shift_count) {
  uint32_t shifted;

  if (runtime == 0) return;
  if (shift_count == 0u) {
    /* shift_count == 0 is a no-op for the shift, but post_merge_validate
     * should still run to enforce window limit constraints. */
    picfw_runtime_post_merge_validate(runtime);
    return;
  }

  shifted = (shift_count >= 32u) ? 0u : (runtime->saved_scan_seed >> shift_count);
  runtime->merged_window_ms |= shifted;
  if (runtime->merged_window_ms < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    runtime->merged_window_ms = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
  picfw_runtime_post_merge_validate(runtime);
}

void picfw_runtime_merge_shifted_scan_masks(picfw_runtime_t *runtime) {
  uint32_t scan_data;
  uint32_t shifted_l24;
  uint32_t acc;

  if (runtime == 0) {
    return;
  }

  scan_data = runtime->scan_mask_seed;
  shifted_l24 = scan_data << 24;
  acc = shifted_l24 & runtime->merged_window_ms;
  runtime->merged_window_ms |= acc;

  if (runtime->merged_window_ms < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    runtime->merged_window_ms = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
  picfw_runtime_post_merge_validate(runtime);
}

void picfw_runtime_merge_pending_scan_masks(picfw_runtime_t *runtime) {
  uint32_t seed;
  uint32_t merged;

  if (runtime == 0) {
    return;
  }

  seed = runtime->scan_mask_seed;
  merged = runtime->merged_window_ms;
  merged |= (seed << 8) & merged;
  merged |= (seed << 24) & merged;

  if (merged < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    merged = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  runtime->merged_window_ms = merged;
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
  picfw_runtime_post_merge_validate(runtime);
}

/* --- Phase 7–10: Full scan slot, seed recomputation, slot ops, app loop --- */

static uint8_t picfw_runtime_slot_addr_hi(uint8_t slot_id) {
  /* Extract bits 7..3 of slot_id into bits 4..0 of result.
   * bit7 -> bit4, bit6 -> bit3, bit5 -> bit2, bit4 -> bit1, bit3 -> bit0. */
  uint8_t bit7 = (uint8_t)(((slot_id & 0x80u) != 0u) ? 1u : 0u);
  uint8_t bit6 = (uint8_t)(((slot_id & 0x40u) != 0u) ? 1u : 0u);
  uint8_t bit5 = (uint8_t)(((slot_id & 0x20u) != 0u) ? 1u : 0u);
  uint8_t bit4 = (uint8_t)(((slot_id & 0x10u) != 0u) ? 1u : 0u);
  uint8_t bit3 = (uint8_t)(((slot_id & 0x08u) != 0u) ? 1u : 0u);
  return (uint8_t)((bit7 << 4) | (bit6 << 3) | (bit5 << 2) | (bit4 << 1) | bit3);
}

void picfw_runtime_recompute_scan_masks_tail(picfw_runtime_t *runtime) {
  uint32_t accum;

  if (runtime == 0) {
    return;
  }

  accum = runtime->scan_accum_r24 | runtime->scan_accum_r8 |
          runtime->scan_accum_l8 | runtime->scan_accum_l24;
  runtime->merged_window_ms |= accum;
  if (runtime->merged_window_ms < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    runtime->merged_window_ms = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }

  if (runtime->descriptor_cursor <= (uint16_t)(0xFFFFu - 0x2Cu)) {
    runtime->descriptor_cursor = (uint16_t)(runtime->descriptor_cursor + 0x2Cu);
  }
  /* else: cursor saturated at max, scan FSM should detect via limit checks */
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
}

void picfw_runtime_initialize_scan_slot_full(picfw_runtime_t *runtime,
                                              uint8_t slot_id) {
  uint8_t addr_lo;
  uint8_t addr_hi;
  uint32_t seed;

  if (runtime == 0) {
    return;
  }

  addr_lo = (uint8_t)((slot_id * 0x20u) + 8u);
  addr_hi = (uint8_t)(picfw_runtime_slot_addr_hi(slot_id) +
                       (0xF7u < (uint8_t)(slot_id * 0x20u)));
  addr_hi |= PICFW_RUNTIME_ADDR_HI_BASE;
  runtime->descriptor_cursor = (uint16_t)((uint16_t)addr_hi << 8 | addr_lo);
  runtime->current_scan_slot_id = slot_id;

  seed = runtime->scan_seed;
  runtime->scan_accum_r24 = seed >> 24;
  runtime->scan_accum_r8 = seed >> 8;
  runtime->scan_accum_l8 = seed << 8;
  runtime->scan_accum_l24 = seed << 24;

  runtime->scan_mask_seed = PICFW_RUNTIME_SCAN_MASK_SEED_DEFAULT;

  picfw_runtime_recompute_scan_masks_tail(runtime);
}

void picfw_runtime_mask_tail_seed_and_recompute(picfw_runtime_t *runtime) {
  uint32_t saved;

  if (runtime == 0) {
    return;
  }

  saved = runtime->saved_scan_seed;
  runtime->scan_accum_l8 = saved << 8;
  runtime->scan_accum_l24 = saved << 24;
  runtime->scan_accum_r8 = 0u;
  runtime->scan_accum_r24 = 0u;

  picfw_runtime_recompute_scan_masks_tail(runtime);
}

void picfw_runtime_init_seed_accumulators_and_recompute(
    picfw_runtime_t *runtime) {
  uint32_t seed;

  if (runtime == 0) {
    return;
  }

  seed = runtime->scan_seed;
  runtime->scan_accum_r24 = seed >> 24;
  runtime->scan_accum_r8 = seed >> 8;
  runtime->scan_accum_l8 = seed << 8;
  runtime->scan_accum_l24 = seed << 24;

  picfw_runtime_recompute_scan_masks_tail(runtime);
}

void picfw_runtime_init_tail_seed_accumulators_and_recompute(
    picfw_runtime_t *runtime, uint8_t param) {
  uint32_t base;

  if (runtime == 0) {
    return;
  }

  base = (uint32_t)param;
  runtime->scan_accum_r24 = base;
  runtime->scan_accum_r8 = base << 8;
  runtime->scan_accum_l8 = base << 16;
  runtime->scan_accum_l24 = base << 24;

  picfw_runtime_recompute_scan_masks_tail(runtime);
}

void picfw_runtime_load_tail_seed_and_recompute(picfw_runtime_t *runtime,
                                                 uint8_t param) {
  uint32_t base;

  if (runtime == 0) {
    return;
  }

  base = (uint32_t)param;
  runtime->scan_accum_r24 = runtime->saved_scan_seed >> 24;
  runtime->scan_accum_r8 = base;
  runtime->scan_accum_l8 = runtime->saved_scan_seed << 8;
  runtime->scan_accum_l24 = base << 24;

  picfw_runtime_recompute_scan_masks_tail(runtime);
}

void picfw_runtime_recompute_scan_masks(picfw_runtime_t *runtime,
                                         uint8_t param) {
  uint32_t accum;

  if (runtime == 0) {
    return;
  }

  accum = runtime->scan_accum_r24 | runtime->scan_accum_r8 |
          runtime->scan_accum_l8 | runtime->scan_accum_l24;
  accum |= (uint32_t)param;
  runtime->merged_window_ms |= accum;
  if (runtime->merged_window_ms < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    runtime->merged_window_ms = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }

  if (runtime->descriptor_cursor <= (uint16_t)(0xFFFFu - 0x2Cu)) {
    runtime->descriptor_cursor = (uint16_t)(runtime->descriptor_cursor + 0x2Cu);
  }
  /* else: cursor saturated at max, scan FSM should detect via limit checks */
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
}

/* --- Slot-level scan operations (host-abstracted) --- */

/* SCAFFOLD LIMITATION: In the real PIC firmware, probe_register_window reads
 * register data from the eBUS via serial I/O and stores it in RAM. In this
 * host-buildable scaffold, descriptor data is pre-loaded into descriptor_data[]
 * by the test harness. The probe function only checks that data is available. */
picfw_bool_t picfw_runtime_probe_register_window(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }

  if (runtime->descriptor_data_len == 0u) {
    return PICFW_FALSE;
  }

  runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_PROBED;
  runtime->protocol_tick_ms = runtime->now_ms;
  return PICFW_TRUE;
}

void picfw_runtime_prime_scan_slot(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_PROBED;
  runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
  runtime->protocol_tick_ms = runtime->now_ms;
  picfw_runtime_set_protocol_state_scan(runtime, PICFW_PROTOCOL_STATE_SCAN);
}

picfw_bool_t picfw_runtime_poll_scan_slot(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }

  if (runtime->descriptor_data_pos >= runtime->descriptor_data_len) {
    runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_COMPLETE;
    return PICFW_TRUE;
  }

  if (runtime->scan_slot_sub_phase == PICFW_SCAN_SUB_PHASE_PROBED) {
    runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_POLLING;
  }

  return PICFW_FALSE;
}

void picfw_runtime_run_scan_slot_sequence(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  picfw_runtime_prime_scan_slot(runtime);

  if (picfw_runtime_poll_scan_slot(runtime)) {
    runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_COMPLETE;
    return;
  }

  if (runtime->descriptor_data_len >= 8u) {
    if (!picfw_runtime_load_descriptor_block(runtime)) {
      runtime->last_error = PICFW_RUNTIME_ERROR_PARSE;
    }
  }

  runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_COMPLETE;
}

void picfw_runtime_retry_scan_slot_sequence(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_RESET;
  runtime->descriptor_data_pos = 0u;
  picfw_runtime_run_scan_slot_sequence(runtime);
}

/* --- App main loop skeleton --- */

void picfw_runtime_app_main_loop_init(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  picfw_runtime_mask_tail_seed_and_recompute(runtime);
  picfw_runtime_init_seed_accumulators_and_recompute(runtime);
  picfw_runtime_load_tail_seed_and_recompute(runtime, 9u);
  picfw_runtime_load_tail_seed_and_recompute(runtime, 10u);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x14u);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x13u);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x1Fu);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x50u);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x30u);
  picfw_runtime_init_tail_seed_accumulators_and_recompute(runtime, 0x20u);
}

picfw_bool_t picfw_runtime_app_main_loop_step(picfw_runtime_t *runtime) {
  uint8_t slot;
  uint8_t addr_lo;
  uint8_t addr_hi;

  if (runtime == 0) {
    return PICFW_FALSE;
  }

  slot = runtime->current_scan_slot;
  addr_lo = (uint8_t)((slot * 0x20u) + 8u);
  addr_hi = picfw_runtime_slot_addr_hi(slot);

  runtime->protocol_tick_ms = runtime->now_ms;

  if (runtime->scan_slot_sub_phase >= PICFW_SCAN_SUB_PHASE_COMPLETE) {
    return PICFW_FALSE;
  }

  (void)addr_lo;
  (void)addr_hi;

  picfw_runtime_run_scan_slot_sequence(runtime);
  return PICFW_TRUE;
}

static void picfw_runtime_seed_scan_state(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->protocol_tick_ms = PICFW_RUNTIME_SCAN_DEFAULT_TICK;
  runtime->protocol_deadline_ms = PICFW_RUNTIME_SCAN_DEFAULT_DEADLINE;
  runtime->scan_window_delay_ms = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  runtime->scan_window_limit_ms = PICFW_RUNTIME_SCAN_DEFAULT_WINDOW_LIMIT;
  runtime->scan_seed = PICFW_RUNTIME_SCAN_DEFAULT_SEED;
  runtime->saved_scan_seed = PICFW_RUNTIME_SCAN_DEFAULT_SEED;
  runtime->merged_window_ms = PICFW_RUNTIME_SCAN_DEFAULT_MERGED_WINDOW;
  runtime->scan_mask_seed = PICFW_RUNTIME_SCAN_DEFAULT_SEED;
  runtime->saved_scan_deadline_ms = PICFW_RUNTIME_SCAN_DEFAULT_DEADLINE;
  runtime->scan_window_delta_ms = PICFW_RUNTIME_SCAN_WINDOW_DELTA_DEFAULT;
  runtime->scan_pass_deadline_ms = 0u;
  runtime->scan_probe_deadline_ms = 0u;
  runtime->scan_retry_deadline_ms = 0u;
  runtime->active_scan_slot = PICFW_RUNTIME_SCAN_DEFAULT_SLOT;
  runtime->current_scan_slot = PICFW_RUNTIME_SCAN_DEFAULT_SLOT;
  runtime->current_scan_slot_id = PICFW_RUNTIME_SCAN_DEFAULT_SLOT;
  runtime->descriptor_cursor = PICFW_RUNTIME_SCAN_DEFAULT_DESCRIPTOR_CURSOR;
  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_IDLE;
  runtime->scan_dispatch_cursor = 0u;
  runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
  runtime->status_variant_selector = 0u;
}

static void
picfw_runtime_update_scan_deadline_and_mark_ready(picfw_runtime_t *runtime,
                                                  uint32_t requested_delay_ms) {
  uint32_t effective_delay;

  if (runtime == 0) {
    return;
  }

  runtime->protocol_tick_ms = runtime->now_ms;
  effective_delay = picfw_runtime_normalize_scan_delay(requested_delay_ms);
  runtime->scan_window_delay_ms = effective_delay;
  if (effective_delay < PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    effective_delay = PICFW_RUNTIME_SCAN_MIN_DELAY_MS;
  }
  picfw_runtime_prepare_scan_deadline(runtime, effective_delay);
  picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_READY, PICFW_RUNTIME_FLAGS_SCAN);
}

static void picfw_runtime_run_scan_pass(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_PRIMED;
  runtime->scan_dispatch_cursor = 0u;
  runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
  runtime->protocol_tick_ms = runtime->now_ms;
  picfw_runtime_initialize_scan_slot_full(runtime, runtime->active_scan_slot);
  picfw_runtime_save_scan_context(runtime);
  picfw_runtime_merge_scan_masks_and_state(runtime);
  picfw_runtime_prepare_scan_pass_deadline(runtime);
  picfw_runtime_prepare_scan_probe_deadline(runtime);
  picfw_runtime_set_protocol_state_scan(runtime, PICFW_PROTOCOL_STATE_SCAN);
}

static picfw_bool_t picfw_runtime_finalize_scan_pass(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }

  if (runtime->scan_pass_deadline_ms != 0u &&
      !picfw_deadline_reached_u32(runtime->now_ms,
                                  runtime->scan_pass_deadline_ms)) {
    return PICFW_FALSE;
  }

  runtime->scan_pass_deadline_ms = 0u;
  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_PASS;
  runtime->current_scan_slot = runtime->active_scan_slot;
  runtime->current_scan_slot_id = runtime->active_scan_slot;
  return PICFW_TRUE;
}

static void picfw_runtime_enqueue_bytes(picfw_runtime_t *runtime,
                                        const uint8_t *bytes, size_t len) {
  size_t idx;

  if (runtime == 0 || bytes == 0) {
    return;
  }

  for (idx = 0u; idx < len && idx < PICFW_RUNTIME_HOST_TX_CAP; ++idx) {
    if (!picfw_runtime_byte_queue_push(&runtime->host_tx_queue, bytes[idx])) {
      runtime->last_error = PICFW_RUNTIME_ERROR_QUEUE_OVERFLOW;
      runtime->dropped_tx_bytes += (uint32_t)(len - idx);
      runtime->startup_state = PICFW_STARTUP_DEGRADED;
      break;
    }
  }
}

static void picfw_runtime_emit_frame(picfw_runtime_t *runtime, uint8_t command,
                                     uint8_t data) {
  uint8_t encoded[2];
  picfw_enh_frame_t frame;
  size_t encoded_len;

  frame.command = command;
  frame.data = data;
  encoded_len = picfw_enh_encode_frame(&frame, encoded, sizeof(encoded));
  picfw_runtime_enqueue_bytes(runtime, encoded, encoded_len);
}

static void picfw_runtime_emit_payload_stream(picfw_runtime_t *runtime,
                                              const uint8_t *payload,
                                              uint8_t payload_len) {
  uint8_t idx;
  uint8_t len = picfw_u8_min(payload_len, PICFW_RUNTIME_INFO_PAYLOAD_MAX);

  picfw_runtime_emit_frame(runtime, PICFW_ENH_RES_INFO, len);
  for (idx = 0u; idx < len && idx < PICFW_RUNTIME_INFO_PAYLOAD_MAX; ++idx) {
    picfw_runtime_emit_frame(runtime, PICFW_ENH_RES_INFO, payload[idx]);
  }
}

static void picfw_runtime_emit_received(picfw_runtime_t *runtime,
                                        uint8_t byte) {
  uint8_t encoded[2];
  size_t encoded_len =
      picfw_enh_encode_received(byte, encoded, sizeof(encoded));

  picfw_runtime_enqueue_bytes(runtime, encoded, encoded_len);
}

static void picfw_runtime_emit_error(picfw_runtime_t *runtime, uint8_t command,
                                     uint8_t code) {
  picfw_runtime_emit_frame(runtime, command, code);
}

static void picfw_runtime_arm_periodic_status(picfw_runtime_t *runtime,
                                              uint32_t now_ms) {
  if (runtime == 0) {
    return;
  }

  runtime->status_tick_count = 0u;
  runtime->status_snapshot_count = 0u;
  runtime->status_variant_count = 0u;
  runtime->status_variant_selector = 0u;
  if (!runtime->config.status_emit_enabled) {
    runtime->status_snapshot_deadline_ms = 0u;
    runtime->status_variant_deadline_ms = 0u;
    return;
  }

  if (runtime->config.status_snapshot_period_ms == 0u) {
    runtime->status_snapshot_deadline_ms = 0u;
  } else {
    runtime->status_snapshot_deadline_ms =
        now_ms + (uint32_t)runtime->config.status_snapshot_period_ms;
  }

  if (runtime->config.status_variant_period_ms == 0u) {
    runtime->status_variant_deadline_ms = 0u;
  } else {
    runtime->status_variant_deadline_ms =
        now_ms + (uint32_t)runtime->config.status_variant_period_ms;
  }
}

static void picfw_runtime_clear_arbitration(picfw_runtime_t *runtime) {
  if (runtime == 0) {
    return;
  }

  runtime->arbitration_active = PICFW_FALSE;
  runtime->arbitration_initiator = 0u;
}

static void picfw_runtime_begin_arbitration(picfw_runtime_t *runtime,
                                            uint8_t initiator) {
  if (runtime == 0) {
    return;
  }

  runtime->arbitration_active = PICFW_TRUE;
  runtime->arbitration_initiator = initiator;
}

static void picfw_runtime_emit_info_stream(picfw_runtime_t *runtime,
                                           uint8_t info_id) {
  if (runtime == 0) return;
  if (info_id < PICFW_RUNTIME_INFO_COUNT) {
    uint8_t len = runtime->config.info_payload_len[info_id];
    picfw_runtime_emit_payload_stream(
        runtime, runtime->config.info_payload[info_id], len);
  }
}

static size_t picfw_runtime_build_status_frame(const picfw_runtime_t *runtime,
                                               uint8_t kind, uint8_t *out,
                                               size_t out_cap) {
  uint8_t cached0;
  uint8_t cached1;
  uint8_t cached2;
  uint8_t cached3;
  uint8_t cached4;
  uint8_t cached5;

  if (runtime == 0 || out == 0 || out_cap < PICFW_RUNTIME_STATUS_FRAME_MAX) {
    return 0u;
  }

  cached0 = runtime->protocol_state_flags;
  cached1 = runtime->active_scan_slot;
  cached2 = (uint8_t)(runtime->descriptor_cursor & 0x00FFu);
  cached3 = (uint8_t)((runtime->descriptor_cursor >> 8) & 0x00FFu);
  cached4 = (uint8_t)(runtime->scan_window_delay_ms & 0x000000FFu);
  cached5 = (uint8_t)(runtime->scan_window_limit_ms & 0x000000FFu);

  out[0] = 0x63u;
  out[1] = 0x82u;
  out[2] = 0x53u;
  out[3] = 0x63u;
  out[4] = kind;
  out[5] = 0x01u;
  out[6] = runtime->protocol_state;
  out[7] = 0x3Du;
  out[8] = 0x07u;
  out[9] = 0x01u;
  out[10] = cached0;
  out[11] = cached1;
  out[12] = cached2;
  out[13] = cached3;
  out[14] = cached4;
  out[15] = cached5;
  out[16] = 0x0Cu;
  out[17] = (uint8_t)(runtime->status_seed_latch + 0x06u);
  out[18] = picfw_runtime_ascii_hex((uint8_t)(out[13] >> 4));
  out[19] = picfw_runtime_ascii_hex(out[13]);
  out[20] = picfw_runtime_ascii_hex((uint8_t)(out[14] >> 4));
  out[21] = picfw_runtime_ascii_hex(out[14]);
  out[22] = picfw_runtime_ascii_hex((uint8_t)(out[15] >> 4));
  out[23] = picfw_runtime_ascii_hex(out[15]);
  return PICFW_RUNTIME_STATUS_FRAME_MAX;
}

size_t picfw_runtime_build_status_snapshot_frame(const picfw_runtime_t *runtime,
                                                 uint8_t *out, size_t out_cap) {
  return picfw_runtime_build_status_frame(
      runtime, PICFW_RUNTIME_STATUS_KIND_SNAPSHOT, out, out_cap);
}

size_t picfw_runtime_build_status_variant_frame(const picfw_runtime_t *runtime,
                                                uint8_t *out, size_t out_cap) {
  return picfw_runtime_build_status_frame(
      runtime, PICFW_RUNTIME_STATUS_KIND_VARIANT, out, out_cap);
}

/* Helper: write return_code if the pointer is non-null. */
static void dispatch_set_return_code(uint8_t *return_code, uint8_t value) {
  if (return_code != 0) {
    *return_code = value;
  }
}

/* Dispatch sub-handler for flags == RETRY.
 * Returns 0 = fall through to common tail, 1 = early TRUE, -1 = early FALSE. */
static int dispatch_flags_retry(picfw_runtime_t *runtime,
                                uint8_t protocol_code,
                                uint8_t *return_code) {
  if (runtime->protocol_state == PICFW_PROTOCOL_STATE_PENDING) {
    if (protocol_code != PICFW_RUNTIME_PROTOCOL_CODE_SLOT_03) {
      dispatch_set_return_code(return_code,
          (uint8_t)(protocol_code ^ PICFW_RUNTIME_PROTOCOL_CODE_SLOT_03));
      return -1;
    }
    picfw_runtime_restore_scan_seed(runtime);
    picfw_runtime_set_protocol_state_ready(runtime);
    dispatch_set_return_code(return_code, 0u);
    return 1;
  }

  if (runtime->protocol_state == PICFW_PROTOCOL_STATE_READY) {
    return 0; /* fall through to common tail */
  }

  if (runtime->protocol_state != PICFW_PROTOCOL_STATE_RETRY) {
    dispatch_set_return_code(return_code,
        (uint8_t)(runtime->protocol_state ^ PICFW_PROTOCOL_STATE_RETRY));
    return -1;
  }
  if (protocol_code != PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT) {
    dispatch_set_return_code(return_code,
        (uint8_t)(protocol_code ^ PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT));
    return -1;
  }
  picfw_runtime_restore_scan_seed(runtime);
  picfw_runtime_prepare_scan_deadline(runtime, PICFW_RUNTIME_SCAN_MIN_DELAY_MS);
  dispatch_set_return_code(return_code, 0u);
  return 1;
}

/* Dispatch sub-handler for flags != RETRY (expects SCAN).
 * Returns 0 = fall through to common tail, -1 = early FALSE. */
static int dispatch_flags_scan(const picfw_runtime_t *runtime,
                               uint8_t *return_code) {
  if (runtime->protocol_state_flags != PICFW_RUNTIME_FLAGS_SCAN) {
    dispatch_set_return_code(return_code,
        (uint8_t)(runtime->protocol_state_flags ^ PICFW_RUNTIME_FLAGS_SCAN));
    return -1;
  }
  if (runtime->protocol_state != PICFW_PROTOCOL_STATE_READY) {
    dispatch_set_return_code(return_code,
        (uint8_t)(runtime->protocol_state ^ PICFW_PROTOCOL_STATE_READY));
    return -1;
  }
  return 0; /* fall through to common tail */
}

/* Common tail for protocol state dispatch: handles DEFAULT code and
 * SLOT_01 vs other protocol codes. */
static picfw_bool_t dispatch_common_tail(picfw_runtime_t *runtime,
                                         uint8_t protocol_code,
                                         uint8_t *return_code) {
  if (protocol_code == PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT) {
    picfw_runtime_restore_scan_seed(runtime);
    picfw_runtime_update_scan_deadline_and_mark_ready(
        runtime, runtime->scan_window_delay_ms);
    dispatch_set_return_code(return_code, 0x01u);
    return PICFW_TRUE;
  }

  if (protocol_code == PICFW_RUNTIME_PROTOCOL_CODE_SLOT_01) {
    picfw_runtime_set_protocol_state_pending(runtime);
  } else {
    picfw_runtime_set_protocol_state_ready(runtime);
  }

  dispatch_set_return_code(return_code, 0u);
  return PICFW_TRUE;
}

picfw_bool_t picfw_runtime_protocol_state_dispatch(picfw_runtime_t *runtime,
                                                   uint8_t protocol_code,
                                                   uint8_t *return_code) {
  int result;

  if (runtime == 0) {
    dispatch_set_return_code(return_code, 0xFFu);
    return PICFW_FALSE;
  }

  if (runtime->protocol_state_flags == PICFW_RUNTIME_FLAGS_RETRY) {
    result = dispatch_flags_retry(runtime, protocol_code, return_code);
  } else {
    result = dispatch_flags_scan(runtime, return_code);
  }

  if (result < 0) {
    return PICFW_FALSE;
  }
  if (result > 0) {
    return PICFW_TRUE;
  }

  return dispatch_common_tail(runtime, protocol_code, return_code);
}

picfw_bool_t picfw_runtime_compute_next_scan_cursor(picfw_runtime_t *runtime,
                                                    uint8_t requested_slot,
                                                    uint8_t *return_code) {
  uint8_t protocol_code;

  if (runtime == 0) {
    if (return_code != 0) {
      *return_code = 0xFFu;
    }
    return PICFW_FALSE;
  }

  picfw_runtime_initialize_scan_slot(runtime, requested_slot);
  runtime->protocol_tick_ms = runtime->now_ms;
  picfw_runtime_save_scan_context(runtime);
  protocol_code = picfw_runtime_protocol_code_for_slot(requested_slot);
  runtime->scan_protocol_code = protocol_code;
  return picfw_runtime_protocol_state_dispatch(runtime, protocol_code,
                                               return_code);
}

void picfw_runtime_run_scan_fsm(picfw_runtime_t *runtime) {
  PICFW_ASSERT(runtime != 0);
  if (runtime == 0) {
    return;
  }

  runtime->scan_retry_deadline_ms = 0u;
  runtime->scan_pass_deadline_ms = 0u;
  /* Guard: only call merge_shifted during active scan cycles with descriptor
   * data. Without this guard, merge_shifted_scan_masks calls post_merge_validate
   * which modifies scan_window_delay_ms during initial status emission, before
   * any descriptor data exists. The original firmware calls this unconditionally
   * because its register state is pre-initialized by the scan hardware. */
  if (runtime->descriptor_data_len > 0u) {
    picfw_runtime_merge_shifted_scan_masks(runtime);
  }
  picfw_runtime_run_scan_pass(runtime);
}

/* --- continue_scan_fsm phase handlers --- */

static void continue_fsm_phase_retry(picfw_runtime_t *runtime) {
  uint8_t return_code = 0u;

  if (runtime->scan_retry_deadline_ms != 0u &&
      !picfw_deadline_reached_u32(runtime->now_ms,
                                  runtime->scan_retry_deadline_ms)) {
    return;
  }

  runtime->scan_retry_deadline_ms = 0u;
  (void)picfw_runtime_protocol_state_dispatch(
      runtime, runtime->scan_protocol_code, &return_code);
  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_IDLE;
  runtime->scan_dispatch_cursor = 0u;
}

/* Returns true if the caller should return early (not proceed to variant dispatch). */
static picfw_bool_t continue_fsm_process_pass_descriptors(picfw_runtime_t *runtime) {
  /* PRIMED: attempt finalize; if not ready, return early. */
  if (runtime->scan_phase == PICFW_RUNTIME_SCAN_PHASE_PRIMED &&
      !picfw_runtime_finalize_scan_pass(runtime)) {
    return PICFW_TRUE;
  }

  /* PASS (or PRIMED that just transitioned to PASS): process descriptors. */
  if (runtime->scan_phase == PICFW_RUNTIME_SCAN_PHASE_PASS &&
      runtime->descriptor_data_len >= 8u) {
    if (picfw_runtime_shift_scan_masks_by_delta(runtime, 1u)) {
      picfw_runtime_load_descriptor_block(runtime);
    } else {
      picfw_runtime_merge_pending_scan_masks(runtime);
      runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_RETRY;
      picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_RETRY,
                                       PICFW_RUNTIME_FLAGS_RETRY);
      picfw_runtime_prepare_scan_retry_deadline(runtime);
      return PICFW_TRUE;
    }
  }

  return PICFW_FALSE;
}

static void continue_fsm_variant_dispatch(picfw_runtime_t *runtime) {
  uint8_t code;

  if (runtime->scan_probe_deadline_ms != 0u &&
      !picfw_deadline_reached_u32(runtime->now_ms,
                                  runtime->scan_probe_deadline_ms)) {
    return;
  }

  code = picfw_runtime_variant_codes[runtime->scan_dispatch_cursor %
                                     PICFW_RUNTIME_VARIANT_CODE_COUNT];
  runtime->scan_probe_deadline_ms = 0u;
  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_PASS;
  runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;

  if (!picfw_runtime_dispatch_scan_code(runtime, code)) {
    runtime->last_error = PICFW_RUNTIME_ERROR_UNSUPPORTED_COMMAND;
    runtime->startup_state = PICFW_STARTUP_DEGRADED;
    runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_IDLE;
    runtime->scan_dispatch_cursor = 0u;
    return;
  }

  runtime->current_scan_slot = runtime->active_scan_slot;
  runtime->current_scan_slot_id = runtime->active_scan_slot;
  picfw_runtime_initialize_scan_slot_full(runtime, runtime->active_scan_slot);
  picfw_runtime_save_scan_context(runtime);
  runtime->scan_dispatch_cursor =
      (uint8_t)((runtime->scan_dispatch_cursor + 1u) %
                PICFW_RUNTIME_VARIANT_CODE_COUNT);

  if (runtime->scan_dispatch_cursor == 0u) {
    runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_RETRY;
    picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_RETRY,
                                     PICFW_RUNTIME_FLAGS_RETRY);
    picfw_runtime_prepare_scan_retry_deadline(runtime);
    return;
  }

  picfw_runtime_prepare_scan_probe_deadline(runtime);
  picfw_runtime_set_protocol_state_scan(runtime, PICFW_PROTOCOL_STATE_VARIANT);
}

/* DIVERGENCE FROM ORIGINAL: The decompiled continue_scan_fsm uses recursive
 * tail-calls to advance the FSM to completion in a single invocation. This
 * scaffold advances one phase per call, driven by the periodic step() timer.
 * This means scan cycles complete over multiple step() calls rather than
 * in one burst. The observable behavior (status frames, protocol states)
 * is equivalent but the timing of intermediate states differs. */
void picfw_runtime_continue_scan_fsm(picfw_runtime_t *runtime,
                                     uint8_t reason_code) {
  PICFW_ASSERT(runtime != 0);
  if (runtime == 0) {
    return;
  }

  if (reason_code != 0u) {
    runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_RETRY;
    runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
    runtime->last_error = reason_code;
    picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_RETRY,
                                     PICFW_RUNTIME_FLAGS_RETRY);
    runtime->scan_pass_deadline_ms = 0u;
    picfw_runtime_prepare_scan_retry_deadline(runtime);
    return;
  }

  switch (runtime->scan_phase) {
  case PICFW_RUNTIME_SCAN_PHASE_IDLE:
    picfw_runtime_run_scan_fsm(runtime);
    return;

  case PICFW_RUNTIME_SCAN_PHASE_RETRY:
    continue_fsm_phase_retry(runtime);
    return;

  case PICFW_RUNTIME_SCAN_PHASE_PRIMED:
  case PICFW_RUNTIME_SCAN_PHASE_PASS:
    break;
  }

  if (continue_fsm_process_pass_descriptors(runtime)) {
    return;
  }

  continue_fsm_variant_dispatch(runtime);
}

void picfw_runtime_start_scan_window(picfw_runtime_t *runtime) {
  PICFW_ASSERT(runtime != 0);
  if (runtime == 0) {
    return;
  }

  runtime->scan_phase = PICFW_RUNTIME_SCAN_PHASE_IDLE;
  runtime->scan_protocol_code = PICFW_RUNTIME_PROTOCOL_CODE_DEFAULT;
  picfw_runtime_run_scan_fsm(runtime);
}

void picfw_runtime_continue_scan_window(picfw_runtime_t *runtime) {
  picfw_runtime_continue_scan_fsm(runtime, 0u);
}

/* ARCHITECTURE NOTE: Status emission divergence from original firmware.
 * The decompiled firmware calls service_scan_tick_and_emit_status() synchronously
 * at the end of run_scan_fsm/continue_scan_fsm. This scaffold uses a decoupled
 * periodic timer model (status_snapshot_deadline_ms / status_variant_deadline_ms)
 * driven by picfw_runtime_step(). This is intentional: the scaffold's step-based
 * architecture does not have an infinite main loop, so periodic emission via
 * deadlines is the correct abstraction. Wire-level status frame timing will
 * differ from the original firmware. */
static void picfw_runtime_emit_periodic_snapshot(picfw_runtime_t *runtime) {
  uint8_t frame[PICFW_RUNTIME_STATUS_FRAME_MAX];
  size_t frame_len;

  picfw_runtime_start_scan_window(runtime);
  runtime->protocol_tick_ms = runtime->now_ms;
  runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
  runtime->status_snapshot_count++;
  runtime->status_tick_count++;
  frame_len =
      picfw_runtime_build_status_snapshot_frame(runtime, frame, sizeof(frame));
  picfw_runtime_enqueue_bytes(runtime, frame, frame_len);
}

static void dispatch_compute_scan_params(uint32_t scan_seed,
                                         uint32_t *out_transformed,
                                         uint32_t *out_limit,
                                         uint32_t *out_delay) {
  uint32_t transformed = picfw_runtime_seed_window_transform(scan_seed);
  uint32_t limit = picfw_runtime_normalize_scan_limit(transformed);
  uint32_t delay = picfw_runtime_normalize_scan_delay(transformed);

  if (delay > limit) {
    delay = limit;
  }
  *out_transformed = transformed;
  *out_limit = limit;
  *out_delay = delay;
}

static uint32_t dispatch_clamp_merged_window(uint32_t transformed,
                                             uint32_t delay, uint32_t limit) {
  uint32_t merged = transformed;

  if (merged < delay) {
    merged = delay;
  }
  if (merged > limit) {
    merged = limit;
  }
  return merged;
}

picfw_bool_t picfw_runtime_dispatch_scan_code(picfw_runtime_t *runtime,
                                              uint8_t code) {
  uint32_t transformed;
  uint32_t limit;
  uint32_t delay;

  if (runtime == 0) {
    return PICFW_FALSE;
  }

  dispatch_compute_scan_params(runtime->scan_seed, &transformed, &limit, &delay);

  switch (code) {
  case 0x01u:
    runtime->descriptor_cursor = PICFW_RUNTIME_DESCRIPTOR_BASE_0X01;
    runtime->active_scan_slot = 0x01u;
    break;

  case 0x03u:
    runtime->descriptor_cursor = PICFW_RUNTIME_DESCRIPTOR_BASE_0X03;
    runtime->active_scan_slot = 0x03u;
    break;

  case 0x33u:
    runtime->scan_window_limit_ms = limit;
    break;

  case 0x35u:
    runtime->status_seed_latch = (uint8_t)(runtime->scan_seed & 0xFFu);
    break;

  case 0x36u:
    runtime->descriptor_cursor = PICFW_RUNTIME_DESCRIPTOR_BASE_0X36;
    runtime->active_scan_slot = 0x06u;
    break;

  case 0x3Au:
    runtime->scan_window_delay_ms = delay;
    runtime->protocol_deadline_ms = runtime->now_ms + delay;
    break;

  case 0x3Bu:
    runtime->merged_window_ms = dispatch_clamp_merged_window(
        transformed, delay, limit);
    break;

  default:
    return PICFW_FALSE;
  }

  runtime->protocol_tick_ms = runtime->now_ms;
  picfw_runtime_set_protocol_state(runtime, PICFW_PROTOCOL_STATE_READY, PICFW_RUNTIME_FLAGS_SCAN);
  return PICFW_TRUE;
}

static void picfw_runtime_emit_periodic_variant(picfw_runtime_t *runtime) {
  uint8_t frame[PICFW_RUNTIME_STATUS_FRAME_MAX];
  size_t frame_len;
  uint8_t cursor_before;

  cursor_before = runtime->scan_dispatch_cursor;
  picfw_runtime_continue_scan_window(runtime);
  if (runtime->scan_phase == PICFW_RUNTIME_SCAN_PHASE_IDLE &&
      cursor_before == runtime->scan_dispatch_cursor &&
      runtime->scan_retry_deadline_ms == 0u) {
    return;
  }

  runtime->status_variant_selector = runtime->scan_dispatch_cursor;
  runtime->status_variant_count++;
  runtime->status_tick_count++;
  frame_len =
      picfw_runtime_build_status_variant_frame(runtime, frame, sizeof(frame));
  picfw_runtime_enqueue_bytes(runtime, frame, frame_len);
}

static picfw_bool_t try_emit_snapshot(picfw_runtime_t *runtime) {
  uint32_t period;

  if (runtime->status_snapshot_deadline_ms == 0u) {
    return PICFW_FALSE;
  }
  if (!picfw_deadline_reached_u32(runtime->now_ms,
                                  runtime->status_snapshot_deadline_ms)) {
    return PICFW_FALSE;
  }

  period = (uint32_t)runtime->config.status_snapshot_period_ms;
  if (period != 0u) {
    runtime->status_snapshot_deadline_ms += period;
  }
  picfw_runtime_emit_periodic_snapshot(runtime);
  return PICFW_TRUE;
}

static picfw_bool_t try_emit_variant(picfw_runtime_t *runtime) {
  uint32_t period;

  if (runtime->status_variant_deadline_ms == 0u) {
    return PICFW_FALSE;
  }
  if (!picfw_deadline_reached_u32(runtime->now_ms,
                                  runtime->status_variant_deadline_ms)) {
    return PICFW_FALSE;
  }

  period = (uint32_t)runtime->config.status_variant_period_ms;
  if (period != 0u) {
    runtime->status_variant_deadline_ms += period;
  }
  picfw_runtime_emit_periodic_variant(runtime);
  return PICFW_TRUE;
}

static void picfw_runtime_service_periodic_status(picfw_runtime_t *runtime) {
  uint8_t emissions;

  if (runtime == 0 || !runtime->config.status_emit_enabled) {
    return;
  }
  if (runtime->startup_state != PICFW_STARTUP_LIVE_READY) {
    return;
  }
  if (runtime->host_parser_active) {
    return;
  }

  for (emissions = 0u; emissions < PICFW_RUNTIME_STATUS_EMISSION_BUDGET;
       ++emissions) {
    if (!try_emit_snapshot(runtime) && !try_emit_variant(runtime)) {
      break;
    }
  }
}

void picfw_runtime_config_init_default(picfw_runtime_config_t *config) {
  uint8_t idx;

  if (config == 0) {
    return;
  }

  config->init_features = 0x01u;
  config->start_should_fail = PICFW_FALSE;
  config->start_failure_winner = 0x7Fu;
  config->status_emit_enabled = PICFW_FALSE;
  config->status_snapshot_period_ms = 0u;
  config->status_variant_period_ms = 0u;
  config->status_snapshot_payload_len = 0u;
  config->status_variant_payload_len = 0u;
  for (idx = 0u; idx < PICFW_RUNTIME_INFO_COUNT; ++idx) {
    uint8_t *payload = config->info_payload[idx];
    config->info_payload_len[idx] = 2u;
    payload[0] = idx;
    payload[1] = 0x00u;
  }

  config->info_payload_len[PICFW_ADAPTER_INFO_VERSION] = 8u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][0] = 0x03u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][1] = 0x01u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][2] = 0x12u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][3] = 0x34u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][4] = 0x1Eu;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][5] = 0x02u;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][6] = 0xCAu;
  config->info_payload[PICFW_ADAPTER_INFO_VERSION][7] = 0xFEu;

  config->info_payload_len[PICFW_ADAPTER_INFO_RESET_INFO] = 2u;
  config->info_payload[PICFW_ADAPTER_INFO_RESET_INFO][0] = 0x05u;
  config->info_payload[PICFW_ADAPTER_INFO_RESET_INFO][1] = 0x01u;
}

void picfw_runtime_init(picfw_runtime_t *runtime,
                        const picfw_runtime_config_t *config) {
  PICFW_ASSERT(runtime != 0);
  if (runtime == 0) {
    return;
  }

  if (config == 0) {
    picfw_runtime_config_init_default(&runtime->config);
  } else {
    runtime->config = *config;
  }

  picfw_enh_parser_init(&runtime->enh_parser);
  picfw_runtime_event_queue_init(&runtime->event_queue);
  picfw_runtime_byte_queue_init(&runtime->host_tx_queue);
  runtime->startup_state = PICFW_STARTUP_BOOT_INIT;
  runtime->protocol_state = PICFW_PROTOCOL_STATE_IDLE;
  runtime->protocol_state_flags = PICFW_RUNTIME_FLAGS_IDLE;
  runtime->now_ms = 0u;
  runtime->host_parser_deadline_ms = 0u;
  runtime->host_parser_active = PICFW_FALSE;
  runtime->arbitration_active = PICFW_FALSE;
  runtime->arbitration_initiator = 0u;
  picfw_runtime_seed_scan_state(runtime);
  runtime->status_snapshot_deadline_ms = 0u;
  runtime->status_variant_deadline_ms = 0u;
  runtime->status_tick_count = 0u;
  runtime->status_snapshot_count = 0u;
  runtime->status_variant_count = 0u;
  runtime->last_error = PICFW_RUNTIME_ERROR_NONE;
  runtime->dropped_events = 0u;
  runtime->dropped_tx_bytes = 0u;
  runtime->validation_corrections = 0u;
  runtime->descriptor_data_len = 0u;
  runtime->descriptor_data_pos = 0u;
  runtime->scan_accum_r24 = 0u;
  runtime->scan_accum_r8 = 0u;
  runtime->scan_accum_l8 = 0u;
  runtime->scan_accum_l24 = 0u;
  runtime->scan_slot_sub_phase = PICFW_SCAN_SUB_PHASE_RESET;
}

picfw_bool_t picfw_runtime_isr_enqueue_host_byte(picfw_runtime_t *runtime,
                                                 uint8_t byte) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }
  if (!picfw_runtime_event_queue_push(&runtime->event_queue,
                                       PICFW_RUNTIME_EVENT_HOST_BYTE, byte)) {
    runtime->dropped_events++;
    return PICFW_FALSE;
  }
  return PICFW_TRUE;
}

picfw_bool_t picfw_runtime_isr_enqueue_bus_byte(picfw_runtime_t *runtime,
                                                uint8_t byte) {
  if (runtime == 0) {
    return PICFW_FALSE;
  }
  if (!picfw_runtime_event_queue_push(&runtime->event_queue,
                                       PICFW_RUNTIME_EVENT_BUS_BYTE, byte)) {
    runtime->dropped_events++;
    return PICFW_FALSE;
  }
  return PICFW_TRUE;
}

static void handle_host_cmd_send(picfw_runtime_t *runtime,
                                 const picfw_enh_frame_t *frame) {
  runtime->startup_state = PICFW_STARTUP_LIVE_READY;
  if (!runtime->arbitration_active) {
    runtime->last_error = PICFW_RUNTIME_ERROR_NO_ACTIVE_SESSION;
    picfw_runtime_emit_error(runtime, PICFW_ENH_RES_ERROR_HOST,
                             PICFW_RUNTIME_ERROR_NO_ACTIVE_SESSION);
    return;
  }
  picfw_runtime_emit_received(runtime, frame->data);
  if (frame->data == PICFW_RUNTIME_SYN_BYTE) {
    picfw_runtime_clear_arbitration(runtime);
  }
}

static void handle_host_cmd_start(picfw_runtime_t *runtime,
                                  const picfw_enh_frame_t *frame) {
  runtime->startup_state = PICFW_STARTUP_LIVE_READY;
  if (frame->data == PICFW_RUNTIME_SYN_BYTE) {
    picfw_runtime_clear_arbitration(runtime);
    return;
  }
  if (runtime->config.start_should_fail) {
    picfw_runtime_emit_frame(runtime, PICFW_ENH_RES_FAILED,
                             runtime->config.start_failure_winner);
    return;
  }
  picfw_runtime_clear_arbitration(runtime);
  picfw_runtime_begin_arbitration(runtime, frame->data);
  picfw_runtime_emit_frame(runtime, PICFW_ENH_RES_STARTED, frame->data);
}

static void picfw_runtime_handle_host_frame(picfw_runtime_t *runtime,
                                            const picfw_enh_frame_t *frame) {
  if (frame == 0) {
    return;
  }

  runtime->host_parser_active = PICFW_FALSE;
  runtime->host_parser_deadline_ms = 0u;
  picfw_runtime_set_protocol_state_pending(runtime);

  switch (frame->command) {
  case PICFW_ENH_REQ_INIT:
    runtime->startup_state = PICFW_STARTUP_LIVE_WARMUP;
    picfw_runtime_clear_arbitration(runtime);
    picfw_runtime_emit_frame(runtime, PICFW_ENH_RES_RESETTED,
                             runtime->config.init_features);
    runtime->startup_state = PICFW_STARTUP_LIVE_READY;
    picfw_runtime_seed_scan_state(runtime);
    picfw_runtime_arm_periodic_status(runtime, runtime->now_ms);
    break;

  case PICFW_ENH_REQ_INFO:
    runtime->startup_state = PICFW_STARTUP_LIVE_READY;
    if (frame->data < PICFW_RUNTIME_INFO_COUNT) {
      picfw_runtime_update_scan_deadline_and_mark_ready(runtime, 0u);
      picfw_runtime_emit_info_stream(runtime, frame->data);
    } else {
      runtime->last_error = PICFW_RUNTIME_ERROR_UNSUPPORTED_INFO;
      runtime->startup_state = PICFW_STARTUP_DEGRADED;
      picfw_runtime_emit_error(runtime, PICFW_ENH_RES_ERROR_HOST,
                               PICFW_RUNTIME_ERROR_UNSUPPORTED_INFO);
    }
    break;

  case PICFW_ENH_REQ_SEND:
    handle_host_cmd_send(runtime, frame);
    break;

  case PICFW_ENH_REQ_START:
    handle_host_cmd_start(runtime, frame);
    break;

  default:
    runtime->last_error = PICFW_RUNTIME_ERROR_UNSUPPORTED_COMMAND;
    runtime->startup_state = PICFW_STARTUP_DEGRADED;
    picfw_runtime_emit_error(runtime, PICFW_ENH_RES_ERROR_HOST,
                             PICFW_RUNTIME_ERROR_UNSUPPORTED_COMMAND);
    break;
  }

  picfw_runtime_set_protocol_state_ready(runtime);
}

/* ADAPTER ROLE BOUNDARY: This firmware is a transparent UART bridge between
 * the ESP host and the eBUS wire. Collision detection (sent-byte vs
 * received-byte comparison) is NOT implemented in the PIC adapter. The host
 * (Go gateway) is responsible for all eBUS protocol logic including
 * collision detection, CRC computation, frame escaping, and retransmission.
 * The adapter only provides: SYN detection, ENH/ENS host framing, and
 * byte-level forwarding.
 *
 * Lock counter (eBUS Part 12 S6.4) is also a gateway responsibility.
 * The PIC adapter does not track failed arbitration attempts. */
static void handle_bus_byte_event(picfw_runtime_t *runtime, uint8_t value) {
  if (!(runtime->arbitration_active &&
        value == runtime->arbitration_initiator)) {
    picfw_runtime_emit_received(runtime, value);
  }
  if (value == PICFW_RUNTIME_SYN_BYTE) {
    picfw_runtime_clear_arbitration(runtime);
  }
}

static void picfw_runtime_handle_event(picfw_runtime_t *runtime,
                                       const picfw_runtime_event_t *event) {
  picfw_enh_frame_t frame;
  picfw_enh_parse_result_t parse_result;

  if (runtime == 0 || event == 0) {
    return;
  }

  if (event->type == PICFW_RUNTIME_EVENT_BUS_BYTE) {
    handle_bus_byte_event(runtime, event->value);
    return;
  }

  runtime->host_parser_active = PICFW_TRUE;
  if (runtime->host_parser_deadline_ms == 0u) {
    runtime->host_parser_deadline_ms =
        runtime->now_ms + PICFW_RUNTIME_HOST_RX_TIMEOUT_MS;
  }

  parse_result =
      picfw_enh_parser_feed(&runtime->enh_parser, event->value, &frame);
  if (parse_result == PICFW_ENH_PARSE_ERROR) {
    runtime->last_error = PICFW_RUNTIME_ERROR_PARSE;
    runtime->startup_state = PICFW_STARTUP_DEGRADED;
    runtime->host_parser_active = PICFW_FALSE;
    runtime->host_parser_deadline_ms = 0u;
    picfw_enh_parser_init(&runtime->enh_parser);
    picfw_runtime_emit_error(runtime, PICFW_ENH_RES_ERROR_HOST,
                             PICFW_RUNTIME_ERROR_PARSE);
    return;
  }
  if (parse_result == PICFW_ENH_PARSE_COMPLETE) {
    if (frame.command == PICFW_ENH_RES_RECEIVED) {
      frame.command = PICFW_ENH_REQ_SEND; /* cppcheck-suppress duplicateConditionalAssign ; F06: semantic remap for direction clarity */
    }
    picfw_runtime_handle_host_frame(runtime, &frame);
    picfw_enh_parser_init(&runtime->enh_parser);
  }
}

void picfw_runtime_step(picfw_runtime_t *runtime, uint32_t now_ms) {
  picfw_runtime_event_t event;
  uint8_t processed = 0u;

  PICFW_ASSERT(runtime != 0);
  if (runtime == 0) {
    return;
  }

  runtime->now_ms = now_ms;
  if (runtime->host_parser_active && runtime->host_parser_deadline_ms != 0u &&
      picfw_deadline_reached_u32(now_ms, runtime->host_parser_deadline_ms)) {
    runtime->host_parser_active = PICFW_FALSE;
    runtime->host_parser_deadline_ms = 0u;
    picfw_enh_parser_init(&runtime->enh_parser);
    runtime->last_error = PICFW_RUNTIME_ERROR_HOST_TIMEOUT;
    runtime->startup_state = PICFW_STARTUP_DEGRADED;
    picfw_runtime_emit_error(runtime, PICFW_ENH_RES_ERROR_HOST,
                             PICFW_RUNTIME_ERROR_HOST_TIMEOUT);
  }

  picfw_runtime_service_periodic_status(runtime);

  while (processed < PICFW_RUNTIME_STEP_EVENT_BUDGET &&
         picfw_runtime_event_queue_pop(&runtime->event_queue, &event)) {
    picfw_runtime_handle_event(runtime, &event);
    processed++;
  }
}

/* out_cap == 0 is safe: returns 0 (no bytes drained). Callers should
 * check for this case if they need to distinguish "queue empty" from
 * "buffer full". */
size_t picfw_runtime_drain_host_tx(picfw_runtime_t *runtime, uint8_t *out,
                                   size_t out_cap) {
  size_t out_len = 0u;
  size_t budget;

  PICFW_ASSERT(runtime != 0);
  if (runtime == 0 || out == 0) {
    return 0u;
  }

  budget = out_cap;
  if (budget > PICFW_RUNTIME_DRAIN_BUDGET) {
    budget = PICFW_RUNTIME_DRAIN_BUDGET;
  }

  while (out_len < budget && runtime->host_tx_queue.count > 0u) {
    uint8_t value = 0u;
    if (!picfw_runtime_byte_queue_pop(&runtime->host_tx_queue, &value)) {
      break;
    }
    out[out_len++] = value;
  }

  return out_len;
}
