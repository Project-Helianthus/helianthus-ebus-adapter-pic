#!/usr/bin/env python3
"""
check_buffer_sizes.py — R10 ring buffer audit.

Checks:
1. #define names containing _CAP, _SIZE, BUF_SIZE, BUFFER_SIZE, RING_SIZE,
   QUEUE_SIZE, FIFO_SIZE: ring buffers must be power-of-2.
2. Modulo (%) on buffer-size constants: must use bitmask (& (N-1)) instead.

Ring buffer classification: name contains FIFO, RING, QUEUE, or the constant
is used with % in index operations. Linear buffers get WARN only.

Usage: python3 scripts/check_buffer_sizes.py src/ [src2/ ...]
"""

import sys
import os
import re


BUFFER_NAME_PATTERN = re.compile(
    r'#\s*define\s+'
    r'(\w*(?:_CAP|_SIZE|BUF_SIZE|BUFFER_SIZE|RING_SIZE|QUEUE_SIZE|FIFO_SIZE)\w*)'
    r'\s+'
    r'([^\s/]+)',
    re.MULTILINE
)

RING_KEYWORDS = ('FIFO', 'RING', 'QUEUE')

MODULO_PATTERN = re.compile(r'%\s*(\w+)')


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def is_power_of_two(n):
    return n > 0 and (n & (n - 1)) == 0


def parse_numeric(value_str):
    """Parse numeric value from C define (handles 32u, 0x20, 32U, 32UL, (32))."""
    s = value_str.strip()
    s = s.strip('()')
    s = re.sub(r'[uUlL]+$', '', s)
    try:
        if s.startswith('0x') or s.startswith('0X'):
            return int(s, 16)
        return int(s)
    except ValueError:
        return None


def is_ring_buffer(name, modulo_constants):
    """Classify as ring buffer if name contains ring keywords or used with %."""
    upper = name.upper()
    for kw in RING_KEYWORDS:
        if kw in upper:
            return True
    return name in modulo_constants


def main():
    if len(sys.argv) < 2:
        print("Usage: check_buffer_sizes.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    # Pass 1: collect all buffer-size defines and modulo usage
    defines = {}  # name -> (value, filepath, line_num, line_text)
    modulo_usage = {}  # constant_name -> [(filepath, line_num, line_text)]
    modulo_constants = set()

    all_files = []
    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            continue
        all_files.extend(find_c_files(src_dir))

    for filepath in all_files:
        with open(filepath, 'r', errors='replace') as f:
            lines = f.readlines()

        content = ''.join(lines)

        # Find defines
        for match in BUFFER_NAME_PATTERN.finditer(content):
            name = match.group(1)
            raw_val = match.group(2)
            line_num = content[:match.start()].count('\n') + 1
            line_text = lines[line_num - 1].strip() if line_num <= len(lines) else ''
            if 'NOLINT(determinism)' in line_text:
                continue
            val = parse_numeric(raw_val)
            if val is not None:
                defines[name] = (val, filepath, line_num, line_text)

        # Find modulo usage
        for line_num, line in enumerate(lines, 1):
            stripped = line.strip()
            if 'NOLINT(determinism)' in stripped:
                continue
            if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue
            for mod_match in MODULO_PATTERN.finditer(stripped):
                const_name = mod_match.group(1)
                if const_name in defines or any(const_name == d for d in defines):
                    modulo_constants.add(const_name)
                    if const_name not in modulo_usage:
                        modulo_usage[const_name] = []
                    modulo_usage[const_name].append((filepath, line_num, stripped))

    # Pass 2: check sizes and modulo usage
    errors = []
    warnings = []

    for name, (val, filepath, line_num, line_text) in sorted(defines.items()):
        ring = is_ring_buffer(name, modulo_constants)
        if ring and not is_power_of_two(val):
            errors.append(
                f"FAIL [R10] {filepath}:{line_num}: {name}={val} "
                f"is a ring buffer but not power-of-2"
            )
        elif not ring and not is_power_of_two(val):
            warnings.append(
                f"WARN [R10] {filepath}:{line_num}: {name}={val} "
                f"is not power-of-2 (linear buffer — not blocking)"
            )

    for const_name, usages in sorted(modulo_usage.items()):
        for filepath, line_num, line_text in usages:
            errors.append(
                f"FAIL [R10] {filepath}:{line_num}: modulo '%' used with "
                f"{const_name} — use '& ({const_name} - 1)' instead"
            )
            print(f"     | {line_text}")

    # Output
    for w in warnings:
        print(w)
    for e in errors:
        print(e)

    if warnings:
        print()

    if errors:
        print(f"\n{len(errors)} R10 violation(s) found.")
        sys.exit(1)
    else:
        if warnings:
            print(f"\nPASS [R10] Ring buffers OK. {len(warnings)} non-blocking warning(s).")
        else:
            print("PASS [R10] All buffer sizes and indexing correct.")
        sys.exit(0)


if __name__ == '__main__':
    main()
