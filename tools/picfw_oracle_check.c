#include "picfw/codec_enh.h"
#include "picfw/codec_ens.h"
#include "picfw/info.h"
#include "picfw/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct picfw_runtime_session_snapshot {
  picfw_bool_t active;
  uint8_t initiator;
} picfw_runtime_session_snapshot_t;

typedef struct picfw_runtime_exchange_sample {
  uint8_t request_hex[2];
  size_t request_len;
  picfw_enh_frame_t request;
  const char *request_name;
  int request_consumed;
  uint8_t response_hex[2];
  size_t response_len;
  picfw_enh_frame_t response;
  int response_consumed;
  picfw_bool_t has_response;
  const char *outcome;
  picfw_runtime_session_snapshot_t state_before;
  picfw_runtime_session_snapshot_t state_after;
} picfw_runtime_exchange_sample_t;

typedef struct picfw_runtime_contract_report {
  picfw_runtime_exchange_sample_t init;
  picfw_runtime_exchange_sample_t start_success;
  picfw_runtime_exchange_sample_t send_after_start;
  picfw_runtime_exchange_sample_t send_cancel;
  picfw_runtime_exchange_sample_t send_without_start;
  picfw_runtime_exchange_sample_t start_cancel;
} picfw_runtime_contract_report_t;

typedef struct picfw_scan_state_snapshot {
  uint8_t state;
  uint8_t flags;
  uint32_t tick;
  uint32_t deadline;
  uint32_t window_delay;
  uint32_t window_limit;
  uint32_t scan_seed;
  uint32_t merged_window;
  uint8_t active_scan_slot;
  uint16_t descriptor_cursor;
} picfw_scan_state_snapshot_t;

typedef struct picfw_scan_deadline_example {
  uint32_t now;
  uint32_t requested_delay;
  uint32_t effective_delay;
  uint32_t deadline;
} picfw_scan_deadline_example_t;

typedef struct picfw_scan_dispatch_sample {
  const char *trigger;
  uint8_t input_code;
  uint8_t return_code;
  picfw_scan_state_snapshot_t before;
  picfw_scan_state_snapshot_t after;
  const char *notes;
} picfw_scan_dispatch_sample_t;

typedef struct picfw_status_frame_sample {
  const char *builder;
  uint8_t prefix_hex[6];
  size_t prefix_len;
  uint8_t summary_hex[PICFW_RUNTIME_STATUS_FRAME_MAX - 6u];
  size_t summary_len;
  size_t length;
  const char *notes;
} picfw_status_frame_sample_t;

typedef struct picfw_scan_report {
  picfw_scan_state_snapshot_t initial;
  picfw_scan_deadline_example_t deadline_examples[4];
  picfw_scan_dispatch_sample_t dispatch_samples[7];
  picfw_status_frame_sample_t status_snapshot;
  picfw_status_frame_sample_t status_variant;
} picfw_scan_report_t;

static const char *picfw_oracle_response_name(uint8_t command) {
  switch (command) {
  case PICFW_ENH_RES_RESETTED:
    return "resetted";
  case PICFW_ENH_RES_RECEIVED:
    return "received";
  case PICFW_ENH_RES_STARTED:
    return "started";
  case PICFW_ENH_RES_INFO:
    return "info";
  case PICFW_ENH_RES_FAILED:
    return "failed";
  case PICFW_ENH_RES_ERROR_EBUS:
    return "error_ebus";
  case PICFW_ENH_RES_ERROR_HOST:
    return "error_host";
  default:
    return "unknown";
  }
}

static const char *picfw_oracle_info_name(uint8_t info_id) {
  switch (info_id) {
  case PICFW_ADAPTER_INFO_VERSION:
    return "version";
  case PICFW_ADAPTER_INFO_HARDWARE_ID:
    return "hw_id";
  case PICFW_ADAPTER_INFO_HARDWARE_CONF:
    return "hw_config";
  case PICFW_ADAPTER_INFO_TEMPERATURE:
    return "temperature";
  case PICFW_ADAPTER_INFO_SUPPLY_VOLT:
    return "supply_voltage";
  case PICFW_ADAPTER_INFO_BUS_VOLT:
    return "bus_voltage";
  case PICFW_ADAPTER_INFO_RESET_INFO:
    return "reset_info";
  case PICFW_ADAPTER_INFO_WIFI_RSSI:
    return "wifi_rssi";
  default:
    return "unknown";
  }
}

static picfw_bool_t
picfw_oracle_supports_info_id(const picfw_adapter_version_t *version,
                              uint8_t info_id) {
  if (version == NULL) {
    return PICFW_FALSE;
  }
  if (!version->supports_info) {
    return info_id == PICFW_ADAPTER_INFO_VERSION ? PICFW_TRUE : PICFW_FALSE;
  }
  return info_id <= PICFW_ADAPTER_INFO_WIFI_RSSI ? PICFW_TRUE : PICFW_FALSE;
}

static const char *picfw_oracle_protocol_state_name(uint8_t state) {
  switch (state) {
  case PICFW_PROTOCOL_STATE_IDLE:
    return "idle";
  case PICFW_PROTOCOL_STATE_PENDING:
    return "pending";
  case PICFW_PROTOCOL_STATE_READY:
    return "ready";
  case PICFW_PROTOCOL_STATE_VARIANT:
    return "variant";
  case PICFW_PROTOCOL_STATE_SCAN:
    return "scan";
  case PICFW_PROTOCOL_STATE_RETRY:
    return "scan_holding";
  default:
    return "unknown";
  }
}

static const char *picfw_oracle_protocol_flags_name(uint8_t flags) {
  switch (flags) {
  case 0u:
    return "idle";
  case 1u:
    return "pending_transition";
  case 3u:
    return "ready_and_pending";
  default:
    return "unknown";
  }
}

static void picfw_oracle_print_indent(size_t count) {
  size_t idx;

  for (idx = 0u; idx < count; ++idx) {
    putchar(' ');
  }
}

static void picfw_oracle_print_hex_array(const uint8_t *data, size_t len,
                                         size_t element_indent) {
  size_t idx;

  printf("[\n");
  for (idx = 0u; idx < len; ++idx) {
    picfw_oracle_print_indent(element_indent);
    printf("\"%02x\"", data[idx]);
    if (idx + 1u < len) {
      printf(",");
    }
    printf("\n");
  }
  picfw_oracle_print_indent(element_indent >= 2u ? element_indent - 2u : 0u);
  printf("]");
}

static int picfw_oracle_decode_stream(const uint8_t *input, size_t input_len,
                                      picfw_enh_frame_t *frame_out,
                                      int *consumed_out) {
  picfw_enh_parser_t parser;
  size_t idx;

  if (input == NULL || frame_out == NULL || consumed_out == NULL) {
    return -1;
  }

  picfw_enh_parser_init(&parser);
  for (idx = 0u; idx < input_len; ++idx) {
    picfw_enh_parse_result_t result =
        picfw_enh_parser_feed(&parser, input[idx], frame_out);
    if (result == PICFW_ENH_PARSE_ERROR) {
      return -1;
    }
    if (result == PICFW_ENH_PARSE_COMPLETE) {
      *consumed_out = (int)(idx + 1u);
      return 0;
    }
  }

  return -1;
}

