#!/usr/bin/env python3
"""
check_include_guards.py — Verify header include guards.

Each .h file must have either:
  - #pragma once
  - Matching #ifndef / #define pair in the first 10 non-empty, non-comment lines

Exits non-zero if any header lacks a guard.

Usage: python3 scripts/check_include_guards.py src/ [src2/ ...]
"""

import sys
import os
import re


def find_h_files(directory):
    h_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith('.h'):
                h_files.append(os.path.join(root, f))
    return h_files


def has_include_guard(filepath):
    """Check for #pragma once or #ifndef/#define pair in first 10 meaningful lines."""
    with open(filepath, 'r', errors='replace') as f:
        lines = f.readlines()

    meaningful = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        if 'NOLINT(determinism)' in stripped:
            continue
        meaningful.append(stripped)
        if len(meaningful) >= 10:
            break

    # Check for #pragma once
    for line in meaningful:
        if re.match(r'#\s*pragma\s+once', line):
            return True

    # Check for #ifndef / #define pair
    ifndef_name = None
    for line in meaningful:
        if ifndef_name is None:
            m = re.match(r'#\s*ifndef\s+(\w+)', line)
            if m:
                ifndef_name = m.group(1)
        else:
            m = re.match(r'#\s*define\s+(\w+)', line)
            if m and m.group(1) == ifndef_name:
                return True

    return False


def main():
    if len(sys.argv) < 2:
        print("Usage: check_include_guards.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    errors = []

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            continue
        for filepath in find_h_files(src_dir):
            if not has_include_guard(filepath):
                errors.append(filepath)

    if errors:
        for filepath in errors:
            print(f"FAIL [GUARD] {filepath}: missing include guard "
                  f"(#pragma once or #ifndef/#define)")
        print(f"\n{len(errors)} header(s) missing include guards.")
        sys.exit(1)
    else:
        print("PASS [GUARD] All headers have include guards.")
        sys.exit(0)


if __name__ == '__main__':
    main()
