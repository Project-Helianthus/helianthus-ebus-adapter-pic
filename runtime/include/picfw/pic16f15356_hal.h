#ifndef PICFW_PIC16F15356_HAL_H
#define PICFW_PIC16F15356_HAL_H

#include "runtime.h"

#define PICFW_PIC16F15356_ISR_LATCH_CAP 16u
#define PICFW_PIC16F15356_MAINLINE_BYTE_BUDGET 8u

typedef enum picfw_pic16f15356_uart_mode {
  PICFW_PIC16F15356_UART_MODE_DEFAULT = 0u,
  PICFW_PIC16F15356_UART_MODE_HIGH_SPEED = 1u,
} picfw_pic16f15356_uart_mode_t;

typedef struct picfw_pic16f15356_byte_fifo {
  uint8_t items[PICFW_PIC16F15356_ISR_LATCH_CAP];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} picfw_pic16f15356_byte_fifo_t;

typedef struct picfw_pic16f15356_registers {
  uint8_t oscccon1;
  uint8_t oscfrq;
  uint8_t osctune;
  uint8_t t0con1;
  uint8_t t0con0;
  uint8_t tmr0h;
  uint8_t tmr0l;
  uint8_t baud1con;
  uint8_t rc1sta;
  uint8_t tx1sta;
  uint8_t sp1brgl;
  uint8_t sp1brgh;
} picfw_pic16f15356_registers_t;

typedef struct picfw_pic16f15356_latches {
  picfw_pic16f15356_byte_fifo_t host_rx_fifo;
  picfw_pic16f15356_byte_fifo_t bus_rx_fifo;
  picfw_pic16f15356_byte_fifo_t host_tx_fifo;
  uint16_t tmr0_isr_count;
  uint16_t scheduler_subticks;
  uint16_t scheduler_pending;
  uint32_t host_rx_overruns;
  uint32_t bus_rx_overruns;
  uint32_t host_tx_overruns;
} picfw_pic16f15356_latches_t;

typedef struct picfw_pic16f15356_hal {
  picfw_pic16f15356_registers_t regs;
  picfw_pic16f15356_latches_t latches;
  uint32_t current_fosc_hz;
  uint32_t runtime_now_ms;
  uint32_t runtime_step_count;
  picfw_pic16f15356_uart_mode_t uart_mode;
} picfw_pic16f15356_hal_t;

void picfw_pic16f15356_hal_reset(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_runtime_init(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_set_uart_mode(picfw_pic16f15356_hal_t *hal, picfw_pic16f15356_uart_mode_t mode);
uint16_t picfw_pic16f15356_hal_current_spbrg(const picfw_pic16f15356_hal_t *hal);
picfw_bool_t picfw_pic16f15356_isr_latch_host_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte);
picfw_bool_t picfw_pic16f15356_isr_latch_bus_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte);
void picfw_pic16f15356_isr_latch_tmr0(picfw_pic16f15356_hal_t *hal);
picfw_bool_t picfw_pic16f15356_mainline_service(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime);
size_t picfw_pic16f15356_hal_drain_host_tx(picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap);

#endif