static picfw_runtime_session_snapshot_t
picfw_runtime_snapshot(const picfw_runtime_t *runtime) {
  picfw_runtime_session_snapshot_t snapshot;

  snapshot.active = PICFW_FALSE;
  snapshot.initiator = 0u;
  if (runtime != NULL) {
    snapshot.active = runtime->arbitration_active;
    snapshot.initiator = runtime->arbitration_initiator;
  }
  return snapshot;
}

static picfw_scan_state_snapshot_t picfw_oracle_make_scan_snapshot(
    uint8_t state, uint8_t flags, uint32_t tick, uint32_t deadline,
    uint32_t window_delay, uint32_t window_limit, uint32_t scan_seed,
    uint32_t merged_window, uint8_t active_scan_slot,
    uint16_t descriptor_cursor) {
  picfw_scan_state_snapshot_t snapshot;

  snapshot.state = state;
  snapshot.flags = flags;
  snapshot.tick = tick;
  snapshot.deadline = deadline;
  snapshot.window_delay = window_delay;
  snapshot.window_limit = window_limit;
  snapshot.scan_seed = scan_seed;
  snapshot.merged_window = merged_window;
  snapshot.active_scan_slot = active_scan_slot;
  snapshot.descriptor_cursor = descriptor_cursor;
  return snapshot;
}

static picfw_scan_deadline_example_t
picfw_oracle_deadline_example(uint32_t now, uint32_t requested_delay) {
  picfw_scan_deadline_example_t example;

  example.now = now;
  example.requested_delay = requested_delay;
  example.effective_delay = picfw_runtime_normalize_scan_delay(requested_delay);
  example.deadline = picfw_runtime_scan_deadline_after(now, requested_delay);
  return example;
}

static picfw_scan_state_snapshot_t
picfw_oracle_scan_snapshot_from_runtime(const picfw_runtime_t *runtime) {
  if (runtime == NULL) {
    return picfw_oracle_make_scan_snapshot(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                                           0u);
  }

  return picfw_oracle_make_scan_snapshot(
      runtime->protocol_state, runtime->protocol_state_flags,
      runtime->protocol_tick_ms, runtime->protocol_deadline_ms,
      runtime->scan_window_delay_ms, runtime->scan_window_limit_ms,
      runtime->scan_seed, runtime->merged_window_ms, runtime->active_scan_slot,
      runtime->descriptor_cursor);
}

static void picfw_oracle_build_status_frame(
    const char *builder, const picfw_runtime_t *runtime, const char *notes,
    size_t (*build_fn)(const picfw_runtime_t *, uint8_t *, size_t),
    picfw_status_frame_sample_t *sample) {
  uint8_t frame[PICFW_RUNTIME_STATUS_FRAME_MAX];
  size_t frame_len;

  if (sample == NULL || build_fn == NULL) {
    return;
  }

  memset(sample, 0, sizeof(*sample));
  frame_len = build_fn(runtime, frame, sizeof(frame));
  sample->builder = builder;
  sample->notes = notes;
  sample->prefix_len = 6u;
  memcpy(sample->prefix_hex, frame, sample->prefix_len);
  sample->summary_len = frame_len - sample->prefix_len;
  memcpy(sample->summary_hex, frame + sample->prefix_len, sample->summary_len);
  sample->length = frame_len;
}

static int picfw_oracle_build_scan_report(picfw_scan_report_t *report) {
  picfw_runtime_t runtime;
  picfw_runtime_t sample_runtime;
  static const uint8_t dispatch_codes[7] = {0x01u, 0x03u, 0x33u, 0x35u,
                                            0x36u, 0x3Au, 0x3Bu};
  static const char *dispatch_notes[7] = {
      "select descriptor window 0x0264",
      "select descriptor window 0x0260",
      "derive scan window limit from scan seed",
      "latch scan seed low byte into status path",
      "select descriptor window 0x0268",
      "derive scan window delay from scan seed",
      "merge scan window candidate from scan seed",
  };
  size_t idx;

  if (report == NULL) {
    return -1;
  }

  memset(report, 0, sizeof(*report));
  picfw_runtime_init(&runtime, NULL);
  runtime.protocol_state = PICFW_PROTOCOL_STATE_READY;
  runtime.protocol_state_flags = 0u;
  runtime.now_ms = 0x00000140u;

  report->initial = picfw_oracle_scan_snapshot_from_runtime(&runtime);
  report->deadline_examples[0] =
      picfw_oracle_deadline_example(0x00000140u, 0x00u);
  report->deadline_examples[1] =
      picfw_oracle_deadline_example(0x00000140u, 0x10u);
  report->deadline_examples[2] =
      picfw_oracle_deadline_example(0xFFFFFFF0u, 0x20u);
  report->deadline_examples[3] =
      picfw_oracle_deadline_example(0x00000140u, 0x80u);

  for (idx = 0u; idx < 7u; ++idx) {
    sample_runtime = runtime;
    report->dispatch_samples[idx].trigger = "command_id_dispatch";
    report->dispatch_samples[idx].input_code = dispatch_codes[idx];
    report->dispatch_samples[idx].before =
        picfw_oracle_scan_snapshot_from_runtime(&sample_runtime);
    report->dispatch_samples[idx].return_code =
        picfw_runtime_dispatch_scan_code(&sample_runtime, dispatch_codes[idx])
            ? 1u
            : 0u;
    report->dispatch_samples[idx].after =
        picfw_oracle_scan_snapshot_from_runtime(&sample_runtime);
    report->dispatch_samples[idx].notes = dispatch_notes[idx];
  }

  sample_runtime = runtime;
  picfw_runtime_start_scan_window(&sample_runtime);
  picfw_oracle_build_status_frame(
      "build_status_snapshot_frame", &sample_runtime,
      "runtime wire-level status snapshot sample",
      picfw_runtime_build_status_snapshot_frame, &report->status_snapshot);

  sample_runtime = runtime;
  picfw_runtime_start_scan_window(&sample_runtime);
  sample_runtime.now_ms = sample_runtime.scan_probe_deadline_ms;
  picfw_runtime_continue_scan_window(&sample_runtime);
  picfw_oracle_build_status_frame(
      "build_status_variant_frame", &sample_runtime,
      "runtime wire-level status variant sample after first scan dispatch",
      picfw_runtime_build_status_variant_frame, &report->status_variant);
  return 0;
}

