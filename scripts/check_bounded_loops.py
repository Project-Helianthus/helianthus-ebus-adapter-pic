#!/usr/bin/env python3
"""
check_bounded_loops.py — Detect unbounded loops in C source files.

Accepted patterns:
  for (i = 0; i < CONSTANT; i++)         — constant-bounded
  while (--timeout)                       — decrementing guard
  while (1) { ... main superloop only     — single allowed infinite loop

Rejected patterns:
  while (peripheral_flag)                 — unbounded busy-wait
  for (;;) without guarded break          — infinite loop
  while (variable) without decrement      — unbounded

Usage: python3 scripts/check_bounded_loops.py src/
"""

import sys
import os
import re


def find_c_files(directory):
    c_files = []
    for root, _, files in os.walk(directory):
        for f in files:
            if f.endswith(('.c', '.h')):
                c_files.append(os.path.join(root, f))
    return c_files


def strip_comments(content):
    content = re.sub(r'//.*$', '', content, flags=re.MULTILINE)
    # Replace block comments preserving newline count so line numbers stay aligned
    def _preserve_newlines(m):
        return '\n' * m.group(0).count('\n')
    content = re.sub(r'/\*.*?\*/', _preserve_newlines, content, flags=re.DOTALL)
    return content


def is_constant_expr(expr):
    """Check if expression looks like a compile-time constant."""
    expr = expr.strip()
    # C integer suffix pattern: u, U, l, L, ul, UL, ull, ULL, etc.
    suffix = r'[uUlL]{0,3}'
    # Pure number (with optional suffix): 42, 0xFF, 8u, 32UL
    if re.match(rf'^(0x[\da-fA-F]+|\d+){suffix}$', expr):
        return True
    # UPPER_CASE macro/define
    if re.match(r'^[A-Z_][A-Z0-9_]*$', expr):
        return True
    # sizeof(...)
    if expr.startswith('sizeof'):
        return True
    # Simple arithmetic on constants: CONST - 1, CONST + 1u
    if re.match(rf'^[A-Z_][A-Z0-9_]*\s*[-+*/]\s*(0x[\da-fA-F]+|\d+){suffix}$', expr):
        return True
    return False


def has_decrementing_guard(condition):
    """Check if while condition is a decrementing guard like --timeout."""
    condition = condition.strip()
    # --var or var--
    if re.match(r'^--\w+$', condition) or re.match(r'^\w+--$', condition):
        return True
    return False


def is_main_superloop(line, filepath):
    """Heuristic: while(1) in main.c inside main() is the superloop."""
    return 'main' in os.path.basename(filepath)


