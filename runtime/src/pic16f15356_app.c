#include "picfw/pic16f15356_app.h"

void picfw_pic16f15356_app_init(picfw_pic16f15356_app_t *app, const picfw_runtime_config_t *config) {
  if (app == 0) {
    return;
  }

  picfw_runtime_init(&app->runtime, config);
  picfw_pic16f15356_hal_runtime_init(&app->hal);
}

picfw_bool_t picfw_pic16f15356_app_isr_host_rx(picfw_pic16f15356_app_t *app, uint8_t byte) {
  if (app == 0) {
    return PICFW_FALSE;
  }
  return picfw_pic16f15356_isr_latch_host_rx(&app->hal, byte);
}

picfw_bool_t picfw_pic16f15356_app_isr_bus_rx(picfw_pic16f15356_app_t *app, uint8_t byte) {
  if (app == 0) {
    return PICFW_FALSE;
  }
  return picfw_pic16f15356_isr_latch_bus_rx(&app->hal, byte);
}

void picfw_pic16f15356_app_isr_tmr0(picfw_pic16f15356_app_t *app) {
  if (app == 0) {
    return;
  }
  picfw_pic16f15356_isr_latch_tmr0(&app->hal);
}

picfw_bool_t picfw_pic16f15356_app_mainline_service(picfw_pic16f15356_app_t *app) {
  if (app == 0) {
    return PICFW_FALSE;
  }
  return picfw_pic16f15356_mainline_service(&app->hal, &app->runtime);
}

size_t picfw_pic16f15356_app_drain_host_tx(picfw_pic16f15356_app_t *app, uint8_t *out, size_t out_cap) {
  if (app == 0) {
    return 0u;
  }
  return picfw_pic16f15356_hal_drain_host_tx(&app->hal, out, out_cap);
}