static int
picfw_oracle_run_runtime_exchange(picfw_runtime_t *runtime, uint8_t command,
                                  const char *request_name, uint8_t data,
                                  uint32_t now_ms, const char *outcome,
                                  picfw_runtime_exchange_sample_t *sample) {
  uint8_t response_bytes[8];
  size_t idx;

  if (runtime == NULL || request_name == NULL || outcome == NULL ||
      sample == NULL) {
    return -1;
  }

  memset(sample, 0, sizeof(*sample));
  sample->state_before = picfw_runtime_snapshot(runtime);
  sample->request.command = command;
  sample->request.data = data;
  sample->request_name = request_name;
  sample->outcome = outcome;

  sample->request_len = picfw_enh_encode(command, data, sample->request_hex,
                                         sizeof(sample->request_hex));
  sample->request_consumed = (int)sample->request_len;
  if (sample->request_len != 2u) {
    return -1;
  }

  for (idx = 0u; idx < sample->request_len; ++idx) {
    if (!picfw_runtime_isr_enqueue_host_byte(runtime,
                                             sample->request_hex[idx])) {
      return -1;
    }
  }

  picfw_runtime_step(runtime, now_ms);
  sample->response_len = picfw_runtime_drain_host_tx(runtime, response_bytes,
                                                     sizeof(response_bytes));
  sample->state_after = picfw_runtime_snapshot(runtime);

  if (sample->response_len == 0u) {
    sample->has_response = PICFW_FALSE;
    return 0;
  }

  if (sample->response_len > sizeof(sample->response_hex)) {
    return -1;
  }
  memcpy(sample->response_hex, response_bytes, sample->response_len);
  if (picfw_oracle_decode_stream(sample->response_hex, sample->response_len,
                                 &sample->response,
                                 &sample->response_consumed) != 0) {
    return -1;
  }
  sample->has_response = PICFW_TRUE;
  return 0;
}

static int
picfw_oracle_build_runtime_report(picfw_runtime_contract_report_t *report) {
  picfw_runtime_t runtime;
  picfw_runtime_t send_without_start_runtime;

  if (report == NULL) {
    return -1;
  }

  picfw_runtime_init(&runtime, NULL);
  if (picfw_oracle_run_runtime_exchange(&runtime, PICFW_ENH_REQ_INIT, "init",
                                        0x01u, 1u, "resetted",
                                        &report->init) != 0) {
    return -1;
  }
  if (picfw_oracle_run_runtime_exchange(&runtime, PICFW_ENH_REQ_START, "start",
                                        0x31u, 2u, "started",
                                        &report->start_success) != 0) {
    return -1;
  }
  if (picfw_oracle_run_runtime_exchange(&runtime, PICFW_ENH_REQ_SEND, "send",
                                        0x55u, 3u, "received",
                                        &report->send_after_start) != 0) {
    return -1;
  }
  if (picfw_oracle_run_runtime_exchange(&runtime, PICFW_ENH_REQ_SEND, "send",
                                        PICFW_RUNTIME_SYN_BYTE, 4u, "received",
                                        &report->send_cancel) != 0) {
    return -1;
  }
  if (picfw_oracle_run_runtime_exchange(
          &runtime, PICFW_ENH_REQ_START, "start", PICFW_RUNTIME_SYN_BYTE, 5u,
          "start_cancelled", &report->start_cancel) != 0) {
    return -1;
  }

  picfw_runtime_init(&send_without_start_runtime, NULL);
  if (picfw_oracle_run_runtime_exchange(
          &send_without_start_runtime, PICFW_ENH_REQ_SEND, "send", 0x55u, 6u,
          "error_host", &report->send_without_start) != 0) {
    return -1;
  }

  return 0;
}

static void picfw_oracle_print_runtime_snapshot(
    const picfw_runtime_session_snapshot_t *snapshot, size_t field_indent,
    size_t inner_indent) {
  printf("{\n");
  picfw_oracle_print_indent(inner_indent);
  printf("\"active\": %s", snapshot->active ? "true" : "false");
  if (snapshot->active) {
    printf(",\n");
    picfw_oracle_print_indent(inner_indent);
    printf("\"initiator\": %u\n", (unsigned)snapshot->initiator);
  } else {
    printf("\n");
  }
  picfw_oracle_print_indent(field_indent);
  printf("}");
}

static void
picfw_oracle_print_runtime_sample(const picfw_runtime_exchange_sample_t *sample,
                                  size_t object_indent, size_t field_indent,
                                  size_t array_indent) {
  printf("{\n");

  picfw_oracle_print_indent(field_indent);
  printf("\"request_hex\": ");
  picfw_oracle_print_hex_array(sample->request_hex, sample->request_len,
                               array_indent);
  printf(",\n");

  picfw_oracle_print_indent(field_indent);
  printf("\"request\": {\n");
  picfw_oracle_print_indent(array_indent);
  printf("\"command\": %u,\n", (unsigned)sample->request.command);
  picfw_oracle_print_indent(array_indent);
  printf("\"command_name\": \"%s\",\n", sample->request_name);
  picfw_oracle_print_indent(array_indent);
  printf("\"data\": %u,\n", (unsigned)sample->request.data);
  picfw_oracle_print_indent(array_indent);
  printf("\"consumed\": %d\n", sample->request_consumed);
  picfw_oracle_print_indent(field_indent);
  printf("},\n");

  picfw_oracle_print_indent(field_indent);
  printf("\"response_hex\": ");
  if (sample->has_response) {
    picfw_oracle_print_hex_array(sample->response_hex, sample->response_len,
                                 array_indent);
  } else {
    printf("null");
  }
  printf(",\n");

  picfw_oracle_print_indent(field_indent);
  printf("\"response\": ");
  if (sample->has_response) {
    printf("{\n");
    picfw_oracle_print_indent(array_indent);
    printf("\"command\": %u,\n", (unsigned)sample->response.command);
    picfw_oracle_print_indent(array_indent);
    printf("\"command_name\": \"%s\",\n",
           picfw_oracle_response_name(sample->response.command));
    picfw_oracle_print_indent(array_indent);
    printf("\"data\": %u,\n", (unsigned)sample->response.data);
    picfw_oracle_print_indent(array_indent);
    printf("\"consumed\": %d\n", sample->response_consumed);
    picfw_oracle_print_indent(field_indent);
    printf("}");
  } else {
    printf("null");
  }
  printf(",\n");

  picfw_oracle_print_indent(field_indent);
  printf("\"outcome\": \"%s\",\n", sample->outcome);
  picfw_oracle_print_indent(field_indent);
  printf("\"state_before\": ");
  picfw_oracle_print_runtime_snapshot(&sample->state_before, field_indent,
                                      array_indent);
  printf(",\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"state_after\": ");
  picfw_oracle_print_runtime_snapshot(&sample->state_after, field_indent,
                                      array_indent);
  printf("\n");

  picfw_oracle_print_indent(object_indent);
  printf("}");
}

