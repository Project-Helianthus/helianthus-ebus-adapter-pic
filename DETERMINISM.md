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
| R4 | ISR constraints (no loops/fptrs/library calls) | Skipped* | ISR body analysis + WCET |
| R5 | No __delay in critical paths | Skipped* | Context-aware scan |
| R6 | No float/double/math.h | Yes | Pattern scan |
| R7 | No variable-length arrays | Yes | Pattern scan (with R2) |
| R8 | Cyclomatic complexity < 10 per function | Yes | Decision point counting |
| R9 | Hardware timers for temporal decisions | Manual | Code review |
| R10 | Ring buffers power-of-2, bitmask indexing | Manual | Code review |

*R4/R5 checks are available as optional make targets but skipped in `check-all` because the firmware uses a HAL simulation model, not `__interrupt()` or `__delay_ms`. These will be enabled when targeting real PIC hardware.

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
