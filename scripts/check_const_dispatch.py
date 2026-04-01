#!/usr/bin/env python3
"""
check_const_dispatch.py — Enforce const qualifier on function pointer arrays.

Scans C source and header files for arrays of function pointers.  Any such
array MUST have the `const` qualifier (placed in ROM by XC8, not mutable
RAM).  Mutable function pointer dispatch is prohibited by DETERMINISM.md R8.

Usage: python3 scripts/check_const_dispatch.py runtime/src runtime/include
"""

import sys
import os
import re


def find_c_files(directories):
    """Find all .c and .h files in the given directories."""
    c_files = []
    for d in directories:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for f in files:
                if f.endswith(('.c', '.h')):
                    c_files.append(os.path.join(root, f))
    return sorted(c_files)


def strip_comments(source):
    """Remove C comments (block and line)."""
    source = re.sub(r'/\*.*?\*/', ' ', source, flags=re.DOTALL)
    source = re.sub(r'//[^\n]*', '', source)
    return source


# Pattern 1: Direct function pointer array declaration
# e.g.: type (*name[])(params) = { ... };
#        type (*name[N])(params) = { ... };
DIRECT_FPTR_ARRAY_RE = re.compile(
    r'^([^\n;{]*?)'                   # prefix (qualifiers, type) — group 1
    r'\(\s*\*\s*(\w+)\s*'             # (* name
    r'\[\s*\w*\s*\]\s*\)'             # [N] or []
    r'\s*\([^)]*\)'                   # (params)
    r'\s*=\s*\{',                     # = {
    re.MULTILINE
)

# Pattern 2: Typedef'd function pointer type used in array declaration
# First find typedefs: typedef ret (*name_t)(params);
FPTR_TYPEDEF_RE = re.compile(
    r'typedef\s+[\w\s\*]+?'           # typedef + return type
    r'\(\s*\*\s*(\w+)\s*\)'           # (*name)
    r'\s*\([^)]*\)\s*;',              # (params);
    re.MULTILINE
)


def check_file(filepath):
    """Check a single file for mutable function pointer arrays.

    Returns: (const_tables, violations)
        const_tables: list of (name, line) for const-qualified tables
        violations: list of (name, line, filepath) for non-const tables
    """
    with open(filepath, 'r') as f:
        raw_source = f.read()
        raw_lines = raw_source.splitlines()

    source = strip_comments(raw_source)

    const_tables = []
    violations = []

    # Find function pointer typedefs
    fptr_typedefs = set()
    for m in FPTR_TYPEDEF_RE.finditer(source):
        fptr_typedefs.add(m.group(1))

    # Pattern 1: Direct function pointer arrays
    for m in DIRECT_FPTR_ARRAY_RE.finditer(source):
        prefix = m.group(1)
        name = m.group(2)
        line_no = source[:m.start()].count('\n') + 1

        # Check NOLINT suppression
        if line_no - 1 < len(raw_lines) and 'NOLINT(determinism)' in raw_lines[line_no - 1]:
            continue

        if 'const' in prefix:
            const_tables.append((name, line_no))
        else:
            violations.append((name, line_no, filepath))

    # Pattern 2: Typedef'd function pointer arrays
    # Look for: fptr_type name[] = { ... };
    for typedef_name in fptr_typedefs:
        # Match: [static] [const] type_name name[N] = {
        pattern = re.compile(
            r'^([^\n;{]*?)'                     # prefix — group 1
            r'\b' + re.escape(typedef_name) +
            r'\s+(\w+)\s*'                       # variable name — group 2
            r'\[\s*\w*\s*\]\s*=\s*\{',          # [N] = {
            re.MULTILINE
        )
        for m in pattern.finditer(source):
            prefix = m.group(1)
            name = m.group(2)
            line_no = source[:m.start()].count('\n') + 1

            # Check NOLINT suppression
            if line_no - 1 < len(raw_lines) and 'NOLINT(determinism)' in raw_lines[line_no - 1]:
                continue

            if 'const' in prefix:
                const_tables.append((name, line_no))
            else:
                violations.append((name, line_no, filepath))

    return const_tables, violations


def main():
    directories = [arg for arg in sys.argv[1:] if not arg.startswith('--')]

    if not directories:
        print("Usage: python3 scripts/check_const_dispatch.py <dirs...>",
              file=sys.stderr)
        sys.exit(2)

    c_files = find_c_files(directories)
    if not c_files:
        print("PASS [CONST] No C files found.")
        sys.exit(0)

    all_const = []
    all_violations = []

    for filepath in c_files:
        const_tables, violations = check_file(filepath)
        all_const.extend(const_tables)
        all_violations.extend(violations)

    if all_violations:
        for name, line, filepath in all_violations:
            print(f"FAIL [CONST] {filepath}:{line}: {name}[] is a mutable "
                  f"function pointer array (must be const)")
        sys.exit(1)
    else:
        n_const = len(all_const)
        if n_const > 0:
            for name, line in all_const:
                print(f"  OK: {name}[] is const (line {line})")
        print(f"\nPASS [CONST] No mutable function pointer arrays found. "
              f"{n_const} const table(s) verified.")
        sys.exit(0)


if __name__ == '__main__':
    main()