static void
picfw_oracle_print_scan_snapshot(const picfw_scan_state_snapshot_t *snapshot,
                                 size_t object_indent, size_t field_indent) {
  printf("{\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"state\": %u,\n", (unsigned)snapshot->state);
  picfw_oracle_print_indent(field_indent);
  printf("\"state_name\": \"%s\",\n",
         picfw_oracle_protocol_state_name(snapshot->state));
  picfw_oracle_print_indent(field_indent);
  printf("\"flags\": %u,\n", (unsigned)snapshot->flags);
  picfw_oracle_print_indent(field_indent);
  printf("\"flags_name\": \"%s\",\n",
         picfw_oracle_protocol_flags_name(snapshot->flags));
  picfw_oracle_print_indent(field_indent);
  printf("\"tick\": %u,\n", (unsigned)snapshot->tick);
  picfw_oracle_print_indent(field_indent);
  printf("\"deadline\": %u,\n", (unsigned)snapshot->deadline);
  picfw_oracle_print_indent(field_indent);
  printf("\"window_delay\": %u,\n", (unsigned)snapshot->window_delay);
  picfw_oracle_print_indent(field_indent);
  printf("\"window_limit\": %u,\n", (unsigned)snapshot->window_limit);
  picfw_oracle_print_indent(field_indent);
  printf("\"scan_seed\": %u,\n", (unsigned)snapshot->scan_seed);
  picfw_oracle_print_indent(field_indent);
  printf("\"merged_window\": %u,\n", (unsigned)snapshot->merged_window);
  picfw_oracle_print_indent(field_indent);
  printf("\"active_scan_slot\": %u,\n", (unsigned)snapshot->active_scan_slot);
  picfw_oracle_print_indent(field_indent);
  printf("\"descriptor_cursor\": %u\n", (unsigned)snapshot->descriptor_cursor);
  picfw_oracle_print_indent(object_indent);
  printf("}");
}

static void picfw_oracle_print_scan_deadline_example(
    const picfw_scan_deadline_example_t *example, size_t object_indent,
    size_t field_indent) {
  printf("{\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"now\": %u,\n", (unsigned)example->now);
  picfw_oracle_print_indent(field_indent);
  printf("\"requested_delay\": %u,\n", (unsigned)example->requested_delay);
  picfw_oracle_print_indent(field_indent);
  printf("\"effective_delay\": %u,\n", (unsigned)example->effective_delay);
  picfw_oracle_print_indent(field_indent);
  printf("\"deadline\": %u\n", (unsigned)example->deadline);
  picfw_oracle_print_indent(object_indent);
  printf("}");
}

static void picfw_oracle_print_scan_dispatch_sample(
    const picfw_scan_dispatch_sample_t *sample, size_t object_indent,
    size_t field_indent, size_t inner_indent) {
  printf("{\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"trigger\": \"%s\",\n", sample->trigger);
  picfw_oracle_print_indent(field_indent);
  printf("\"input_code\": %u,\n", (unsigned)sample->input_code);
  picfw_oracle_print_indent(field_indent);
  printf("\"return\": %u,\n", (unsigned)sample->return_code);
  picfw_oracle_print_indent(field_indent);
  printf("\"before\": ");
  picfw_oracle_print_scan_snapshot(&sample->before, field_indent, inner_indent);
  printf(",\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"after\": ");
  picfw_oracle_print_scan_snapshot(&sample->after, field_indent, inner_indent);
  printf(",\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"notes\": \"%s\"\n", sample->notes);
  picfw_oracle_print_indent(object_indent);
  printf("}");
}

static void picfw_oracle_print_status_frame_sample(
    const picfw_status_frame_sample_t *sample, size_t object_indent,
    size_t field_indent, size_t array_indent) {
  printf("{\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"builder\": \"%s\",\n", sample->builder);
  picfw_oracle_print_indent(field_indent);
  printf("\"prefix_hex\": ");
  picfw_oracle_print_hex_array(sample->prefix_hex, sample->prefix_len,
                               array_indent);
  printf(",\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"summary_hex\": ");
  picfw_oracle_print_hex_array(sample->summary_hex, sample->summary_len,
                               array_indent);
  printf(",\n");
  picfw_oracle_print_indent(field_indent);
  printf("\"length\": %u,\n", (unsigned)sample->length);
  picfw_oracle_print_indent(field_indent);
  printf("\"notes\": \"%s\"\n", sample->notes);
  picfw_oracle_print_indent(object_indent);
  printf("}");
}

static void picfw_oracle_print_info_queries(
    const picfw_adapter_version_t *version, const uint8_t *version_input,
    size_t version_input_len, const picfw_adapter_reset_info_t *reset,
    const uint8_t *reset_input, size_t reset_input_len) {
  uint8_t info_id;

  printf("[\n");
  for (info_id = PICFW_ADAPTER_INFO_VERSION;
       info_id <= PICFW_ADAPTER_INFO_WIFI_RSSI; ++info_id) {
    uint8_t request_hex[2];
    size_t request_len = picfw_enh_encode(PICFW_ENH_REQ_INFO, info_id,
                                          request_hex, sizeof(request_hex));
    picfw_bool_t modeled =
        (picfw_bool_t)(info_id == PICFW_ADAPTER_INFO_VERSION ||
                       info_id == PICFW_ADAPTER_INFO_RESET_INFO);

    picfw_oracle_print_indent(6u);
    printf("{\n");
    picfw_oracle_print_indent(8u);
    printf("\"id\": %u,\n", (unsigned)info_id);
    picfw_oracle_print_indent(8u);
    printf("\"name\": \"%s\",\n", picfw_oracle_info_name(info_id));
    picfw_oracle_print_indent(8u);
    printf("\"supported\": %s,\n",
           picfw_oracle_supports_info_id(version, info_id) ? "true" : "false");
    picfw_oracle_print_indent(8u);
    printf("\"modeled\": %s,\n", modeled ? "true" : "false");
    picfw_oracle_print_indent(8u);
    printf("\"request_hex\": ");
    picfw_oracle_print_hex_array(request_hex, request_len, 10u);

    if (info_id == PICFW_ADAPTER_INFO_VERSION) {
      printf(",\n");
      picfw_oracle_print_indent(8u);
      printf("\"response_hex\": ");
      picfw_oracle_print_hex_array(version_input, version_input_len, 10u);
      printf(",\n");
      picfw_oracle_print_indent(8u);
      printf("\"version\": {\n");
      picfw_oracle_print_indent(10u);
      printf("\"version\": %u,\n", (unsigned)version->version);
      picfw_oracle_print_indent(10u);
      printf("\"features\": %u,\n", (unsigned)version->features);
      picfw_oracle_print_indent(10u);
      printf("\"checksum\": %u,\n", (unsigned)version->checksum);
      picfw_oracle_print_indent(10u);
      printf("\"jumpers\": %u,\n", (unsigned)version->jumpers);
      picfw_oracle_print_indent(10u);
      printf("\"bootloader_version\": %u,\n",
             (unsigned)version->bootloader_version);
      picfw_oracle_print_indent(10u);
      printf("\"bootloader_checksum\": %u,\n",
             (unsigned)version->bootloader_checksum);
      picfw_oracle_print_indent(10u);
      printf("\"has_checksum\": %s,\n",
             version->has_checksum ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"has_bootloader\": %s,\n",
             version->has_bootloader ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"supports_info\": %s,\n",
             version->supports_info ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"is_wifi\": %s,\n", version->is_wifi ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"is_ethernet\": %s,\n", version->is_ethernet ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"is_high_speed\": %s,\n",
             version->is_high_speed ? "true" : "false");
      picfw_oracle_print_indent(10u);
      printf("\"is_v31\": %s\n", version->is_v31 ? "true" : "false");
      picfw_oracle_print_indent(8u);
      printf("},\n");
      picfw_oracle_print_indent(8u);
      printf("\"notes\": \"version response is modeled as the canonical INFO "
             "0x00 payload\"\n");
    } else if (info_id == PICFW_ADAPTER_INFO_RESET_INFO) {
      printf(",\n");
      picfw_oracle_print_indent(8u);
      printf("\"response_hex\": ");
      picfw_oracle_print_hex_array(reset_input, reset_input_len, 10u);
      printf(",\n");
      picfw_oracle_print_indent(8u);
      printf("\"reset_info\": {\n");
      picfw_oracle_print_indent(10u);
      printf("\"cause\": \"%s\",\n", reset->cause_name);
      picfw_oracle_print_indent(10u);
      printf("\"cause_code\": %u,\n", (unsigned)reset->cause_code);
      picfw_oracle_print_indent(10u);
      printf("\"restart_count\": %u\n", (unsigned)reset->restart_count);
      picfw_oracle_print_indent(8u);
      printf("},\n");
      picfw_oracle_print_indent(8u);
      printf("\"notes\": \"reset-info response is modeled as the canonical "
             "INFO 0x06 payload\"\n");
    } else {
      printf(",\n");
      picfw_oracle_print_indent(8u);
      printf("\"notes\": \"supported by adapter version, but no response "
             "parser is modeled here yet\"\n");
    }

    picfw_oracle_print_indent(6u);
    printf("}");
    if (info_id != PICFW_ADAPTER_INFO_WIFI_RSSI) {
      printf(",");
    }
    printf("\n");
  }
  picfw_oracle_print_indent(4u);
  printf("]");
}

