#!/usr/bin/env python3
"""
wcet_estimate.py — Estimate worst-case execution time of ISR functions.

Since we can't always run XC8 in CI, this provides a source-level heuristic.
It counts "cost units" for operations in ISR bodies and flags functions
that exceed the budget.

For accurate WCET, use XC8's ASM output (--asmlist) and count instructions.
This script provides an early warning system.

Cost model (PIC16 instruction cycles, approximate):
  - Simple assignment/read:  1-2 cycles
  - Bit test/set:            1 cycle
  - Array index (constant):  2-3 cycles
  - Array index (variable):  3-5 cycles
  - Function call overhead:  4 cycles (call + return)
  - Comparison:              1-2 cycles
  - if/else branch:          2-3 cycles
  - switch/case:             2+ cycles per case

Usage: python3 scripts/wcet_estimate.py src/ [--max-isr-cycles=50]
"""

import sys
import os
import re


DEFAULT_MAX_CYCLES = 60

# Cost heuristics per source construct (in PIC16 instruction cycles)
# Ordered most-specific first — FIRST MATCH WINS per line
COST_MAP = [
    # VIOLATIONS — these should blow the budget in ISRs
    (r'\bfor\s*\(', 100),                                # loop in ISR = budget explosion
    (r'\bwhile\s*\(', 100),                              # loop in ISR = budget explosion
    (r'\bdo\s*\{', 100),                                 # loop in ISR = budget explosion
    (r'\bprintf\s*\(', 200),                             # library call = massive cost
    (r'\bsprintf\s*\(', 200),                            # library call
    (r'\bmemcpy\s*\(', 50),                              # library call
    (r'\bmemset\s*\(', 50),                              # library call
    (r'\b__delay_ms\s*\(', 10000),                       # blocking delay = infinite cost
    (r'\b__delay_us\s*\(', 500),                         # blocking delay
    # Compound patterns (check before simple ones)
    (r'\w+\[.+\]\s*=\s*.+;', 4),                        # array write
    (r'\w+\s*=\s*\w+\[.+\]\s*;', 4),                    # array read
    (r'\w+bits\.\w+\s*=\s*\d+', 2),                     # bit-field set: PORTbits.X = 0
    (r'if\s*\(.+&&.+\)', 4),                             # compound condition with &&
    (r'if\s*\(.+\|\|.+\)', 4),                           # compound condition with ||
    (r'if\s*\(', 3),                                      # simple branch
    (r'else\s*\{', 1),                                    # else path
    (r'\w+bits\.\w+', 1),                                # bit-field read
    (r'\w+\s*=\s*\w+\s*;', 2),                           # simple assignment
    (r'\w+\+\+', 1),                                      # increment
    (r'\w+--', 1),                                        # decrement
    (r'\w+\s*\(\s*\)', 4),                                # function call
    (r'return', 2),                                       # return
]


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def strip_comments(content):
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    return content


def extract_isr_bodies(filepath):
    with open(filepath, 'r', errors='replace') as f:
        content = f.read()

    clean = strip_comments(content)
    isrs = []

    isr_pattern = re.compile(
        r'(?:void\s+)?__interrupt\s*(?:\(\s*\))?\s*(\w+)\s*\([^)]*\)\s*\{',
        re.MULTILINE
    )

    for match in isr_pattern.finditer(clean):
        func_name = match.group(1)
        start = match.end() - 1
        depth = 0
        pos = start
        while pos < len(clean):
            if clean[pos] == '{':
                depth += 1
            elif clean[pos] == '}':
                depth -= 1
                if depth == 0:
                    body = clean[start + 1:pos]
                    line_num = clean[:start].count('\n') + 1
                    isrs.append((func_name, body, line_num, filepath))
                    break
            pos += 1

    return isrs


def estimate_wcet(body):
    """Estimate worst-case cycle count from source constructs."""
    total = 4  # ISR entry overhead (context save on PIC16)
    breakdown = []

    lines = body.strip().split('\n')
    for line in lines:
        line = line.strip()
        if not line or line == '{' or line == '}':
            continue

        line_cost = 0
        # First-match-wins: take cost from the most specific matching pattern
        for pattern, cost in COST_MAP:
            if re.search(pattern, line):
                line_cost = cost
                break  # first match wins — patterns ordered most-specific first

        if line_cost == 0 and line not in ('{', '}', ''):
            line_cost = 2  # default cost for unrecognized statements

        total += line_cost
        if line_cost > 0:
            breakdown.append((line_cost, line))

    total += 4  # ISR exit overhead (context restore)
    return total, breakdown


def main():
    max_cycles = DEFAULT_MAX_CYCLES
    source_dirs = []

    for arg in sys.argv[1:]:
        if arg.startswith('--max-isr-cycles='):
            max_cycles = int(arg.split('=')[1])
        else:
            source_dirs.append(arg)

    if not source_dirs:
        print(f"Usage: wcet_estimate.py <source_dir> [--max-isr-cycles={DEFAULT_MAX_CYCLES}]")
        sys.exit(2)

    errors = []
    isr_count = 0

    for src_dir in source_dirs:
        if not os.path.exists(src_dir):
            continue
        for filepath in find_c_files(src_dir):
            for func_name, body, line_num, fpath in extract_isr_bodies(filepath):
                isr_count += 1
                estimated, breakdown = estimate_wcet(body)

                status = "PASS" if estimated <= max_cycles else "FAIL"
                print(f"\n{status} ISR {func_name}() @ {os.path.basename(fpath)}:{line_num}")
                print(f"  Estimated WCET: ~{estimated} cycles "
                      f"(budget: {max_cycles} cycles)")

                if estimated > max_cycles or '-v' in sys.argv:
                    print("  Breakdown:")
                    for cost, line in breakdown[:15]:
                        print(f"    +{cost:>3} cyc | {line[:60]}")
                    if len(breakdown) > 15:
                        print(f"    ... and {len(breakdown) - 15} more lines")

                if estimated > max_cycles:
                    errors.append((fpath, func_name, line_num, estimated))

    print()
    if errors:
        for fpath, func_name, line_num, est in errors:
            print(f"FAIL [R4] {fpath}:{line_num}: ISR {func_name}() "
                  f"estimated ~{est} cycles (budget: {max_cycles})")
        print(f"\n{len(errors)} ISR(s) exceed cycle budget.")
        print("NOTE: These are source-level estimates. For exact counts,")
        print("      compile with XC8 --asmlist and count instructions.")
        sys.exit(1)
    else:
        if isr_count > 0:
            print(f"PASS [R4] {isr_count} ISR(s) within cycle budget ({max_cycles}).")
        else:
            print("INFO: No ISR functions found (no __interrupt pattern detected).")
        sys.exit(0)


if __name__ == '__main__':
    main()
