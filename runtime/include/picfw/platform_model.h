#ifndef PICFW_PLATFORM_MODEL_H
#define PICFW_PLATFORM_MODEL_H

#include "common.h"

/*
 * Recovered from the legacy combined.hex image plus Microchip PIC16F15356
 * register documentation. These values model the observed application runtime
 * clock/timer/UART layout; they are not yet hardware-measured on a live
 * adapter. The EUSART values below describe the application image, not the
 * separate bootloader transfer modes used by ebuspicloader.
 */

#define PICFW_PIC16F15356_RESET_FOSC_HZ 1000000u
#define PICFW_PIC16F15356_RUN_FOSC_HZ 32000000u
#define PICFW_PIC16F15356_INSTRUCTION_HZ (PICFW_PIC16F15356_RUN_FOSC_HZ / 4u)
#define PICFW_PIC16F15356_APP_OSCCON1_RUNTIME_INIT 0x60u
#define PICFW_PIC16F15356_APP_OSCFRQ_RUNTIME_INIT 0x06u

#define PICFW_PIC16F15356_TMR0_T0CON1_INIT 0x44u
#define PICFW_PIC16F15356_TMR0_T0CON0_INIT 0x80u
#define PICFW_PIC16F15356_TMR0_PERIOD_REG 0xF9u
#define PICFW_PIC16F15356_TMR0_PRESCALER 16u
#define PICFW_PIC16F15356_TMR0_COUNTS_PER_ISR ((uint16_t)(PICFW_PIC16F15356_TMR0_PERIOD_REG + 1u))
#define PICFW_PIC16F15356_TMR0_ISR_DIVIDER 200u

#define PICFW_PIC16F15356_APP_EUSART_BAUD1CON_INIT 0x08u
#define PICFW_PIC16F15356_APP_EUSART_RC1STA_INIT 0x90u
#define PICFW_PIC16F15356_APP_EUSART_TX1STA_INIT 0x24u
#define PICFW_PIC16F15356_APP_EUSART_DEFAULT_SPBRG 0x0340u
#define PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_SPBRG 0x0044u
#define PICFW_PIC16F15356_APP_EUSART_DEFAULT_BAUD_NOMINAL 9600u
#define PICFW_PIC16F15356_APP_EUSART_HIGH_SPEED_BAUD_NOMINAL 115200u

static inline uint32_t picfw_pic16f15356_tmr0_isr_period_us(void) {
  return (uint32_t)(((uint64_t)PICFW_PIC16F15356_TMR0_PRESCALER *
                     (uint64_t)PICFW_PIC16F15356_TMR0_COUNTS_PER_ISR *
                     1000000ull) /
                    (uint64_t)PICFW_PIC16F15356_INSTRUCTION_HZ);
}

static inline uint32_t picfw_pic16f15356_scheduler_period_us(void) {
  return picfw_pic16f15356_tmr0_isr_period_us() * (uint32_t)PICFW_PIC16F15356_TMR0_ISR_DIVIDER;
}

static inline uint32_t picfw_pic16f15356_scheduler_period_ms(void) {
  return picfw_pic16f15356_scheduler_period_us() / 1000u;
}

static inline uint32_t picfw_pic16f15356_app_eusart_async_baud(uint16_t spbrg) {
  uint32_t denominator = 4u * ((uint32_t)spbrg + 1u);
  return (PICFW_PIC16F15356_RUN_FOSC_HZ + (denominator / 2u)) / denominator;
}

static inline picfw_bool_t picfw_within_percent(uint32_t actual, uint32_t target, uint32_t percent) {
  uint32_t tolerance = (target * percent) / 100u;
  return (picfw_bool_t)(actual + tolerance >= target && actual <= target + tolerance);
}

#endif