static void picfw_oracle_print_scan_report(const picfw_scan_report_t *report) {
  size_t idx;

  printf("{\n");
  printf("    \"initial\": ");
  picfw_oracle_print_scan_snapshot(&report->initial, 4u, 6u);
  printf(",\n");
  printf("    \"deadline_examples\": [\n");
  for (idx = 0u; idx < 4u; ++idx) {
    picfw_oracle_print_indent(6u);
    picfw_oracle_print_scan_deadline_example(&report->deadline_examples[idx],
                                             6u, 8u);
    if (idx + 1u < 4u) {
      printf(",");
    }
    printf("\n");
  }
  printf("    ],\n");
  printf("    \"dispatch_samples\": [\n");
  for (idx = 0u; idx < 7u; ++idx) {
    picfw_oracle_print_indent(6u);
    picfw_oracle_print_scan_dispatch_sample(&report->dispatch_samples[idx], 6u,
                                            8u, 10u);
    if (idx + 1u < 7u) {
      printf(",");
    }
    printf("\n");
  }
  printf("    ],\n");
  printf("    \"status_snapshot\": ");
  picfw_oracle_print_status_frame_sample(&report->status_snapshot, 4u, 6u, 8u);
  printf(",\n");
  printf("    \"status_variant\": ");
  picfw_oracle_print_status_frame_sample(&report->status_variant, 4u, 6u, 8u);
  printf("\n");
  printf("  }");
}

static int picfw_oracle_check_enh(void) {
  uint8_t encoded[2];
  uint8_t received[2];
  picfw_enh_frame_t decoded;
  int consumed;

  if (picfw_oracle_decode_stream((const uint8_t[]){0x5Au}, 1u, &decoded,
                                 &consumed) != 0) {
    return 1;
  }
  if (decoded.command != PICFW_ENH_RES_RECEIVED || decoded.data != 0x5Au ||
      consumed != 1) {
    return 2;
  }
  if (picfw_enh_encode_received(decoded.data, received, sizeof(received)) !=
          1u ||
      received[0] != 0x5Au) {
    return 3;
  }

  if (picfw_enh_encode(PICFW_ENH_REQ_INFO, 0x06u, encoded, sizeof(encoded)) !=
      2u) {
    return 4;
  }
  if (picfw_oracle_decode_stream(encoded, sizeof(encoded), &decoded,
                                 &consumed) != 0) {
    return 5;
  }
  if (decoded.command != PICFW_ENH_REQ_INFO || decoded.data != 0x06u ||
      consumed != 2) {
    return 6;
  }

  return 0;
}

static int picfw_oracle_check_ens(void) {
  const uint8_t raw[] = {0x11u, 0xA9u, 0xAAu, 0x7Fu};
  const uint8_t expected_encoded[] = {0x11u, 0xA9u, 0x00u, 0xA9u, 0x01u, 0x7Fu};
  uint8_t encoded[8];
  uint8_t decoded[8];
  size_t encoded_len;
  int decoded_len;

  encoded_len = picfw_ens_encode(raw, sizeof(raw), encoded, sizeof(encoded));
  if (encoded_len != sizeof(expected_encoded) ||
      memcmp(encoded, expected_encoded, sizeof(expected_encoded)) != 0) {
    return 1;
  }

  decoded_len =
      picfw_ens_decode(encoded, encoded_len, decoded, sizeof(decoded));
  if (decoded_len != (int)sizeof(raw) ||
      memcmp(decoded, raw, sizeof(raw)) != 0) {
    return 2;
  }

  return 0;
}

static int picfw_oracle_check_info(void) {
  const uint8_t version_input[] = {0x12u, 0x01u, 0xABu, 0xCDu,
                                   0x1Eu, 0x34u, 0x56u, 0x78u};
  const uint8_t reset_input[] = {0x03u, 0x07u};
  picfw_adapter_version_t version;
  picfw_adapter_reset_info_t reset;

  if (picfw_info_parse_version(version_input, sizeof(version_input),
                               &version) != 0) {
    return 1;
  }
  if (version.version != 18u || version.features != 1u ||
      version.checksum != 0xABCDu || version.jumpers != 30u) {
    return 2;
  }
  if (version.bootloader_version != 52u ||
      version.bootloader_checksum != 0x5678u) {
    return 3;
  }
  if (version.has_checksum != PICFW_TRUE ||
      version.has_bootloader != PICFW_TRUE ||
      version.supports_info != PICFW_TRUE) {
    return 4;
  }
  if (version.is_wifi != PICFW_TRUE || version.is_ethernet != PICFW_TRUE ||
      version.is_high_speed != PICFW_TRUE || version.is_v31 != PICFW_TRUE) {
    return 5;
  }

  if (picfw_info_parse_reset(reset_input, sizeof(reset_input), &reset) != 0) {
    return 6;
  }
  if (strcmp(reset.cause_name, "watchdog") != 0 || reset.cause_code != 3u ||
      reset.restart_count != 7u) {
    return 7;
  }

  return 0;
}

