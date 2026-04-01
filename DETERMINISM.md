# Determinism Rules -- eBUS Adapter PIC Firmware

This firmware must behave identically to a hardware UART peripheral. Every code path has bounded, predictable execution time. These rules are enforced automatically by CI and pre-commit hooks.

## Why Determinism Matters

eBUS runs at 2400 baud (417 us per bit). Master-master arbitration is bit-level: if your firmware's response jitters by more than ~10 us, you lose arbitration and corrupt the bus. There is no retry mechanism -- a missed arbitration window means dropped frames and unhappy heating systems.

## Source Layout

The checks scan these directories:
- `runtime/src/*.c` and `runtime/include/picfw/*.h` -- runtime C source
- `bootloader/src/*.c` and `bootloader/include/picboot/*.h` -- bootloader C source

## Rules

| ID | Rule | Automated | How Checked |
|----|------|-----------|-------------|
| R1 | No recursion (direct or mutual) | Yes | Call graph cycle detection |
| R2 | No malloc/calloc/realloc/free | Yes | Pattern scan |
| R3 | All loops bounded by constant or decrementing guard | Yes | AST pattern analysis |
| R4 | ISR-context WCET < 60 cycles | Yes | Naming pattern + source heuristic |
| R5 | No __delay in critical paths | Skipped* | Context-aware scan |
| R6 | No float/double/math.h | Yes | Pattern scan |
| R7 | No variable-length arrays | Yes | Pattern scan (with R2) |
| R8 | Cyclomatic complexity < 10 per function | Yes | Decision point counting |
| R9 | Hardware timers for temporal decisions | Manual | Code review |
| R10 | Ring buffers power-of-2, bitmask indexing | Yes | Buffer size + modulo scan |
| STACK | Call depth < 14 (16-level HW stack) | Yes | Call graph DFS |
| GUARD | Header include guards | Yes | Pattern scan |
| RAM | Static struct footprint < 75% of 2KB | Yes | Host sizeof budget check |
| WCET | ISR-context functions < 60 cycles | Yes | Source heuristic (naming pattern) |
| CONST | Function pointer arrays must be const | Yes | Qualifier scan |

*R5 blocking delay check is available as an optional make target but skipped in `check-all` because the firmware uses a HAL simulation model without `__delay_ms`. R4 ISR constraints are now covered by `check-wcet-isr` using naming patterns (`*_isr_*`) instead of `__interrupt()`.

## Running Checks Locally

```bash
# Run all enabled checks
make check-all

# Run individual checks
make check-recursion
make check-malloc
make check-loops
make check-float
make check-complexity
make check-stack-depth
make check-buffers
make check-guards
make check-ram-budget
make check-wcet-isr
make check-const-dispatch

# Optional checks (XC8/hardware-specific, not in check-all)
make check-isr
make check-delays
make check-wcet

# Validate the checks themselves work
bash tests/test_checks.sh

# Install git hooks
make install-hooks
```

## Adding New Code

1. Write code following patterns in existing source files
2. Run `make check-all` -- must pass with zero violations
3. If adding a new state to the FSM, document the eBUS spec section
4. Commit -- pre-commit hook runs checks automatically
5. Push -- CI runs the same checks on GitHub

## Exceptions

The **only** permitted exception to R3 (bounded loops) is the single `while(1)` main superloop in `main()`.

### R8: Flat State Machine Architecture
The eBUS protocol FSM must be implemented as a flat `switch/case` on an enum state variable. Maximum nesting depth: 2 levels (switch inside switch).

**Prohibited patterns:**
- Mutable function pointer dispatch (runtime-assigned callbacks)
- Coroutine-like patterns, setjmp/longjmp
- Callback chains where the next handler is determined at runtime

**Permitted patterns:**
- `const` dispatch tables with validated index -- these provide O(1) deterministic dispatch with constant WCET, superior to large switch/case cascades
- Static `const` function pointer arrays (placed in ROM by the compiler; the `const` qualifier prevents mutation)

No other exceptions exist. If a rule seems too restrictive for your use case, the code design needs to change -- not the rule.
