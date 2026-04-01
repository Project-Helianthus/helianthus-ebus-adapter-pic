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
  /* Oscillator */
  uint8_t oscccon1;
  uint8_t oscfrq;
  uint8_t osctune;
  /* Timer 0 */
  uint8_t t0con1;
  uint8_t t0con0;
  uint8_t tmr0h;
  uint8_t tmr0l;
  /* EUSART1 (bus UART — eBUS transceiver) */
  uint8_t baud1con;
  uint8_t rc1sta;
  uint8_t tx1sta;
  uint8_t sp1brgl;
  uint8_t sp1brgh;
  /* EUSART2 (host UART — ESP) */
  uint8_t baud2con;
  uint8_t rc2sta;
  uint8_t tx2sta;
  uint8_t sp2brgl;
  uint8_t sp2brgh;
  /* GPIO direction (0=output, 1=input) */
  uint8_t trisa;
  uint8_t trisb;
  uint8_t trisc;
  /* Analog select (0=digital, 1=analog) */
  uint8_t ansela;
  uint8_t anselb;
  uint8_t anselc;
  /* Output latches */
  uint8_t lata;
  uint8_t latb;
  uint8_t latc;
  /* Weak pull-ups */
  uint8_t wpub;
  /* PPS input select */
  uint8_t rx1pps;
  uint8_t rx2pps;
  /* PPS output (RB3 → EUSART1 TX, RC1 → EUSART2 TX) */
  uint8_t rb3pps;
  uint8_t rc1pps_out;
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
  /* Simulated port input state (set by test harness or ISR) */
  uint8_t porta_input;
  uint8_t portb_input;
  uint8_t portc_input;
  /* TX register empty flags (set by ISR, cleared by mainline) */
  picfw_bool_t host_tx_ready;
  picfw_bool_t bus_tx_ready;
} picfw_pic16f15356_latches_t;

typedef struct picfw_pic16f15356_hal {
  picfw_pic16f15356_registers_t regs;
  picfw_pic16f15356_latches_t latches;
  uint32_t current_fosc_hz;
  uint32_t runtime_now_ms;
  uint32_t runtime_step_count;
  picfw_pic16f15356_uart_mode_t uart_mode;
} picfw_pic16f15356_hal_t;

/* J12 AUX strap configuration (active-low: open=high, GND=low) */
typedef struct picfw_pic16f15356_straps {
  picfw_bool_t enhanced_protocol;  /* J12 pin 2: high=enhanced, low=standard */
  picfw_bool_t high_speed;         /* J12 pin 7: low=high-speed, high=normal */
  uint8_t variant;                 /* 0=RPi/USB, 1=WIFI, 2=Ethernet */
} picfw_pic16f15356_straps_t;

void picfw_pic16f15356_hal_reset(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_runtime_init(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_hal_set_uart_mode(picfw_pic16f15356_hal_t *hal, picfw_pic16f15356_uart_mode_t mode);
uint16_t picfw_pic16f15356_hal_current_spbrg(const picfw_pic16f15356_hal_t *hal);
picfw_bool_t picfw_pic16f15356_isr_latch_host_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte);
picfw_bool_t picfw_pic16f15356_isr_latch_bus_rx(picfw_pic16f15356_hal_t *hal, uint8_t byte);
void picfw_pic16f15356_isr_latch_tmr0(picfw_pic16f15356_hal_t *hal);
picfw_bool_t picfw_pic16f15356_mainline_service(picfw_pic16f15356_hal_t *hal, picfw_runtime_t *runtime);
size_t picfw_pic16f15356_hal_drain_host_tx(picfw_pic16f15356_hal_t *hal, uint8_t *out, size_t out_cap);

/* GPIO pin read/write (simulation model) */
picfw_bool_t picfw_pic16f15356_hal_read_pin(const picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit);
void picfw_pic16f15356_hal_write_pin(picfw_pic16f15356_hal_t *hal, uint8_t port, uint8_t bit, picfw_bool_t value);

/* Signal-detect: reads RB1/INT (eBUS transceiver presence) */
picfw_bool_t picfw_pic16f15356_hal_signal_detect(const picfw_pic16f15356_hal_t *hal);

/* PPS configuration: routes EUSART1/2 to physical pins per schematic */
void picfw_pic16f15356_hal_configure_pps(picfw_pic16f15356_hal_t *hal);

/* J12 AUX strap read: decodes protocol/variant/speed from GPIO inputs */
void picfw_pic16f15356_hal_read_straps(const picfw_pic16f15356_hal_t *hal,
                                       picfw_pic16f15356_straps_t *straps);

/* TX ISR handlers (EUSART1/2 TX register empty) */
void picfw_pic16f15356_isr_latch_host_tx_ready(picfw_pic16f15356_hal_t *hal);
void picfw_pic16f15356_isr_latch_bus_tx_ready(picfw_pic16f15356_hal_t *hal);

#endif