static int picfw_oracle_check_runtime(void) {
  picfw_runtime_contract_report_t report;

  if (picfw_oracle_build_runtime_report(&report) != 0) {
    return 1;
  }
  if (!report.init.has_response ||
      report.init.response.command != PICFW_ENH_RES_RESETTED ||
      report.init.response.data != 0x01u) {
    return 2;
  }
  if (!report.start_success.has_response ||
      report.start_success.response.command != PICFW_ENH_RES_STARTED ||
      report.start_success.response.data != 0x31u) {
    return 3;
  }
  if (!report.send_after_start.has_response ||
      report.send_after_start.response.command != PICFW_ENH_RES_RECEIVED ||
      report.send_after_start.response.data != 0x55u) {
    return 4;
  }
  if (!report.send_cancel.has_response ||
      report.send_cancel.response.command != PICFW_ENH_RES_RECEIVED ||
      report.send_cancel.response.data != PICFW_RUNTIME_SYN_BYTE) {
    return 5;
  }
  if (!report.send_without_start.has_response ||
      report.send_without_start.response.command != PICFW_ENH_RES_ERROR_HOST ||
      report.send_without_start.response.data !=
          PICFW_RUNTIME_ERROR_SEND_WITHOUT_SESSION) {
    return 6;
  }
  if (report.start_cancel.has_response ||
      report.start_cancel.state_after.active != PICFW_FALSE) {
    return 7;
  }

  return 0;
}

static int picfw_oracle_check_descriptor_merge(void) {
  picfw_runtime_t rt;
  picfw_runtime_config_t config;

  picfw_runtime_config_init_default(&config);

  /* descriptor_merge_with_seed: xor_key=0 path */
  picfw_runtime_init(&rt, &config);
  rt.scan_mask_seed = PICFW_RUNTIME_DESCRIPTOR_XOR_KEY;
  rt.scan_seed = 0x000002A4u;
  rt.merged_window_ms = 0u;
  picfw_runtime_descriptor_merge_with_seed(&rt, 0xFFFFFFFFu, 0xFFFFFFFFu);
  if (rt.status_seed_latch != 0xA4u) {
    fprintf(stderr, "descriptor merge xor_key=0: seed_latch mismatch\n");
    return 1;
  }

  /* descriptor_merge_with_seed: xor_key!=0 path */
  picfw_runtime_init(&rt, &config);
  rt.scan_mask_seed = 0x00010601u;
  rt.scan_seed = 0x000002A4u;
  rt.merged_window_ms = 0u;
  picfw_runtime_descriptor_merge_with_seed(&rt, 0xFFFF00FFu, 0xFFFFFFFFu);
  if (rt.merged_window_ms == 0u) {
    fprintf(stderr, "descriptor merge xor_key!=0: merged_window should be non-zero\n");
    return 1;
  }

  /* post_merge_validate: limit floor */
  picfw_runtime_init(&rt, &config);
  rt.protocol_state = PICFW_PROTOCOL_STATE_READY;
  rt.scan_window_limit_ms = 0x00000050u;
  rt.scan_window_delay_ms = 0x00000040u;
  rt.merged_window_ms = 0x00000080u;
  picfw_runtime_post_merge_validate(&rt);
  if (rt.scan_window_limit_ms != PICFW_RUNTIME_SCAN_WINDOW_LIMIT_FLOOR) {
    fprintf(stderr, "post_merge_validate: limit not clamped to floor\n");
    return 1;
  }

  /* load_descriptor_block */
  picfw_runtime_init(&rt, &config);
  rt.protocol_state = PICFW_PROTOCOL_STATE_READY;
  rt.scan_mask_seed = 0x00010601u;
  rt.scan_seed = 0x000002A4u;
  rt.scan_window_limit_ms = 0x00000200u;
  rt.descriptor_data[0] = 0xAAu;
  rt.descriptor_data[1] = 0xBBu;
  rt.descriptor_data[2] = 0xCCu;
  rt.descriptor_data[3] = 0xDDu;
  rt.descriptor_data[4] = 0xFFu;
  rt.descriptor_data[5] = 0xFFu;
  rt.descriptor_data[6] = 0xFFu;
  rt.descriptor_data[7] = 0xFFu;
  rt.descriptor_data_len = 8u;
  rt.descriptor_data_pos = 0u;
  if (!picfw_runtime_load_descriptor_block(&rt)) {
    fprintf(stderr, "load_descriptor_block: returned false\n");
    return 1;
  }
  if (rt.descriptor_data_pos != 8u) {
    fprintf(stderr, "load_descriptor_block: pos not advanced\n");
    return 1;
  }

  /* shift_scan_masks_by_delta */
  picfw_runtime_init(&rt, &config);
  rt.protocol_state = PICFW_PROTOCOL_STATE_READY;
  rt.scan_mask_seed = 0x00010601u;
  rt.scan_seed = 0x000002A4u;
  rt.scan_window_limit_ms = 0x00000200u;
  rt.descriptor_data[0] = 0xFFu;
  rt.descriptor_data[1] = 0xFFu;
  rt.descriptor_data[2] = 0xFFu;
  rt.descriptor_data[3] = 0xFFu;
  rt.descriptor_data[4] = 0xFFu;
  rt.descriptor_data[5] = 0xFFu;
  rt.descriptor_data[6] = 0xFFu;
  rt.descriptor_data[7] = 0xFFu;
  rt.descriptor_data_len = 8u;
  rt.descriptor_data_pos = 0u;
  if (!picfw_runtime_shift_scan_masks_by_delta(&rt, 0x05u)) {
    fprintf(stderr, "shift_scan_masks_by_delta: returned false\n");
    return 1;
  }

  return 0;
}

