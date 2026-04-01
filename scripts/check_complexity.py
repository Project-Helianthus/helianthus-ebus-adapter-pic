#!/usr/bin/env python3
"""
check_complexity.py — Measure cyclomatic complexity of C functions.

Rejects any function with cyclomatic complexity >= threshold (default 15).
Uses a simple heuristic: count decision points (if, else if, case, for,
while, do, &&, ||, ?:) + 1.

Usage: python3 scripts/check_complexity.py src/ [--max=15]
"""

import sys
import os
import re


DEFAULT_MAX_COMPLEXITY = 15

DECISION_POINTS = [
    r'\bif\s*\(',
    r'\belse\s+if\s*\(',
    r'\bcase\s+',
    r'\bfor\s*\(',
    r'\bwhile\s*\(',
    r'\bdo\s*\{',
    r'\?\s*',           # ternary operator
    r'&&',
    r'\|\|',
]


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith('.c'):
                c_files.append(os.path.join(root, f))
    return c_files


def strip_comments(content):
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    return content


def extract_functions(content):
    """Extract function name and body pairs."""
    functions = []
    func_pattern = re.compile(
        r'(?:[\w\*\s]+?)\s+'
        r'(?:__interrupt\s*(?:\(\s*\))?\s*)?'
        r'(\w+)\s*\([^)]*\)\s*\{',
        re.MULTILINE
    )

    skip_keywords = {'if', 'for', 'while', 'switch', 'return', 'sizeof',
                     'typedef', 'struct', 'enum', 'union', 'else', 'do'}

    for match in func_pattern.finditer(content):
        func_name = match.group(1)
        if func_name in skip_keywords:
            continue

        start = match.end() - 1
        depth = 0
        pos = start
        while pos < len(content):
            if content[pos] == '{':
                depth += 1
            elif content[pos] == '}':
                depth -= 1
                if depth == 0:
                    line_num = content[:match.start()].count('\n') + 1
                    functions.append((func_name, content[start:pos + 1], line_num))
                    break
            pos += 1

    return functions


def calculate_complexity(body):
    """Calculate cyclomatic complexity as decision points + 1."""
    # Remove strings to avoid false matches
    body = re.sub(r'"(?:[^"\\]|\\.)*"', '""', body)
    body = re.sub(r"'(?:[^'\\]|\\.)'", "''", body)

    complexity = 1  # base complexity
    for pattern in DECISION_POINTS:
        complexity += len(re.findall(pattern, body))

    return complexity


def main():
    max_complexity = DEFAULT_MAX_COMPLEXITY
    source_dirs = []

    for arg in sys.argv[1:]:
        if arg.startswith('--max='):
            max_complexity = int(arg.split('=')[1])
        else:
            source_dirs.append(arg)

    if not source_dirs:
        print(f"Usage: check_complexity.py <source_dir> [--max={DEFAULT_MAX_COMPLEXITY}]")
        sys.exit(2)

    errors = []
    all_functions = []

    for src_dir in source_dirs:
        if not os.path.exists(src_dir):
            continue
        for filepath in find_c_files(src_dir):
            with open(filepath, 'r', errors='replace') as f:
                content = f.read()
            clean = strip_comments(content)
            functions = extract_functions(clean)
            for func_name, body, line_num in functions:
                cc = calculate_complexity(body)
                all_functions.append((filepath, func_name, line_num, cc))
                if cc >= max_complexity:
                    errors.append((filepath, func_name, line_num, cc))

    # Print summary of top functions by complexity
    all_functions.sort(key=lambda x: x[3], reverse=True)
    print("Cyclomatic complexity report (top 10):")
    print(f"{'Function':<30} {'CC':>4}  {'File'}")
    print("-" * 70)
    for filepath, func_name, line_num, cc in all_functions[:10]:
        marker = " ← FAIL" if cc >= max_complexity else ""
        fname = os.path.basename(filepath)
        print(f"  {func_name:<28} {cc:>4}  {fname}:{line_num}{marker}")
    print()

    if errors:
        for filepath, func_name, line_num, cc in errors:
            print(f"FAIL [R8] {filepath}:{line_num}: {func_name}() "
                  f"complexity={cc} (max={max_complexity}). "
                  f"Refactor into smaller functions or simplify logic.")
        print(f"\n{len(errors)} function(s) exceed complexity threshold.")
        sys.exit(1)
    else:
        print(f"PASS [R8] All functions below complexity threshold ({max_complexity}).")
        sys.exit(0)


if __name__ == '__main__':
    main()