def check_file(filepath):
    errors = []
    warnings = []

    with open(filepath, 'r', errors='replace') as f:
        content = f.read()

    original_lines = content.split('\n')
    clean = strip_comments(content)
    lines = clean.split('\n')

    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()

        # Skip lines with NOLINT suppression (check original source,
        # since strip_comments removes the comment containing NOLINT)
        if line_num <= len(original_lines) and 'NOLINT(determinism)' in original_lines[line_num - 1]:
            continue

        # --- for loops ---
        for_match = re.match(r'for\s*\(([^;]*);([^;]*);([^)]*)\)', stripped)
        if for_match:
            init, cond, incr = for_match.groups()
            # for (;;) — infinite loop
            if not cond.strip():
                if not _has_bounded_break_nearby(lines, line_num):
                    errors.append((filepath, line_num, 'for(;;) without bounded break'))
                continue
            # Check if condition compares against constant.
            # Support compound conditions: if ANY sub-condition (split
            # on &&) has a constant upper bound, the loop is bounded.
            sub_conds = [s.strip() for s in cond.strip().split('&&')]
            any_constant_bound = False
            for sc in sub_conds:
                sc_match = re.search(r'[<>=!]+\s*(.+)$', sc)
                if sc_match:
                    sc_bound = sc_match.group(1).strip().rstrip(')')
                    if is_constant_expr(sc_bound):
                        any_constant_bound = True
                        break
            if not any_constant_bound:
                cmp_match = re.search(r'[<>=!]+\s*(.+)$', cond.strip())
                if cmp_match:
                    bound = cmp_match.group(1).strip().rstrip(')')
                    if not is_constant_expr(bound):
                        lhs_match = re.search(r'^(\w+)', cond.strip())
                        if lhs_match and not is_constant_expr(lhs_match.group(1)):
                            warnings.append((filepath, line_num,
                                             f'for-loop bound may not be constant: {cond.strip()}'))
            continue

        # --- while loops ---
        # Match both while(...){ and while(...); (busy-wait)
        while_match = re.match(r'while\s*\((.+)\)\s*[{;]?', stripped)
        if while_match:
            condition = while_match.group(1).strip()

            # Detect while(EXPR); — semicolon-terminated busy-wait
            if re.match(r'while\s*\(.+\)\s*;', stripped):
                if not has_decrementing_guard(condition):
                    errors.append((filepath, line_num,
                                   f'Busy-wait: while({condition}); — '
                                   f'use interrupt-driven I/O or add timeout guard.'))
                continue
            # while(1) or while(true) — superloop check
            if condition in ('1', 'true', '!0'):
                if is_main_superloop(line, filepath):
                    continue  # main superloop is OK
                else:
                    # Check if it has a bounded break
                    if not _has_bounded_break_nearby(lines, line_num):
                        errors.append((filepath, line_num,
                                       f'while(1) outside main() without bounded break'))
                continue

            # Decrementing guard: while(--timeout)
            if has_decrementing_guard(condition):
                continue  # OK

            # Check for bit/flag polling without timeout
            # Patterns like: while(REG & BIT), while(!flag), while(UART_busy())
            if re.match(r'^!?\w+\s*$', condition) or \
               re.match(r'^[^&]*&[^&]', condition) or \
               re.match(r'^!?\w+\s*\(.*\)$', condition):
                # These are suspicious — might be unbounded busy-waits
                # Check if there's a timeout guard nearby
                context_start = max(0, line_num - 3)
                context = '\n'.join(lines[context_start:line_num + 5])
                if not re.search(r'timeout|TIMEOUT|--\w+|_cnt\s*>', context):
                    errors.append((filepath, line_num,
                                   f'Potentially unbounded busy-wait: while({condition}). '
                                   f'Add a decrementing timeout guard.'))
            continue

        # --- do-while loops ---
        do_match = re.match(r'do\s*\{?', stripped)
        if do_match:
            # Find the matching while condition
            # Simple heuristic: search forward for } while(...)
            for look_ahead in range(line_num, min(line_num + 50, len(lines))):
                dw_match = re.match(r'\}\s*while\s*\((.+)\)\s*;', lines[look_ahead - 1].strip() if look_ahead > 0 else '')
                if dw_match:
                    dw_cond = dw_match.group(1).strip()
                    if not has_decrementing_guard(dw_cond) and \
                       not is_constant_expr(dw_cond) and \
                       dw_cond not in ('0', 'false'):
                        warnings.append((filepath, look_ahead,
                                         f'do-while condition may be unbounded: {dw_cond}'))
                    break

    return errors, warnings


def _has_bounded_break_nearby(lines, start_line, max_look=30):
    """Check if there's a break with a decrementing counter nearby."""
    end = min(start_line + max_look, len(lines))
    context = '\n'.join(lines[start_line:end])
    # Look for patterns like: if (--counter == 0) break; or if (!--timeout) break;
    if re.search(r'(--\w+|timeout|TIMEOUT)\s*.*break', context):
        return True
    if re.search(r'break\s*;', context) and re.search(r'--\w+', context):
        return True
    return False


def main():
    if len(sys.argv) < 2:
        print("Usage: check_bounded_loops.py <source_dir> [<source_dir2> ...]")
        sys.exit(2)

    all_errors = []
    all_warnings = []

    for src_dir in sys.argv[1:]:
        if not os.path.exists(src_dir):
            print(f"WARNING: {src_dir} does not exist, skipping")
            continue
        for filepath in find_c_files(src_dir):
            errors, warnings = check_file(filepath)
            all_errors.extend(errors)
            all_warnings.extend(warnings)

    for filepath, line_num, desc in all_warnings:
        print(f"WARN [R3] {filepath}:{line_num}: {desc}")

    for filepath, line_num, desc in all_errors:
        print(f"FAIL [R3] {filepath}:{line_num}: {desc}")

    if all_errors:
        print(f"\n{len(all_errors)} unbounded loop violation(s), "
              f"{len(all_warnings)} warning(s).")
        sys.exit(1)
    else:
        if all_warnings:
            print(f"\nPASS [R3] with {len(all_warnings)} warning(s) — review recommended.")
        else:
            print("PASS [R3] All loops appear bounded.")
        sys.exit(0)


if __name__ == '__main__':
    main()