static int picfw_oracle_check_scan_slot_full(void) {
  picfw_runtime_t rt;
  picfw_runtime_config_t config;

  picfw_runtime_config_init_default(&config);

  /* initialize_scan_slot_full: slot 0x06 */
  picfw_runtime_init(&rt, &config);
  rt.scan_seed = 0x000002A4u;
  rt.saved_scan_seed = 0x000002A4u;
  rt.saved_scan_deadline_ms = 100u;
  rt.scan_window_delay_ms = 0x50u;
  picfw_runtime_initialize_scan_slot_full(&rt, 0x06u);
  if (rt.scan_mask_seed != 0x00060101u) {
    fprintf(stderr, "init_slot_full 0x06: scan_mask_seed mismatch: 0x%08x\n",
            (unsigned)rt.scan_mask_seed);
    return 1;
  }
  if (rt.current_scan_slot_id != 0x06u) {
    fprintf(stderr, "init_slot_full 0x06: slot_id mismatch\n");
    return 1;
  }

  /* recompute_scan_masks_tail: cursor advancement */
  picfw_runtime_init(&rt, &config);
  rt.scan_seed = 0x00000099u;
  rt.descriptor_cursor = 0x0268u;
  rt.scan_accum_r24 = 0x11u;
  rt.scan_accum_r8 = 0x2200u;
  rt.scan_accum_l8 = 0x330000u;
  rt.scan_accum_l24 = 0x44000000u;
  picfw_runtime_recompute_scan_masks_tail(&rt);
  if (rt.descriptor_cursor != (uint16_t)(0x0268u + 0x2Cu)) {
    fprintf(stderr,
            "recompute_tail: cursor not advanced: 0x%04x\n",
            (unsigned)rt.descriptor_cursor);
    return 1;
  }

  /* app_main_loop_init */
  picfw_runtime_init(&rt, &config);
  rt.scan_seed = 0x000002A4u;
  rt.saved_scan_seed = 0x000002A4u;
  picfw_runtime_app_main_loop_init(&rt);
  if (rt.status_seed_latch != 0xA4u) {
    fprintf(stderr, "app_main_loop_init: seed_latch mismatch\n");
    return 1;
  }

  /* slot-level operations: probe+prime+poll cycle */
  picfw_runtime_init(&rt, &config);
  rt.startup_state = PICFW_STARTUP_LIVE_READY;
  rt.protocol_state = PICFW_PROTOCOL_STATE_READY;
  rt.protocol_state_flags = 0x03u;
  rt.descriptor_data[0] = 0xFFu;
  rt.descriptor_data_len = 8u;
  rt.descriptor_data_pos = 0u;
  if (!picfw_runtime_probe_register_window(&rt)) {
    fprintf(stderr, "probe: returned false with data\n");
    return 1;
  }
  picfw_runtime_prime_scan_slot(&rt);
  if (rt.protocol_state != PICFW_PROTOCOL_STATE_SCAN) {
    fprintf(stderr, "prime: state not SCAN\n");
    return 1;
  }
  rt.descriptor_data_pos = 8u;
  if (!picfw_runtime_poll_scan_slot(&rt)) {
    fprintf(stderr, "poll: returned false when data consumed\n");
    return 1;
  }

  return 0;
}

static int picfw_oracle_check_scan(void) {
  picfw_scan_report_t report;

  if (picfw_oracle_build_scan_report(&report) != 0) {
    return 1;
  }
  if (report.initial.state != PICFW_PROTOCOL_STATE_READY ||
      report.initial.flags != 0u) {
    return 2;
  }
  if (report.deadline_examples[0].effective_delay !=
      PICFW_RUNTIME_SCAN_MIN_DELAY_MS) {
    return 3;
  }
  if (report.deadline_examples[2].deadline != 44u) {
    return 4;
  }
  if (report.dispatch_samples[0].after.descriptor_cursor != 0x0264u ||
      report.dispatch_samples[1].after.descriptor_cursor != 0x0260u ||
      report.dispatch_samples[4].after.descriptor_cursor != 0x0268u) {
    return 5;
  }
  if (report.dispatch_samples[2].after.window_limit != 0xA402A402u ||
      report.dispatch_samples[5].after.window_delay != 0xA402A402u ||
      report.dispatch_samples[6].after.merged_window != 0xA402A402u) {
    return 6;
  }
  if (report.status_snapshot.length != PICFW_RUNTIME_STATUS_FRAME_MAX ||
      report.status_variant.length != PICFW_RUNTIME_STATUS_FRAME_MAX ||
      report.status_snapshot.prefix_hex[4] != 0x35u ||
      report.status_variant.prefix_hex[4] != 0x37u) {
    return 7;
  }
  if (report.status_snapshot.summary_hex[12] != '2' ||
      report.status_snapshot.summary_hex[13] != '4' ||
      report.status_variant.summary_hex[12] != '2' ||
      report.status_variant.summary_hex[13] != '4') {
    return 8;
  }

  return 0;
}

