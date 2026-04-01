/* ============================================================================
 * check_ram_budget.c — Verify static struct sizes fit in PIC16F15356 RAM
 *
 * Compiled with the HOST compiler and run as a CI check.  Host sizeof
 * differs from XC8 (alignment, pointer width) but tracks RELATIVE growth:
 * if a struct grows 32 bytes on the host build, it grew similarly on PIC.
 * This catches regressions even though absolute numbers differ.
 *
 * Usage: cc -std=c11 -Iruntime/include tools/check_ram_budget.c -o build/check_ram_budget
 *        ./build/check_ram_budget
 * ============================================================================ */

#include "picfw/runtime.h"
#include "picfw/pic16f15356_hal.h"
#include <stdio.h>
#include <stdlib.h>

/* PIC16F15356 total RAM. */
#define PIC16F15356_RAM_BYTES 2048u
/* Budget: 75% of total RAM for static allocations.
 * Remaining 25% reserved for compiler-managed stack, temps, and locals. */
#define RAM_BUDGET_PERCENT 75u
#define RAM_BUDGET_BYTES ((PIC16F15356_RAM_BYTES * RAM_BUDGET_PERCENT) / 100u)

int main(void) {
    size_t runtime_sz = sizeof(picfw_runtime_t);
    size_t hal_sz = sizeof(picfw_pic16f15356_hal_t);
    size_t total = runtime_sz + hal_sz;

    printf("RAM budget report (host sizeof — relative tracking):\n");
    printf("  picfw_runtime_t:          %4zu bytes\n", runtime_sz);
    printf("  picfw_pic16f15356_hal_t:  %4zu bytes\n", hal_sz);
    printf("  ----------------------------------------\n");
    printf("  Total static footprint:   %4zu bytes\n", total);
    printf("  PIC16F15356 RAM budget:   %4u bytes (%u%% of %u)\n",
           (unsigned)RAM_BUDGET_BYTES, (unsigned)RAM_BUDGET_PERCENT,
           (unsigned)PIC16F15356_RAM_BYTES);
    printf("  Utilization:              %4.1f%%\n",
           (double)total / (double)RAM_BUDGET_BYTES * 100.0);

    if (total > RAM_BUDGET_BYTES) {
        printf("\nFAIL [RAM] Static footprint %zu exceeds budget %u bytes.\n",
               total, (unsigned)RAM_BUDGET_BYTES);
        printf("Reduce struct sizes or increase RAM_BUDGET_PERCENT if justified.\n");
        return 1;
    }

    printf("\nPASS [RAM] Static footprint within budget (%zu / %u bytes).\n",
           total, (unsigned)RAM_BUDGET_BYTES);
    return 0;
}
