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

/* EUSART2 registers — host UART (same control bits as EUSART1) */
#define PICFW_PIC16F15356_APP_EUSART2_BAUD2CON_INIT 0x08u
#define PICFW_PIC16F15356_APP_EUSART2_RC2STA_INIT 0x90u
#define PICFW_PIC16F15356_APP_EUSART2_TX2STA_INIT 0x24u

/* --- GPIO pin definitions (from eBUS Adapter v3 schematic, IC5) --- */

/* Port identifiers for read_pin/write_pin */
#define PICFW_PORT_A 0u
#define PICFW_PORT_B 1u
#define PICFW_PORT_C 2u

/* LED2 (Info Blue): RC6, output via R7 1k resistor */
#define PICFW_PIN_LED2_PORT PICFW_PORT_C
#define PICFW_PIN_LED2_BIT  6u

/* J11 ICSP bootloader entry: RB6 (PGC) + RB7 (PGD) shorted at POR */
#define PICFW_PIN_J11_PGC_PORT PICFW_PORT_B
#define PICFW_PIN_J11_PGC_BIT  6u
#define PICFW_PIN_J11_PGD_PORT PICFW_PORT_B
#define PICFW_PIN_J11_PGD_BIT  7u

/* WiFi-check: RB0 (J12 Pin 4, Wemos D1 mini readiness signal) */
#define PICFW_PIN_WIFI_CHECK_PORT PICFW_PORT_B
#define PICFW_PIN_WIFI_CHECK_BIT  0u

/* Signal-detect: RB1/INT (external interrupt, eBUS transceiver presence) */
#define PICFW_PIN_SIGNAL_DETECT_PORT PICFW_PORT_B
#define PICFW_PIN_SIGNAL_DETECT_BIT  1u

/* EUSART1: bus UART (eBUS transceiver) */
#define PICFW_PIN_EUSART1_RX_PORT PICFW_PORT_B
#define PICFW_PIN_EUSART1_RX_BIT  2u   /* RB2/RX1 */
#define PICFW_PIN_EUSART1_TX_PORT PICFW_PORT_B
#define PICFW_PIN_EUSART1_TX_BIT  3u   /* RB3/TX1 */

/* EUSART2: host UART (ESP) */
#define PICFW_PIN_EUSART2_RX_PORT PICFW_PORT_C
#define PICFW_PIN_EUSART2_RX_BIT  0u   /* RC0/RX2 */
#define PICFW_PIN_EUSART2_TX_PORT PICFW_PORT_C
#define PICFW_PIN_EUSART2_TX_BIT  1u   /* RC1/TX2 */

/* SPI1: W5500 Ethernet module */
#define PICFW_PIN_SPI1_SCK_PORT  PICFW_PORT_B
#define PICFW_PIN_SPI1_SCK_BIT   4u   /* RB4/SCK1 */
#define PICFW_PIN_SPI1_SEL_PORT  PICFW_PORT_B
#define PICFW_PIN_SPI1_SEL_BIT   5u   /* RB5/SEL1 */
#define PICFW_PIN_SPI1_CLK_PORT  PICFW_PORT_B
#define PICFW_PIN_SPI1_CLK_BIT   6u   /* RB6/SPCLK */
#define PICFW_PIN_SPI1_DAT_PORT  PICFW_PORT_B
#define PICFW_PIN_SPI1_DAT_BIT   7u   /* RB7/SPDAT */

/* SPI2: secondary SPI bus */
#define PICFW_PIN_SPI2_SCK_PORT  PICFW_PORT_C
#define PICFW_PIN_SPI2_SCK_BIT   2u   /* RC2/SCK2 */
#define PICFW_PIN_SPI2_SDI_PORT  PICFW_PORT_C
#define PICFW_PIN_SPI2_SDI_BIT   3u   /* RC3/SDI2 */
#define PICFW_PIN_SPI2_SDO_PORT  PICFW_PORT_C
#define PICFW_PIN_SPI2_SDO_BIT   4u   /* RC4/SDO2 */
#define PICFW_PIN_SPI2_SEL_PORT  PICFW_PORT_C
#define PICFW_PIN_SPI2_SEL_BIT   5u   /* RC5/SEL2 */

/* J12 AUX variant identifiers (decoded from RA0/RA1 strap pins) */
#define PICFW_VARIANT_RPI_USB  0u
#define PICFW_VARIANT_WIFI     1u
#define PICFW_VARIANT_ETHERNET 2u

/* PPS routing values (PIC16F15356 datasheet DS40001866 Table 15-2) */
#define PICFW_PPS_EUSART1_TX 0x0Fu  /* RxyPPS value for TX1/CK1 output */
#define PICFW_PPS_EUSART2_TX 0x11u  /* RxyPPS value for TX2/CK2 output */
#define PICFW_PPS_RB2_INPUT  0x0Au  /* RX1PPS value for RB2 input */
#define PICFW_PPS_RC0_INPUT  0x10u  /* RX2PPS value for RC0 input */

/* TRIS defaults (0=output, 1=input; derived from schematic pin functions) */
#define PICFW_PIC16F15356_TRISA_INIT 0x37u  /* RA0-2,4,5=in; RA3=out(C1OUT) */
#define PICFW_PIC16F15356_TRISB_INIT 0x07u  /* RB0-2=in; RB3-7=out */
#define PICFW_PIC16F15356_TRISC_INIT 0x09u  /* RC0,3=in; RC1,2,4,5,6,7=out */

/* ANSEL defaults (0=digital, 1=analog) — all digital except RA2 (comparator) */
#define PICFW_PIC16F15356_ANSELA_INIT 0x04u  /* RA2=analog (C1IN0+) */
#define PICFW_PIC16F15356_ANSELB_INIT 0x00u  /* all digital */
#define PICFW_PIC16F15356_ANSELC_INIT 0x00u  /* all digital */

/* Weak pull-up on RB1 (signal detect — active-low input needs pull-up) */
#define PICFW_PIC16F15356_WPUB_INIT 0x02u    /* WPU on RB1 only */

/* J12 AUX strap pin mapping (active-low: open=high via pull-up, GND=low) */
#define PICFW_STRAP_PROTOCOL_PORT PICFW_PORT_A
#define PICFW_STRAP_PROTOCOL_BIT  4u   /* J12 pin 2: open=enhanced, GND=standard */
#define PICFW_STRAP_SPEED_PORT    PICFW_PORT_A
#define PICFW_STRAP_SPEED_BIT     5u   /* J12 pin 7: open=normal, GND=high-speed */
#define PICFW_STRAP_VARIANT_PORT  PICFW_PORT_A
#define PICFW_STRAP_VARIANT_BIT   0u   /* J12 pin 5: decoded via A0/A1 */
#define PICFW_STRAP_VARIANT2_PORT PICFW_PORT_A
#define PICFW_STRAP_VARIANT2_BIT  1u   /* J12 pin 5 secondary (variant decode) */

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
