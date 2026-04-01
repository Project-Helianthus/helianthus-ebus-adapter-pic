#!/usr/bin/env python3
"""
check_wcet_isr.py — Source-level WCET estimator for ISR-context functions.

Finds ISR-context functions by naming pattern (*_isr_*) and ISR-CONTEXT
annotation, then transitively marks callees.  Estimates cycle cost with a
first-match heuristic modeled on PIC16 instruction timing.

Budget: 60 cycles by default (150 us at 2.5 MHz instruction clock — well
under the 417 us eBUS bit time).

Usage: python3 scripts/check_wcet_isr.py runtime/src runtime/include [--max-cycles=60]
"""

import sys
import os
import re
from collections import defaultdict


def find_c_files(directories):
    """Find all .c files in the given directories."""
    c_files = []
    for d in directories:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for f in files:
                if f.endswith('.c'):
                    c_files.append(os.path.join(root, f))
    return sorted(c_files)


# Regex for C function definition (simplified, handles static and non-static)
FUNC_DEF_RE = re.compile(
    r'^(?:static\s+)?'            # optional static
    r'(?:[\w\s\*]+?)\s+'          # return type
    r'(\w+)\s*\('                 # function name + open paren
    r'[^)]*\)\s*\{',             # params + open brace
    re.MULTILINE
)

# Regex for function calls inside a body
FUNC_CALL_RE = re.compile(r'\b(\w+)\s*\(')

# Keywords that look like function calls but aren't
NOT_FUNCTIONS = {
    'if', 'else', 'while', 'for', 'do', 'switch', 'case', 'return',
    'sizeof', 'typedef', '_Static_assert', 'defined',
}


def strip_comments(source):
    """Remove C comments (block and line)."""
    source = re.sub(r'/\*.*?\*/', ' ', source, flags=re.DOTALL)
    source = re.sub(r'//[^\n]*', '', source)
    return source


def extract_functions(filepath):
    """Extract function bodies and their call graph."""
    with open(filepath, 'r') as f:
        raw_source = f.read()
        raw_lines = raw_source.splitlines()

    source = strip_comments(raw_source)
    functions = {}  # name -> {body, calls, file, line, is_isr_direct}

    for m in FUNC_DEF_RE.finditer(source):
        name = m.group(1)
        start = m.start()
        line_no = source[:start].count('\n') + 1

        # Extract body by brace matching
        brace_start = source.index('{', m.end() - 1)
        depth = 1
        pos = brace_start + 1
        while pos < len(source) and depth > 0:
            if source[pos] == '{':
                depth += 1
            elif source[pos] == '}':
                depth -= 1
            pos += 1
        body = source[brace_start:pos]

        # Find function calls in body
        calls = set()
        for cm in FUNC_CALL_RE.finditer(body):
            callee = cm.group(1)
            if callee not in NOT_FUNCTIONS and not callee.startswith('PICFW_') and callee.upper() != callee:
                calls.add(callee)
            elif callee not in NOT_FUNCTIONS and callee[0].islower():
                calls.add(callee)

        # Check if this is a direct ISR-context function
        is_isr_direct = False
        # Pattern 1: name contains _isr_
        if '_isr_' in name:
            is_isr_direct = True
        # Pattern 2: ISR-CONTEXT comment on preceding line or same line
        if line_no >= 2 and line_no - 2 < len(raw_lines):
            prev_line = raw_lines[line_no - 2] if line_no >= 2 else ''
            curr_line = raw_lines[line_no - 1] if line_no >= 1 else ''
            if 'ISR-CONTEXT' in prev_line or 'ISR-CONTEXT' in curr_line:
                is_isr_direct = True

        # Check NOLINT suppression
        if line_no - 1 < len(raw_lines):
            func_line = raw_lines[line_no - 1]
            if 'NOLINT(determinism)' in func_line:
                continue

        functions[name] = {
            'body': body,
            'calls': calls,
            'file': filepath,
            'line': line_no,
            'is_isr_direct': is_isr_direct,
        }

    return functions


def estimate_wcet(body):
    """Estimate WCET cycles for a function body using first-match heuristic."""
    cycles = 0
    lines = body.split('\n')

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith('//') or stripped == '{' or stripped == '}':
            continue

        if 'NOLINT(determinism)' in stripped:
            continue

        # Budget-blowing patterns (first-match-wins)
        if re.search(r'\b(for|while)\s*\(', stripped):
            cycles += 100
            continue
        if re.search(r'\bdo\s*\{', stripped):
            cycles += 100
            continue
        if re.search(r'\b(printf|sprintf)\s*\(', stripped):
            cycles += 200
            continue
        if re.search(r'\b(memcpy|memset|strlen)\s*\(', stripped):
            cycles += 50
            continue
        if re.search(r'\bdelay', stripped, re.IGNORECASE):
            if re.search(r'\bdelay\w*\s*\(', stripped, re.IGNORECASE):
                cycles += 10000
                continue

        # Normal operations
        if re.search(r'\w+\s*\[', stripped) and '=' in stripped:
            cycles += 4  # array access with index
            continue
        if '&&' in stripped or '||' in stripped:
            cycles += 4  # compound condition
            continue
        if re.search(r'\bif\s*\(', stripped):
            cycles += 3
            continue
        if re.search(r'\belse\s*\{', stripped) or stripped == 'else':
            cycles += 1
            continue
        if re.search(r'\breturn\b', stripped):
            cycles += 2
            continue
        if re.search(r'\+\+|--', stripped) and '=' not in stripped.replace('++', '').replace('--', ''):
            cycles += 1
            continue
        if '=' in stripped and '==' not in stripped and '!=' not in stripped:
            cycles += 2
            continue

        # Function call (fallthrough)
        if re.search(r'\b\w+\s*\(', stripped) and not stripped.startswith('if') and not stripped.startswith('for'):
            cycles += 4
            continue

        # Default: 1 cycle for anything else
        cycles += 1

    return cycles


