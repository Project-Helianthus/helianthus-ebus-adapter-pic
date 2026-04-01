#!/usr/bin/env python3
"""
check_no_float.py — Detect floating-point usage in C source files.

Catches: float/double declarations, FP literals, math.h functions.
Exits non-zero if any found.

Usage: python3 scripts/check_no_float.py src/
"""

import sys
import os
import re


FLOAT_PATTERNS = [
    (r'\bfloat\b', 'float type declaration'),
    (r'\bdouble\b', 'double type declaration'),
    (r'#\s*include\s*<\s*math\.h\s*>', 'math.h included'),
    (r'\bsin\s*\(', 'sin() — use lookup table'),
    (r'\bcos\s*\(', 'cos() — use lookup table'),
    (r'\btan\s*\(', 'tan() — use lookup table'),
    (r'\bsqrt\s*\(', 'sqrt() — use integer approximation'),
    (r'\bpow\s*\(', 'pow() — use integer shift/multiply'),
    (r'\blog\s*\(', 'log() — floating point'),
    (r'\blog10\s*\(', 'log10() — floating point'),
    (r'\bexp\s*\(', 'exp() — floating point'),
    (r'\bceil\s*\(', 'ceil() — floating point'),
    (r'\bfloor\s*\(', 'floor() — floating point'),
    (r'\bfabs\s*\(', 'fabs() — floating point'),
    (r'\batof\s*\(', 'atof() — string to float conversion'),
    (r'\bstrtof\s*\(', 'strtof() — string to float conversion'),
    (r'\bstrtod\s*\(', 'strtod() — string to double conversion'),
]

# FP literal: 1.0, 3.14, 1e5, .5f, etc. (but not inside version strings or comments)
FP_LITERAL = re.compile(r'(?<!["\w])(\d+\.\d+[fFlL]?|\d+[eE][+-]?\d+[fFlL]?|\.\d+[fFlL]?)(?!["\w])')


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def check_file(filepath):
    errors = []
    with open(filepath, 'r', errors='replace') as f:
        lines = f.readlines()

    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()

        # Skip full-line comments and NOLINT-suppressed lines
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        if 'NOLINT(determinism)' in line:
            continue

        # Strip inline comments before analysis
        code_only = re.sub(r'/\*.*?\*/', '', stripped)   # /* ... */
        code_only = re.sub(r'//.*$', '', code_only)      # // ...
        code_only = code_only.strip()
        if not code_only:
            continue

        # Skip preprocessor version macros
        if code_only.startswith('#') and ('version' in code_only.lower() or 'VERSION' in code_only):
            continue

        for pattern, desc in FLOAT_PATTERNS:
            if re.search(pattern, code_only):
                errors.append((filepath, line_num, desc, stripped))

        # Check for FP literals
        for match in FP_LITERAL.finditer(code_only):
            literal = match.group(1)
            errors.append((filepath, line_num,
                            f'Floating-point literal: {literal}', stripped))

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: check_no_float.py <source_dir>")
        sys.exit(2)

    all_errors = []
    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            continue
        for filepath in find_c_files(src_dir):
            all_errors.extend(check_file(filepath))

    if all_errors:
        for filepath, line_num, desc, context in all_errors:
            print(f"FAIL [R6] {filepath}:{line_num}: {desc}")
            print(f"     | {context.strip()}")
        print(f"\n{len(all_errors)} floating-point violation(s) found.")
        sys.exit(1)
    else:
        print("PASS [R6] No floating-point usage detected.")
        sys.exit(0)


if __name__ == '__main__':
    main()
