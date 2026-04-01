#!/usr/bin/env python3
"""
check_no_recursion.py — Detect direct and mutual recursion in C source files.

Builds a call graph from C sources using regex-based parsing (no external
dependencies) and checks for cycles via DFS. Exits non-zero if recursion found.

Usage: python3 scripts/check_no_recursion.py src/
"""

import sys
import os
import re
from collections import defaultdict


def find_c_files(directory):
    """Find all .c and .h files recursively."""
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def extract_functions_and_calls(filepath):
    """
    Extract function definitions and the functions they call.
    Returns dict: {function_name: set(called_functions)}
    """
    with open(filepath, 'r', errors='replace') as f:
        content = f.read()

    # Remove single-line comments
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    # Remove multi-line comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # Remove string literals (to avoid false matches)
    content = re.sub(r'"(?:[^"\\]|\\.)*"', '""', content)
    # Remove preprocessor directives
    content = re.sub(r'^\s*#.*$', '', content, flags=re.MULTILINE)

    calls = defaultdict(set)

    # Match function definitions: type name(params) {
    # Handles __interrupt(), void*, uint8_t, etc.
    func_pattern = re.compile(
        r'(?:[\w\*\s]+?)\s+'           # return type
        r'(__interrupt\s*\(\s*\)\s+)?'  # optional __interrupt()
        r'(\w+)\s*'                      # function name
        r'\([^)]*\)\s*\{',              # parameters + opening brace
        re.MULTILINE
    )

    # Find function boundaries by tracking brace depth
    func_bodies = []
    for match in func_pattern.finditer(content):
        func_name = match.group(2)
        # Skip type-like keywords
        if func_name in ('if', 'for', 'while', 'switch', 'return', 'sizeof',
                         'typedef', 'struct', 'enum', 'union', 'else'):
            continue

        start = match.end() - 1  # position of '{'
        depth = 0
        pos = start
        while pos < len(content):
            if content[pos] == '{':
                depth += 1
            elif content[pos] == '}':
                depth -= 1
                if depth == 0:
                    func_bodies.append((func_name, content[start:pos + 1], filepath))
                    break
            pos += 1

    # Extract function calls from each body
    call_pattern = re.compile(r'\b(\w+)\s*\(')
    keywords = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'typeof',
                'defined', '__delay_ms', '__delay_us', 'asm', '__asm'}

    for func_name, body, fpath in func_bodies:
        for call_match in call_pattern.finditer(body):
            called = call_match.group(1)
            if called not in keywords and called != func_name.split('(')[0]:
                # Even self-calls are added — that's direct recursion
                calls[func_name].add(called)
            if called == func_name:
                calls[func_name].add(called)  # explicit self-call

    return calls


def find_cycles(graph):
    """Find all cycles in directed graph using DFS. Returns list of cycles."""
    cycles = []
    visited = set()
    rec_stack = set()
    path = []

    def dfs(node):
        visited.add(node)
        rec_stack.add(node)
        path.append(node)

        for neighbor in graph.get(node, set()):
            if neighbor in graph:  # only check defined functions
                if neighbor not in visited:
                    dfs(neighbor)
                elif neighbor in rec_stack:
                    # Found a cycle
                    cycle_start = path.index(neighbor)
                    cycle = path[cycle_start:] + [neighbor]
                    cycles.append(cycle)

        path.pop()
        rec_stack.discard(node)

    for node in graph:
        if node not in visited:
            dfs(node)

    return cycles


def main():
    if len(sys.argv) < 2:
        print("Usage: check_no_recursion.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    all_calls = defaultdict(set)
    errors = 0

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            print(f"WARNING: {src_dir} does not exist, skipping")
            continue
        for filepath in find_c_files(src_dir):
            calls = extract_functions_and_calls(filepath)
            for func, callees in calls.items():
                all_calls[func].update(callees)

    # Check for direct self-recursion
    for func, callees in all_calls.items():
        if func in callees:
            print(f"FAIL [R1] Direct recursion: {func}() calls itself")
            errors += 1

    # Check for mutual recursion (cycles in call graph)
    cycles = find_cycles(dict(all_calls))
    seen_cycles = set()
    for cycle in cycles:
        # Normalize cycle for dedup
        key = tuple(sorted(set(cycle)))
        if key not in seen_cycles and len(set(cycle)) > 1:
            seen_cycles.add(key)
            chain = ' -> '.join(cycle)
            print(f"FAIL [R1] Mutual recursion detected: {chain}")
            errors += 1

    if errors > 0:
        print(f"\n{errors} recursion violation(s) found.")
        sys.exit(1)
    else:
        print("PASS [R1] No recursion detected in call graph.")
        sys.exit(0)


if __name__ == '__main__':
    main()
