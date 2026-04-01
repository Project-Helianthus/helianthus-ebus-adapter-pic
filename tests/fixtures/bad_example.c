/* ============================================================================
 * bad_example.c — DELIBERATELY VIOLATES ALL DETERMINISM RULES
 * This file is a test fixture. It should NEVER pass checks.
 * Copy to src/ temporarily to verify that checks catch violations.
 * ============================================================================ */

#include <xc.h>
#include <stdint.h>
#include <stdlib.h>     /* R2: stdlib included */
#include <math.h>       /* R6: math.h included */

/* R1: Direct recursion */
uint16_t factorial(uint16_t n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);    /* VIOLATION: direct recursion */
}

/* R1: Mutual recursion */
void func_a(uint8_t n);
void func_b(uint8_t n) {
    if (n > 0) func_a(n - 1);      /* VIOLATION: mutual recursion */
}
void func_a(uint8_t n) {
    if (n > 0) func_b(n - 1);      /* VIOLATION: mutual recursion */
}

/* R2: Dynamic allocation */
void process_data(uint8_t len) {
    uint8_t *buf = (uint8_t *)malloc(len);  /* VIOLATION: malloc */
    if (buf) {
        buf[0] = 0x42;
        free(buf);                           /* VIOLATION: free */
    }
}

/* R3: Unbounded loop */
void wait_for_uart(void) {
    while (PIR1bits.RCIF);    /* VIOLATION: unbounded busy-wait */
}

/* R4: ISR with loops, library calls, function pointers */
typedef void (*callback_t)(uint8_t);
static callback_t rx_callback;

void __interrupt() bad_isr(void) {
    if (PIR1bits.RCIF) {
        uint8_t byte = RCREG;

        /* VIOLATION: loop in ISR */
        for (uint8_t i = 0; i < 10; i++) {
            /* processing... */
        }

        /* VIOLATION: printf in ISR */
        printf("RX: %02X\n", byte);

        /* VIOLATION: memcpy in ISR */
        uint8_t tmp[4];
        memcpy(tmp, &byte, 1);

        /* VIOLATION: function pointer in ISR */
        if (rx_callback) {
            rx_callback(byte);
        }

        /* VIOLATION: __delay_ms in ISR */
        __delay_ms(1);
    }
}

/* R5: Blocking delay in critical function */
void ebus_send_byte(uint8_t byte) {
    TXREG = byte;
    __delay_us(417);              /* VIOLATION: delay in TX path */
    while (!PIR1bits.TXIF);       /* VIOLATION: unbounded wait */
}

/* R6: Floating point */
float calculate_temperature(uint16_t adc_value) {
    float voltage = adc_value * 3.3 / 1024.0;   /* VIOLATION: float */
    return voltage * 100.0;                       /* VIOLATION: FP literal */
}

/* R7: Variable-length array */
void process_frame(uint8_t len) {
    uint8_t frame[len];           /* VIOLATION: VLA */
    frame[0] = 0;
}

/* R8: Overly complex function (high cyclomatic complexity) */
void overcomplicated_handler(uint8_t cmd, uint8_t sub, uint8_t flag) {
    if (cmd == 0x01) {
        if (sub == 0x01) {
            if (flag & 0x01) {
                if (flag & 0x02) {
                    if (flag & 0x04) {
                        /* deeply nested */
                    } else if (flag & 0x08) {
                        /* more branches */
                    } else if (flag & 0x10) {
                        /* even more */
                    }
                } else if (flag & 0x20) {
                    if (flag & 0x40) {
                        /* nesting continues */
                    }
                }
            }
        } else if (sub == 0x02) {
            if (flag & 0x01 && flag & 0x02) {
                /* complex condition */
            } else if (flag & 0x04 || flag & 0x08) {
                /* more complex */
            }
        } else if (sub == 0x03) {
            switch (flag) {
                case 0: break;
                case 1: break;
                case 2: break;
                case 3: break;
                case 4: break;
                default: break;
            }
        }
    } else if (cmd == 0x02) {
        if (sub > 0x10 && sub < 0x20) {
            /* another branch */
        } else if (sub > 0x20 || sub == 0) {
            /* yet another */
        }
    } else if (cmd == 0x03) {
        if (flag & 0x01) {
            /* R8 padding */
        } else if (flag & 0x02) {
            /* R8 padding */
        } else if (flag & 0x04) {
            /* R8 padding */
        } else if (flag & 0x08) {
            /* R8 padding */
        } else if (flag & 0x10) {
            /* R8 padding */
        }
    }
}
