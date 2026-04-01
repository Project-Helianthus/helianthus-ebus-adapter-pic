#!/bin/bash
# ============================================================================
# test_checks.sh — Validate that determinism check scripts work correctly
#
# Runs each check against:
#   1. runtime/src + bootloader/src  (good code) — expects PASS
#   2. tests/fixtures/               (bad code)  — expects FAIL
#
# Only tests the enabled checks: recursion, malloc, loops, float, complexity.
# ISR constraints, delays, and WCET are skipped (HAL simulation model).
#
# Usage: bash tests/test_checks.sh
# ============================================================================

set -u

PASS=0
FAIL=0
ERRORS=""

green()  { printf "\033[32m%s\033[0m\n" "$1"; }
red()    { printf "\033[31m%s\033[0m\n" "$1"; }
yellow() { printf "\033[33m%s\033[0m\n" "$1"; }

# Test that a check PASSES on good code
expect_pass() {
    local label="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        green "  PASS $label -> correctly PASSED on good code"
        PASS=$((PASS + 1))
    else
        red "  FAIL $label -> UNEXPECTEDLY FAILED on good code"
        "$@" 2>&1 | sed 's/^/     /'
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  - $label false-negative on good code"
    fi
}

# Test that a check FAILS on bad code
expect_fail() {
    local label="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        red "  FAIL $label -> UNEXPECTEDLY PASSED on bad code (missed violations!)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  - $label false-positive on bad code"
    else
        green "  PASS $label -> correctly FAILED on bad code"
        PASS=$((PASS + 1))
    fi
}

echo "=========================================="
echo "  Determinism Check Scripts -- Self-Test  "
echo "=========================================="
echo ""

# --- Test against GOOD code (runtime/src) ---
yellow "Testing against runtime/src (should all PASS):"
expect_pass "R1 No recursion"        python3 scripts/check_no_recursion.py runtime/src
expect_pass "R2 No malloc"           python3 scripts/check_no_malloc.py runtime/src
expect_pass "R3 Bounded loops"       python3 scripts/check_bounded_loops.py runtime/src
expect_pass "R6 No float"            python3 scripts/check_no_float.py runtime/src
expect_pass "R8 Complexity"          python3 scripts/check_complexity.py runtime/src --max=10
expect_pass "STACK Depth"            python3 scripts/check_stack_depth.py runtime/src runtime/include --max-depth=14
expect_pass "R10 Buffers"            python3 scripts/check_buffer_sizes.py runtime/src runtime/include
expect_pass "GUARD Headers"          python3 scripts/check_include_guards.py runtime/include
echo ""

# --- Test against GOOD code (bootloader/src) ---
yellow "Testing against bootloader/src (should all PASS):"
expect_pass "R1 No recursion"        python3 scripts/check_no_recursion.py bootloader/src
expect_pass "R2 No malloc"           python3 scripts/check_no_malloc.py bootloader/src
expect_pass "R3 Bounded loops"       python3 scripts/check_bounded_loops.py bootloader/src
expect_pass "R6 No float"            python3 scripts/check_no_float.py bootloader/src
expect_pass "R8 Complexity"          python3 scripts/check_complexity.py bootloader/src --max=10
expect_pass "STACK Depth"            python3 scripts/check_stack_depth.py bootloader/src bootloader/include --max-depth=14
expect_pass "R10 Buffers"            python3 scripts/check_buffer_sizes.py bootloader/src bootloader/include
expect_pass "GUARD Headers"          python3 scripts/check_include_guards.py bootloader/include
echo ""

# --- Test against BAD code (tests/fixtures/) ---
yellow "Testing against tests/fixtures/ (should all FAIL):"
expect_fail "R1 Recursion detected"  python3 scripts/check_no_recursion.py tests/fixtures/
expect_fail "R2 malloc detected"     python3 scripts/check_no_malloc.py tests/fixtures/
expect_fail "R3 Unbounded loops"     python3 scripts/check_bounded_loops.py tests/fixtures/
expect_fail "R6 Float detected"      python3 scripts/check_no_float.py tests/fixtures/
expect_fail "R8 High complexity"     python3 scripts/check_complexity.py tests/fixtures/ --max=10
expect_fail "STACK Deep chain"       python3 scripts/check_stack_depth.py tests/fixtures/ --max-depth=14
expect_fail "R10 Bad ring"           python3 scripts/check_buffer_sizes.py tests/fixtures/
expect_fail "GUARD No guard"         python3 scripts/check_include_guards.py tests/fixtures/
echo ""

# --- Summary ---
TOTAL=$((PASS + FAIL))
echo "=========================================="
if [ $FAIL -eq 0 ]; then
    echo "  All $TOTAL self-tests passed"
else
    echo "  $FAIL/$TOTAL self-tests failed"
fi
echo "=========================================="

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
exit 0
