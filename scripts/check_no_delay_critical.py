#!/usr/bin/env python3
"""
check_no_delay_critical.py — Detect __delay_ms/__delay_us in critical paths.

Delays are ONLY allowed in initialization code, marked with /* INIT-ONLY-DELAY */.
Any delay in ISR, FSM, TX/RX, or arbitration code is a blocking violation.

Usage: python3 scripts/check_no_delay_critical.py src/
"""

import sys
import os
import re


DELAY_PATTERN = re.compile(r'__delay_(?:ms|us)\s*\([^)]*\)')
INIT_MARKER = re.compile(r'/\*\s*INIT-ONLY-DELAY\s*\*/')

# Files / function names that are always critical (delays forbidden)
CRITICAL_FILE_PATTERNS = [
    'ebus_fsm', 'uart', 'arbitrat', 'timer', 'isr', 'interrupt'
]
CRITICAL_FUNC_PATTERNS = [
    'isr', 'interrupt', 'ebus_', 'uart_rx', 'uart_tx',
    'arbitrat', 'send_', 'recv_', 'process_frame'
]


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def is_critical_file(filepath):
    basename = os.path.basename(filepath).lower()
    return any(p in basename for p in CRITICAL_FILE_PATTERNS)


def check_file(filepath):
    errors = []
    with open(filepath, 'r', errors='replace') as f:
        lines = f.readlines()

    critical_file = is_critical_file(filepath)
    current_function = None

    for line_num, line in enumerate(lines, 1):
        # Track current function (rough heuristic)
        func_match = re.match(r'(?:[\w\*\s]+?)\s+(\w+)\s*\([^)]*\)\s*\{', line)
        if func_match:
            current_function = func_match.group(1).lower()

        # Look for delays
        delay_match = DELAY_PATTERN.search(line)
        if delay_match:
            delay_call = delay_match.group(0)

            # Check if it's marked as INIT-ONLY-DELAY
            # Look at current line and previous line for the marker
            context = ''
            if line_num > 1:
                context = lines[line_num - 2]
            context += line

            if INIT_MARKER.search(context):
                continue  # Properly marked, OK

            # In a critical file?
            if critical_file:
                errors.append((filepath, line_num, 'R5',
                                f'{delay_call} in critical file. '
                                f'Delays forbidden in protocol-critical code.'))
                continue

            # In a critical function?
            if current_function and any(p in current_function
                                         for p in CRITICAL_FUNC_PATTERNS):
                errors.append((filepath, line_num, 'R5',
                                f'{delay_call} in critical function '
                                f'{current_function}(). '
                                f'Use hardware timer instead.'))
                continue

            # Not marked and not obviously critical — warn
            errors.append((filepath, line_num, 'R5',
                            f'{delay_call} without /* INIT-ONLY-DELAY */ marker. '
                            f'Add marker if this is init-only, or remove.'))

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: check_no_delay_critical.py <source_dir>")
        sys.exit(2)

    all_errors = []

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            continue
        for filepath in find_c_files(src_dir):
            all_errors.extend(check_file(filepath))

    if all_errors:
        for filepath, line_num, rule, desc in all_errors:
            print(f"FAIL [{rule}] {filepath}:{line_num}: {desc}")
        print(f"\n{len(all_errors)} blocking delay violation(s) found.")
        sys.exit(1)
    else:
        print("PASS [R5] No blocking delays in critical paths.")
        sys.exit(0)


if __name__ == '__main__':
    main()