static void picfw_oracle_emit_json(void) {
  const uint8_t short_input[] = {0x5Au};
  const uint8_t ens_raw[] = {0x11u, 0xA9u, 0xAAu, 0x7Fu};
  const uint8_t version_input[] = {0x12u, 0x01u, 0xABu, 0xCDu,
                                   0x1Eu, 0x34u, 0x56u, 0x78u};
  const uint8_t reset_input[] = {0x03u, 0x07u};
  uint8_t encoded_info[2];
  uint8_t short_reencoded[2];
  uint8_t encoded_reencoded[2];
  uint8_t ens_encoded[8];
  uint8_t ens_decoded[8];
  picfw_enh_frame_t short_decoded;
  picfw_enh_frame_t encoded_decoded;
  picfw_adapter_version_t version;
  picfw_adapter_reset_info_t reset;
  picfw_runtime_contract_report_t runtime_report;
  picfw_scan_report_t scan_report;
  int short_consumed;
  int encoded_consumed;
  size_t short_reencoded_len;
  size_t encoded_len;
  size_t encoded_reencoded_len;
  size_t ens_encoded_len;
  int ens_decoded_len;

  if (picfw_oracle_decode_stream(short_input, sizeof(short_input),
                                 &short_decoded, &short_consumed) != 0) {
    fprintf(stderr, "failed to decode ENH short-form sample\n");
    exit(1);
  }
  short_reencoded_len = picfw_enh_encode_received(
      short_decoded.data, short_reencoded, sizeof(short_reencoded));

  encoded_len = picfw_enh_encode(PICFW_ENH_REQ_INFO, 0x06u, encoded_info,
                                 sizeof(encoded_info));
  if (encoded_len != 2u) {
    fprintf(stderr, "failed to encode ENH info sample\n");
    exit(1);
  }
  if (picfw_oracle_decode_stream(encoded_info, encoded_len, &encoded_decoded,
                                 &encoded_consumed) != 0) {
    fprintf(stderr, "failed to decode ENH info sample\n");
    exit(1);
  }
  encoded_reencoded_len =
      picfw_enh_encode(encoded_decoded.command, encoded_decoded.data,
                       encoded_reencoded, sizeof(encoded_reencoded));

  ens_encoded_len = picfw_ens_encode(ens_raw, sizeof(ens_raw), ens_encoded,
                                     sizeof(ens_encoded));
  ens_decoded_len = picfw_ens_decode(ens_encoded, ens_encoded_len, ens_decoded,
                                     sizeof(ens_decoded));

  if (picfw_info_parse_version(version_input, sizeof(version_input),
                               &version) != 0) {
    fprintf(stderr, "failed to parse INFO version sample\n");
    exit(1);
  }
  if (picfw_info_parse_reset(reset_input, sizeof(reset_input), &reset) != 0) {
    fprintf(stderr, "failed to parse INFO reset sample\n");
    exit(1);
  }
  if (picfw_oracle_build_runtime_report(&runtime_report) != 0) {
    fprintf(stderr, "failed to build runtime report\n");
    exit(1);
  }
  if (picfw_oracle_build_scan_report(&scan_report) != 0) {
    fprintf(stderr, "failed to build scan report\n");
    exit(1);
  }

  printf("{\n");
  printf("  \"enh\": {\n");
  printf("    \"short_form_received\": {\n");
  printf("      \"input_hex\": ");
  picfw_oracle_print_hex_array(short_input, sizeof(short_input), 8u);
  printf(",\n");
  printf("      \"decoded\": {\n");
  printf("        \"command\": %u,\n", (unsigned)short_decoded.command);
  printf("        \"command_name\": \"%s\",\n",
         picfw_oracle_response_name(short_decoded.command));
  printf("        \"data\": %u,\n", (unsigned)short_decoded.data);
  printf("        \"consumed\": %d\n", short_consumed);
  printf("      },\n");
  printf("      \"reencoded_hex\": ");
  picfw_oracle_print_hex_array(short_reencoded, short_reencoded_len, 8u);
  printf("\n");
  printf("    },\n");
  printf("    \"encoded_info\": {\n");
  printf("      \"input_hex\": ");
  picfw_oracle_print_hex_array(encoded_info, encoded_len, 8u);
  printf(",\n");
  printf("      \"decoded\": {\n");
  printf("        \"command\": %u,\n", (unsigned)encoded_decoded.command);
  printf("        \"command_name\": \"info\",\n");
  printf("        \"data\": %u,\n", (unsigned)encoded_decoded.data);
  printf("        \"consumed\": %d\n", encoded_consumed);
  printf("      },\n");
  printf("      \"reencoded_hex\": ");
  picfw_oracle_print_hex_array(encoded_reencoded, encoded_reencoded_len, 8u);
  printf("\n");
  printf("    }\n");
  printf("  },\n");
  printf("  \"ens\": {\n");
  printf("    \"raw_hex\": ");
  picfw_oracle_print_hex_array(ens_raw, sizeof(ens_raw), 6u);
  printf(",\n");
  printf("    \"encoded_hex\": ");
  picfw_oracle_print_hex_array(ens_encoded, ens_encoded_len, 6u);
  printf(",\n");
  printf("    \"decoded_hex\": ");
  picfw_oracle_print_hex_array(ens_decoded, (size_t)ens_decoded_len, 6u);
  printf("\n");
  printf("  },\n");
  printf("  \"info\": {\n");
  printf("    \"version\": {\n");
  printf("      \"input_hex\": ");
  picfw_oracle_print_hex_array(version_input, sizeof(version_input), 8u);
  printf(",\n");
  printf("      \"parsed\": {\n");
  printf("        \"version\": %u,\n", (unsigned)version.version);
  printf("        \"features\": %u,\n", (unsigned)version.features);
  printf("        \"checksum\": %u,\n", (unsigned)version.checksum);
  printf("        \"jumpers\": %u,\n", (unsigned)version.jumpers);
  printf("        \"bootloader_version\": %u,\n",
         (unsigned)version.bootloader_version);
  printf("        \"bootloader_checksum\": %u,\n",
         (unsigned)version.bootloader_checksum);
  printf("        \"has_checksum\": %s,\n",
         version.has_checksum ? "true" : "false");
  printf("        \"has_bootloader\": %s,\n",
         version.has_bootloader ? "true" : "false");
  printf("        \"supports_info\": %s,\n",
         version.supports_info ? "true" : "false");
  printf("        \"is_wifi\": %s,\n", version.is_wifi ? "true" : "false");
  printf("        \"is_ethernet\": %s,\n",
         version.is_ethernet ? "true" : "false");
  printf("        \"is_high_speed\": %s,\n",
         version.is_high_speed ? "true" : "false");
  printf("        \"is_v31\": %s\n", version.is_v31 ? "true" : "false");
  printf("      },\n");
  printf("      \"supports_info_query_0\": %s,\n",
         version.supports_info ? "true" : "false");
  printf("      \"supports_info_query_6\": %s\n",
         (version.supports_info && version.has_bootloader) ? "true" : "false");
  printf("    },\n");
  printf("    \"reset_info\": {\n");
  printf("      \"input_hex\": ");
  picfw_oracle_print_hex_array(reset_input, sizeof(reset_input), 8u);
  printf(",\n");
  printf("      \"parsed\": {\n");
  printf("        \"cause\": \"%s\",\n", reset.cause_name);
  printf("        \"cause_code\": %u,\n", (unsigned)reset.cause_code);
  printf("        \"restart_count\": %u\n", (unsigned)reset.restart_count);
  printf("      }\n");
  printf("    },\n");
  printf("    \"queries\": ");
  picfw_oracle_print_info_queries(&version, version_input,
                                  sizeof(version_input), &reset, reset_input,
                                  sizeof(reset_input));
  printf("\n");
  printf("  },\n");
  printf("  \"runtime\": {\n");
  printf("    \"init\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.init, 4u, 6u, 8u);
  printf(",\n");
  printf("    \"start_success\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.start_success, 4u, 6u, 8u);
  printf(",\n");
  printf("    \"send_after_start\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.send_after_start, 4u, 6u,
                                    8u);
  printf(",\n");
  printf("    \"send_cancel\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.send_cancel, 4u, 6u, 8u);
  printf(",\n");
  printf("    \"send_without_start\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.send_without_start, 4u, 6u,
                                    8u);
  printf(",\n");
  printf("    \"start_cancel\": ");
  picfw_oracle_print_runtime_sample(&runtime_report.start_cancel, 4u, 6u, 8u);
  printf("\n");
  printf("  },\n");
  printf("  \"scan\": ");
  picfw_oracle_print_scan_report(&scan_report);
  printf("\n");
  printf("}\n");
}

int main(int argc, char **argv) {
  picfw_bool_t emit_json;
  int rc;

  emit_json = PICFW_FALSE;
  if (argc > 1 && strcmp(argv[1], "--json") == 0) {
    emit_json = PICFW_TRUE;
  }

  if (emit_json) {
    picfw_oracle_emit_json();
    return 0;
  }

  rc = picfw_oracle_check_enh();
  if (rc != 0) {
    fprintf(stderr, "enh oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_ens();
  if (rc != 0) {
    fprintf(stderr, "ens oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_info();
  if (rc != 0) {
    fprintf(stderr, "info oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_runtime();
  if (rc != 0) {
    fprintf(stderr, "runtime oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_scan();
  if (rc != 0) {
    fprintf(stderr, "scan oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_descriptor_merge();
  if (rc != 0) {
    fprintf(stderr, "descriptor merge oracle check failed: %d\n", rc);
    return rc;
  }

  rc = picfw_oracle_check_scan_slot_full();
  if (rc != 0) {
    fprintf(stderr, "scan slot full oracle check failed: %d\n", rc);
    return rc;
  }

  puts("picfw oracle check passed");
  return 0;
}
