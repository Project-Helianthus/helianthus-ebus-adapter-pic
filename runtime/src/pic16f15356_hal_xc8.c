#include "picfw/pic16f15356_hal_internal.h"

#if PICFW_HAL_ACTIVE_PROFILE == PICFW_HAL_PROFILE_XC8

void picfw_pic16f15356_hal_profile_reset(picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  hal->profile.host_txreg_loaded = PICFW_FALSE;
  hal->profile.bus_txreg_loaded = PICFW_FALSE;
}

void picfw_pic16f15356_hal_profile_apply_power_on_defaults(
    picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return;
  }

  /* Compile-time silicon scaffold.
   * These are register mirrors only; a future XC8 binding should replace
   * them with direct SFR reads captured at power-on before PPS routing. */
  hal->regs.porta = 0x33u;
  hal->regs.portb = 0xC2u;
  hal->regs.portc = 0x00u;
  hal->profile.host_txreg_loaded = PICFW_FALSE;
  hal->profile.bus_txreg_loaded = PICFW_FALSE;
}

picfw_bool_t picfw_pic16f15356_hal_profile_read_pin(
    const picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit) {
  uint8_t port_val;

  if (hal == 0) {
    return PICFW_FALSE;
  }

  switch (port) {
  case PICFW_PORT_A:
    port_val = hal->regs.porta;
    break;
  case PICFW_PORT_B:
    port_val = hal->regs.portb;
    break;
  case PICFW_PORT_C:
    port_val = hal->regs.portc;
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
  uint8_t *port_reg;

  if (hal == 0) {
    return;
  }

  switch (port) {
  case PICFW_PORT_A:
    lat = &hal->regs.lata;
    port_reg = &hal->regs.porta;
    break;
  case PICFW_PORT_B:
    lat = &hal->regs.latb;
    port_reg = &hal->regs.portb;
    break;
  case PICFW_PORT_C:
    lat = &hal->regs.latc;
    port_reg = &hal->regs.portc;
    break;
  default:
    return;
  }

  if (value) {
    *lat = (uint8_t)(*lat | (1u << bit));
    *port_reg = (uint8_t)(*port_reg | (1u << bit));
  } else {
    *lat = (uint8_t)(*lat & ~(1u << bit));
    *port_reg = (uint8_t)(*port_reg & ~(1u << bit));
  }
}

picfw_bool_t picfw_pic16f15356_hal_profile_host_tx_sink_ready(
    const picfw_pic16f15356_hal_t *hal) {
  if (hal == 0) {
    return PICFW_FALSE;
  }

  return (picfw_bool_t)(!hal->profile.host_txreg_loaded);
}

void picfw_pic16f15356_hal_profile_emit_host_tx_byte(
    picfw_pic16f15356_hal_t *hal, uint8_t byte) {
  if (hal == 0) {
    return;
  }

  hal->regs.tx2reg = byte;
  hal->profile.host_txreg_loaded = PICFW_TRUE;
}

size_t picfw_pic16f15356_hal_profile_drain_host_tx(
    picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap) {
  (void)hal;
  (void)out;
  (void)out_cap;
  return 0u;
}

#endif /* PICFW_HAL_ACTIVE_PROFILE == PICFW_HAL_PROFILE_XC8 */
