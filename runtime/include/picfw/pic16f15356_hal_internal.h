#ifndef PICFW_PIC16F15356_HAL_INTERNAL_H
#define PICFW_PIC16F15356_HAL_INTERNAL_H

#include "pic16f15356_hal.h"

void picfw_pic16f15356_hal_profile_reset(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_profile_apply_power_on_defaults(
    picfw_pic16f15356_hal_t *hal);
picfw_bool_t picfw_pic16f15356_hal_profile_read_pin(
    const picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit);
void picfw_pic16f15356_hal_profile_write_pin(picfw_pic16f15356_hal_t *hal,
                                             uint8_t port, uint8_t bit,
                                             picfw_bool_t value);
picfw_bool_t picfw_pic16f15356_hal_profile_host_tx_sink_ready(
    const picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_profile_emit_host_tx_byte(
    picfw_pic16f15356_hal_t *hal, uint8_t byte);
size_t picfw_pic16f15356_hal_profile_drain_host_tx(
    picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap);

#endif
