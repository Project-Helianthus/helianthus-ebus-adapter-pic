#!/usr/bin/env python3
"""
check_stack_depth.py — Measure maximum call stack depth from root functions.

PIC16F15356 has a 16-level hardware call stack. Budget: 14 (2 reserved for
ISR nesting). Builds a call graph via regex-based parsing, finds root
functions (defined but never called), and computes the longest DFS path
from each root.

Usage: python3 scripts/check_stack_depth.py src/ [--max-depth=14]
"""

import sys
import os
import re
from collections import defaultdict


DEFAULT_MAX_DEPTH = 14


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


def extract_functions_and_calls(filepath):
    """Extract function definitions and callees. Returns (defined, calls)."""
    with open(filepath, 'r', errors='replace') as f:
        content = f.read()

    content = strip_comments(content)
    content = re.sub(r'"(?:[^"\\]|\\.)*"', '""', content)
    content = re.sub(r'^\s*#.*$', '', content, flags=re.MULTILINE)

    defined = set()
    calls = defaultdict(set)

    func_pattern = re.compile(
        r'(?:[\w\*\s]+?)\s+'
        r'(?:__interrupt\s*(?:\(\s*\))?\s*)?'
        r'(\w+)\s*\([^)]*\)\s*\{',
        re.MULTILINE
    )

    skip_keywords = {'if', 'for', 'while', 'switch', 'return', 'sizeof',
                     'typedef', 'struct', 'enum', 'union', 'else', 'do'}

    call_pattern = re.compile(r'\b(\w+)\s*\(')
    call_keywords = {'if', 'for', 'while', 'switch', 'return', 'sizeof',
                     'typeof', 'defined', '__delay_ms', '__delay_us', 'asm',
                     '__asm', '_Static_assert'}

    for match in func_pattern.finditer(content):
        func_name = match.group(1)
        if func_name in skip_keywords:
            continue

        defined.add(func_name)

        start = match.end() - 1
        depth = 0
        pos = start
        while pos < len(content):
            if content[pos] == '{':
                depth += 1
            elif content[pos] == '}':
                depth -= 1
                if depth == 0:
                    body = content[start:pos + 1]
                    for call_match in call_pattern.finditer(body):
                        called = call_match.group(1)
                        if called not in call_keywords and called != func_name:
                            calls[func_name].add(called)
                    break
            pos += 1

    return defined, calls


def find_roots(defined, calls):
    """Find root functions: defined but never called by other defined functions."""
    called_by_others = set()
    for caller, callees in calls.items():
        if caller in defined:
            for callee in callees:
                if callee in defined:
                    called_by_others.add(callee)

    roots = defined - called_by_others

    # Force-include ISR and main patterns
    for func in defined:
        if func == 'main' or '_isr_' in func.lower() or '_ISR' in func:
            roots.add(func)

    return roots


def max_depth_from(node, calls, defined, visited):
    """DFS to find max depth. Returns depth (1-based: leaf = 1)."""
    if node in visited:
        return 0  # cycle — don't recurse
    visited.add(node)

    max_child = 0
    for callee in calls.get(node, set()):
        if callee in defined:
            child_depth = max_depth_from(callee, calls, defined, visited)
            max_child = max(max_child, child_depth)
        else:
            # Unknown external callee counts as +1 leaf
            max_child = max(max_child, 1)

    visited.discard(node)
    return 1 + max_child


def main():
    max_depth = DEFAULT_MAX_DEPTH
    source_dirs = []

    for arg in sys.argv[1:]:
        if arg.startswith('--max-depth='):
            max_depth = int(arg.split('=')[1])
        else:
            source_dirs.append(arg)

    if not source_dirs:
        print(f"Usage: check_stack_depth.py <source_dir> [--max-depth={DEFAULT_MAX_DEPTH}]")
        sys.exit(2)

    all_defined = set()
    all_calls = defaultdict(set)

    for src_dir in source_dirs:
        if not os.path.exists(src_dir):
            print(f"WARNING: {src_dir} does not exist, skipping")
            continue
        for filepath in find_c_files(src_dir):
            defined, calls = extract_functions_and_calls(filepath)
            all_defined.update(defined)
            for func, callees in calls.items():
                all_calls[func].update(callees)

    roots = find_roots(all_defined, all_calls)
    if not roots:
        print("WARNING: No root functions found.")
        sys.exit(0)

    errors = []
    results = []

    for root in sorted(roots):
        depth = max_depth_from(root, all_calls, all_defined, set())
        results.append((root, depth))
        if depth > max_depth:
            errors.append((root, depth))

    # Sort by depth descending
    results.sort(key=lambda x: x[1], reverse=True)

    print("Stack depth report:")
    for func, depth in results[:15]:
        marker = " ← FAIL" if depth > max_depth else ""
        print(f"  {func + '()':<50} → max depth {depth}{marker}")
    print()

    if errors:
        for func, depth in errors:
            print(f"FAIL [STACK] {func}() max depth={depth} (limit={max_depth})")
        print(f"\n{len(errors)} stack depth violation(s) found.")
        sys.exit(1)
    else:
        print(f"PASS [STACK] All call chains within depth limit ({max_depth}).")
        sys.exit(0)


if __name__ == '__main__':
    main()
