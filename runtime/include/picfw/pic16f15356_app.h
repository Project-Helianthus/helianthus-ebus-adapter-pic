#ifndef PICFW_PIC16F15356_APP_H
#define PICFW_PIC16F15356_APP_H

#include "pic16f15356_hal.h"

typedef struct picfw_pic16f15356_app {
  picfw_runtime_t runtime;
  picfw_pic16f15356_hal_t hal;
} picfw_pic16f15356_app_t;

void picfw_pic16f15356_app_init(picfw_pic16f15356_app_t *app, const picfw_runtime_config_t *config);
picfw_bool_t picfw_pic16f15356_app_isr_host_rx(picfw_pic16f15356_app_t *app, uint8_t byte);
picfw_bool_t picfw_pic16f15356_app_isr_bus_rx(picfw_pic16f15356_app_t *app, uint8_t byte);
void picfw_pic16f15356_app_isr_tmr0(picfw_pic16f15356_app_t *app);
picfw_bool_t picfw_pic16f15356_app_mainline_service(picfw_pic16f15356_app_t *app);
size_t picfw_pic16f15356_app_drain_host_tx(picfw_pic16f15356_app_t *app, uint8_t *out, size_t out_cap);

#endif
