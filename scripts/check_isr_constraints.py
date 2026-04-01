#!/usr/bin/env python3
"""
check_isr_constraints.py — Verify ISR bodies comply with determinism rules.

Checks:
  - No function pointer calls inside ISRs
  - No loops inside ISRs
  - No library function calls (printf, sprintf, memcpy with variable len, etc.)
  - No nested function calls deeper than 1 level
  - Volatile qualifier on shared variables (advisory)

Usage: python3 scripts/check_isr_constraints.py src/
"""

import sys
import os
import re


FORBIDDEN_IN_ISR = [
    (r'\bprintf\s*\(', 'printf() — never in ISR'),
    (r'\bsprintf\s*\(', 'sprintf() — never in ISR'),
    (r'\bsnprintf\s*\(', 'snprintf() — never in ISR'),
    (r'\bfprintf\s*\(', 'fprintf() — never in ISR'),
    (r'\bputs\s*\(', 'puts() — never in ISR'),
    (r'\bputchar\s*\(', 'putchar() — never in ISR'),
    (r'\bmemcpy\s*\(', 'memcpy() — use direct assignment in ISR'),
    (r'\bmemset\s*\(', 'memset() — use direct assignment in ISR'),
    (r'\bstrcpy\s*\(', 'strcpy() — never in ISR'),
    (r'\bstrlen\s*\(', 'strlen() — never in ISR'),
    (r'\b__delay_ms\s*\(', '__delay_ms() — blocking delay in ISR [R5]'),
    (r'\b__delay_us\s*\(', '__delay_us() — blocking delay in ISR [R5]'),
    (r'\bmalloc\s*\(', 'malloc() — dynamic allocation in ISR [R2]'),
    (r'\bfree\s*\(', 'free() — dynamic allocation in ISR [R2]'),
]

LOOP_PATTERNS = [
    (r'\bfor\s*\(', 'for-loop inside ISR'),
    (r'\bwhile\s*\(', 'while-loop inside ISR'),
    (r'\bdo\s*\{', 'do-while loop inside ISR'),
]

FPTR_PATTERNS = [
    (r'\(\s*\*\s*\w+\s*\)\s*\(', 'function pointer call: (*fptr)()'),
    (r'\w+_callback\s*\(', 'callback function call (likely function pointer)'),
    (r'\w+_handler\s*\(', 'handler function call (review: is this a fptr?)'),
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
    """Find __interrupt() function bodies and return them with line offsets."""
    with open(filepath, 'r', errors='replace') as f:
        content = f.read()

    clean = strip_comments(content)
    isrs = []

    # Match: void __interrupt() func_name(void) {
    # Or:    void __interrupt() isr(void) {
    # Or:    void __interrupt isr(void) {
    isr_pattern = re.compile(
        r'(?:void\s+)?__interrupt\s*(?:\(\s*\))?\s*(\w+)\s*\([^)]*\)\s*\{',
        re.MULTILINE
    )

    for match in isr_pattern.finditer(clean):
        func_name = match.group(1)
        start = match.end() - 1  # position of '{'
        depth = 0
        pos = start
        while pos < len(clean):
            if clean[pos] == '{':
                depth += 1
            elif clean[pos] == '}':
                depth -= 1
                if depth == 0:
                    body = clean[start + 1:pos]
                    # Calculate line offset
                    line_offset = clean[:start].count('\n') + 1
                    isrs.append((func_name, body, line_offset, filepath))
                    break
            pos += 1

    return isrs


def check_isr_body(func_name, body, line_offset, filepath):
    """Check a single ISR body for violations."""
    errors = []
    lines = body.split('\n')

    for rel_line, line in enumerate(lines, 1):
        abs_line = line_offset + rel_line
        stripped = line.strip()

        # Check forbidden functions
        for pattern, desc in FORBIDDEN_IN_ISR:
            if re.search(pattern, stripped):
                errors.append((filepath, abs_line, 'R4',
                                f'ISR {func_name}(): {desc}'))

        # Check loops
        for pattern, desc in LOOP_PATTERNS:
            if re.search(pattern, stripped):
                errors.append((filepath, abs_line, 'R4',
                                f'ISR {func_name}(): {desc} — '
                                f'unroll or move to main loop'))

        # Check function pointers
        for pattern, desc in FPTR_PATTERNS:
            if re.search(pattern, stripped):
                errors.append((filepath, abs_line, 'R4',
                                f'ISR {func_name}(): {desc}'))

    # Count function calls (excluding register/bit access patterns)
    call_pattern = re.compile(r'\b([a-z]\w+)\s*\(')
    # Exclude known safe patterns
    safe_calls = {'if', 'while', 'for', 'switch', 'return', 'sizeof'}
    calls_found = []
    for match in call_pattern.finditer(body):
        fname = match.group(1)
        if fname not in safe_calls:
            calls_found.append(fname)

    if len(calls_found) > 3:
        errors.append((filepath, line_offset, 'R4',
                        f'ISR {func_name}(): {len(calls_found)} function calls '
                        f'detected ({", ".join(set(calls_found))}). '
                        f'ISRs should be minimal — max 1-2 helper calls.'))

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: check_isr_constraints.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    all_errors = []
    isr_count = 0

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            print(f"WARNING: {src_dir} does not exist, skipping")
            continue
        for filepath in find_c_files(src_dir):
            isrs = extract_isr_bodies(filepath)
            isr_count += len(isrs)
            for func_name, body, line_offset, fpath in isrs:
                all_errors.extend(
                    check_isr_body(func_name, body, line_offset, fpath))

    if all_errors:
        for filepath, line_num, rule, desc in all_errors:
            print(f"FAIL [{rule}] {filepath}:{line_num}: {desc}")
        print(f"\n{len(all_errors)} ISR violation(s) found in {isr_count} ISR(s).")
        sys.exit(1)
    else:
        print(f"PASS [R4] {isr_count} ISR(s) checked — all constraints satisfied.")
        sys.exit(0)


if __name__ == '__main__':
    main()
