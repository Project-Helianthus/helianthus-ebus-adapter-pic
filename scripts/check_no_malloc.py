#!/usr/bin/env python3
"""
check_no_malloc.py — Detect dynamic memory allocation and VLAs in C sources.

Catches: malloc, calloc, realloc, free, alloca, and variable-length arrays.
Exits non-zero if any found.

Usage: python3 scripts/check_no_malloc.py src/
"""

import sys
import os
import re


FORBIDDEN_PATTERNS = [
    # (pattern, description, rule)
    (r'\bmalloc\s*\(', 'malloc() call', 'R2'),
    (r'\bcalloc\s*\(', 'calloc() call', 'R2'),
    (r'\brealloc\s*\(', 'realloc() call', 'R2'),
    (r'\bfree\s*\(', 'free() call', 'R2'),
    (r'\balloca\s*\(', 'alloca() call', 'R2/R7'),
    (r'#\s*include\s*<\s*stdlib\.h\s*>', 'stdlib.h included (potential dynamic alloc)', 'R2'),
]

# VLA pattern: type name[variable] where variable is not a macro/constant
# We look for array declarations where the size is a lowercase identifier
# (constants/macros are typically UPPER_CASE)
VLA_PATTERN = re.compile(
    r'\b(?:uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|char|unsigned\s+char|int|unsigned\s+int)'
    r'\s+(\w+)\s*\[\s*([a-z]\w*)\s*\]',
    re.MULTILINE
)


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


def check_file(filepath):
    errors = []
    with open(filepath, 'r', errors='replace') as f:
        lines = f.readlines()

    # Check raw lines for forbidden patterns (to get line numbers)
    for line_num, line in enumerate(lines, 1):
        # Skip comment-only lines and NOLINT-suppressed lines
        stripped = line.strip()
        if stripped.startswith('//') or stripped.startswith('/*'):
            continue
        if 'NOLINT(determinism)' in line:
            continue

        for pattern, desc, rule in FORBIDDEN_PATTERNS:
            if re.search(pattern, line):
                errors.append((filepath, line_num, rule, desc, stripped.strip()))

    # Check for VLAs (need comment-free content but track line numbers)
    content = ''.join(lines)
    clean = strip_comments(content)
    for match in VLA_PATTERN.finditer(clean):
        var_name = match.group(1)
        size_expr = match.group(2)
        # Find approximate line number
        pos = match.start()
        line_num = clean[:pos].count('\n') + 1
        errors.append((filepath, line_num, 'R7',
                        f'Possible VLA: {var_name}[{size_expr}] — '
                        f'size must be compile-time constant (use UPPER_CASE #define)',
                        match.group(0).strip()))

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: check_no_malloc.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    all_errors = []

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            print(f"WARNING: {src_dir} does not exist, skipping")
            continue
        for filepath in find_c_files(src_dir):
            all_errors.extend(check_file(filepath))

    if all_errors:
        for filepath, line_num, rule, desc, context in all_errors:
            print(f"FAIL [{rule}] {filepath}:{line_num}: {desc}")
            print(f"     | {context}")
        print(f"\n{len(all_errors)} dynamic allocation / VLA violation(s) found.")
        sys.exit(1)
    else:
        print("PASS [R2/R7] No dynamic allocation or VLAs detected.")
        sys.exit(0)


if __name__ == '__main__':
    main()
