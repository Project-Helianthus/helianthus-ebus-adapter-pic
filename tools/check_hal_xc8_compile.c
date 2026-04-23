#include "picfw/pic16f15356_app.h"

int main(void) {
  picfw_pic16f15356_app_t app;

  picfw_pic16f15356_app_init(&app, 0);
  picfw_pic16f15356_app_isr_tmr0(&app);
  picfw_pic16f15356_app_isr_host_tx_ready(&app);
  (void)picfw_pic16f15356_app_mainline_service(&app);
  return 0;
}
