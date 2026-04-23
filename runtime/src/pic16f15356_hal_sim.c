#include "picfw/pic16f15356_hal_internal.h"

#if PICFW_HAL_ACTIVE_PROFILE != PICFW_HAL_PROFILE_SIM
#error "pic16f15356_hal_sim.c requires PICFW_HAL_PROFILE_SIM"
#endif

#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(
    (PICFW_RUNTIME_HOST_TX_CAP & (PICFW_RUNTIME_HOST_TX_CAP - 1u)) == 0u,
    "PICFW_RUNTIME_HOST_TX_CAP must be power of 2 for simulation outbox");
#endif

static void sim_outbox_reset(picfw_pic16f15356_tx_outbox_t *outbox) {
  if (outbox == 0) {
    return;
  }

  outbox->head = 0u;
  outbox->tail = 0u;
  outbox->count = 0u;
}

static picfw_bool_t sim_outbox_push(picfw_pic16f15356_tx_outbox_t *outbox,
                                    uint8_t value) {
  if (outbox == 0 || outbox->count >= PICFW_RUNTIME_HOST_TX_CAP) {
    return PICFW_FALSE;
  }

  outbox->items[outbox->tail] = value;
  outbox->tail =
      (uint8_t)((outbox->tail + 1u) & (PICFW_RUNTIME_HOST_TX_CAP - 1u));
  outbox->count++;
  return PICFW_TRUE;
}

static picfw_bool_t sim_outbox_pop(picfw_pic16f15356_tx_outbox_t *outbox,
                                   uint8_t *value) {
  if (outbox == 0 || value == 0 || outbox->count == 0u) {
    return PICFW_FALSE;
  }

  *value = outbox->items[outbox->head];
  outbox->head =
      (uint8_t)((outbox->head + 1u) & (PICFW_RUNTIME_HOST_TX_CAP - 1u));
  outbox->count--;
  return PICFW_TRUE;
}

void picfw_pic16f15356_hal_profile_reset(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  hal->profile.porta_input = 0u;
  hal->profile.portb_input = 0u;
  hal->profile.portc_input = 0u;
  sim_outbox_reset(&hal->profile.host_tx_outbox);
}

void picfw_pic16f15356_hal_profile_apply_power_on_defaults(
    picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  hal->profile.porta_input = 0x33u;
  hal->profile.portb_input = 0xC2u;
  hal->profile.portc_input = 0x00u;
}

picfw_bool_t picfw_pic16f15356_hal_profile_read_pin(
    const picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit) {
  uint8_t port_val;

  if (hal == 0) {
    return PICFW_FALSE;
  }

  switch (port) {
  case PICFW_PORT_A:
    port_val = hal->profile.porta_input;
    break;
  case PICFW_PORT_B:
    port_val = hal->profile.portb_input;
    break;
  case PICFW_PORT_C:
    port_val = hal->profile.portc_input;
    break;
  default:
    return PICFW_FALSE;
  }

  return (picfw_bool_t)((port_val >> bit) & 1u);
}

void picfw_pic16f15356_hal_profile_write_pin(picfw_pic16f15356_hal_t *hal,
                                             uint8_t port, uint8_t bit,
                                             picfw_bool_t value) {
  uint8_t *lat;

  if (hal == 0) {
    return;
  }

  switch (port) {
  case PICFW_PORT_A:
    lat = &hal->regs.lata;
    break;
  case PICFW_PORT_B:
    lat = &hal->regs.latb;
    break;
  case PICFW_PORT_C:
    lat = &hal->regs.latc;
    break;
  default:
    return;
  }

  if (value) {
    *lat = (uint8_t)(*lat | (1u << bit));
  } else {
    *lat = (uint8_t)(*lat & ~(1u << bit));
  }
}

picfw_bool_t picfw_pic16f15356_hal_profile_host_tx_sink_ready(
    const picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  return (picfw_bool_t)(hal->profile.host_tx_outbox.count <
                        PICFW_RUNTIME_HOST_TX_CAP);
}

void picfw_pic16f15356_hal_profile_emit_host_tx_byte(
    picfw_pic16f15356_hal_t *hal, uint8_t byte) {
  if (hal == 0) {
    return;
  }

  hal->regs.tx2reg = byte;
  if (!sim_outbox_push(&hal->profile.host_tx_outbox, byte)) {
    hal->latches.host_tx_overruns++;
  }
}

size_t picfw_pic16f15356_hal_profile_drain_host_tx(
    picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap) {
  size_t out_len = 0u;

  if (hal == 0 || out == 0) {
    return 0u;
  }

  while (out_len < out_cap &&
         hal->profile.host_tx_outbox.count > 0u) {
    if (!sim_outbox_pop(&hal->profile.host_tx_outbox, &out[out_len])) {
      break;
    }
    out_len++;
  }

  return out_len;
}