def main():
    max_cycles = 60
    directories = []

    for arg in sys.argv[1:]:
        if arg.startswith('--max-cycles='):
            max_cycles = int(arg.split('=')[1])
        else:
            directories.append(arg)

    if not directories:
        print("Usage: python3 scripts/check_wcet_isr.py <dirs...> [--max-cycles=N]",
              file=sys.stderr)
        sys.exit(2)

    c_files = find_c_files(directories)
    if not c_files:
        print("PASS [WCET] No C files found.")
        sys.exit(0)

    # Collect all functions across files
    all_functions = {}
    for filepath in c_files:
        funcs = extract_functions(filepath)
        all_functions.update(funcs)

    # Find direct ISR-context functions
    isr_direct = {name for name, info in all_functions.items() if info['is_isr_direct']}

    if not isr_direct:
        print("PASS [WCET] No ISR-context functions found.")
        sys.exit(0)

    # Transitively mark callees as ISR-context
    isr_context = set(isr_direct)
    changed = True
    while changed:
        changed = False
        for name in list(isr_context):
            if name in all_functions:
                for callee in all_functions[name]['calls']:
                    if callee in all_functions and callee not in isr_context:
                        isr_context.add(callee)
                        changed = True

    # Print ISR-context discovery
    print("ISR-context functions detected:")
    for name in sorted(isr_direct):
        info = all_functions[name]
        isr_callees = [c for c in info['calls'] if c in isr_context and c in all_functions]
        print(f"  {name} (direct: name match)")
        for callee in sorted(isr_callees):
            print(f"    calls: {callee} (transitive)")
    transitive_only = isr_context - isr_direct
    for name in sorted(transitive_only):
        if name in all_functions:
            info = all_functions[name]
            print(f"  {name} (transitive: called by ISR-context)")
    print()

    # Compute WCET with transitive callee costs
    # First pass: estimate body-only WCET for all ISR-context functions
    body_wcet = {}
    for name in isr_context:
        if name in all_functions:
            body_wcet[name] = estimate_wcet(all_functions[name]['body'])

    # Second pass: total WCET includes callee costs
    total_wcet = {}
    # Process leaf functions first (no ISR-context callees), then upward
    resolved = set()
    max_iterations = len(isr_context) + 1
    for _ in range(max_iterations):
        for name in isr_context:
            if name in resolved or name not in all_functions:
                continue
            info = all_functions[name]
            isr_callees = [c for c in info['calls']
                          if c in isr_context and c in all_functions and c != name]
            # Check if all callees are resolved
            if all(c in resolved for c in isr_callees):
                callee_cost = sum(total_wcet.get(c, 0) for c in isr_callees)
                own_cost = body_wcet.get(name, 0)
                # For direct ISR functions, add ISR overhead (+4 entry, +4 exit)
                isr_overhead = 8 if name in isr_direct else 0
                total_wcet[name] = own_cost + callee_cost + isr_overhead
                resolved.add(name)

    # Handle any unresolved (circular — shouldn't happen per R1)
    for name in isr_context:
        if name not in resolved and name in all_functions:
            total_wcet[name] = body_wcet.get(name, 0) + (8 if name in isr_direct else 0)

    # Print WCET estimates
    print("WCET estimates:")
    failures = []
    for name in sorted(total_wcet.keys(), key=lambda n: total_wcet[n]):
        wcet = total_wcet[name]
        info = all_functions.get(name, {})
        isr_callees = [c for c in info.get('calls', [])
                      if c in isr_context and c in all_functions and c != name]
        body_cost = body_wcet.get(name, 0)

        parts = [f"~{body_cost} own"]
        for callee in sorted(isr_callees):
            parts.append(f"{callee} ~{total_wcet.get(callee, 0)}")
        if name in isr_direct:
            parts.append("+ 8 ISR overhead")

        detail = ", ".join(parts)
        status = "OK" if wcet <= max_cycles else "OVER"
        print(f"  {name}(): ~{wcet} cycles ({detail}) [{status}]")

        if wcet > max_cycles:
            failures.append((name, wcet, info.get('file', '?'), info.get('line', 0)))

    print()
    if failures:
        for name, wcet, filepath, line in failures:
            print(f"FAIL [WCET] {filepath}:{line}: {name}() estimated ~{wcet} cycles "
                  f"(budget: {max_cycles})")
        sys.exit(1)
    else:
        print(f"PASS [WCET] All ISR-context functions within cycle budget ({max_cycles}).")
        sys.exit(0)


if __name__ == '__main__':
    main()
