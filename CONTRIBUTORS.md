# Contributors

## Contribution Model: Dual-AI Agentic Development

This firmware was developed entirely through an agentic AI workflow. No line of code was written by a human hand — every function, test, and documentation file was authored by AI agents operating under strict human oversight and architectural direction.

### Why This Matters

Traditional firmware development for safety-adjacent embedded systems relies on individual developers writing code, then reviewing it. This creates single points of failure: one tired developer, one missed edge case, one untested path.

The agentic model inverts this:

1. **Human provides intent and constraints** — architectural decisions, protocol specifications, hardware targets, quality criteria
2. **AI agents implement** — writing code, tests, documentation
3. **Adversarial AI agents attack** — 11 independent adversarial review agents analyzed the codebase from C11 correctness, silent failure, determinism, type safety, ENH protocol compliance, eBUS wire protocol, and scan FSM behavioral parity angles
4. **Fix-rescan cycle until convergence** — 64 findings identified, 64 fixed, re-scanned to 0 CRITICAL + 0 HIGH + 0 MEDIUM
5. **Automated enforcement pipeline** — 8 determinism check scripts run on every commit, blocking violations before they enter the codebase

### Why the Rigid Contribution Flow

This repository enforces extreme contribution rigidity for a specific reason: **firmware that controls heating systems must be perfectly deterministic**.

The eBUS protocol operates at 2400 baud with bit-level arbitration. A firmware jitter of >10 microseconds can cause bus corruption, lost frames, and malfunctioning heating systems in people's homes. There is no room for "probably correct" or "works on my machine."

The contribution rules (detailed in `DETERMINISM.md`) are:

- **R1: Zero recursion** — PIC16F15356 has a 16-level hardware call stack. Recursion makes stack depth unbounded and WCET uncomputable.
- **R2: Zero dynamic allocation** — 2KB RAM. Heap fragmentation is fatal.
- **R3: All loops bounded** — Unbounded loops make worst-case execution time infinite.
- **R4: ISR bodies bounded** — Interrupt handlers must complete within the eBUS bit-time budget.
- **R5: No blocking delays in critical paths** — Busy-waits block all interrupts.
- **R6: No floating point** — PIC16 has no FPU. Software float is 200-2000 non-deterministic cycles.
- **R7: No variable-length arrays** — Stack consumption must be predictable.
- **R8: Cyclomatic complexity bounded** — Complex functions are harder to verify and test.
- **R9: Hardware timers only** — No instruction-counting for timing.
- **R10: Power-of-two ring buffers** — Bitmask indexing is 1 cycle; modulo is ~40 cycles on PIC16.

These are not guidelines. They are blocking rules enforced by automated checks (`make check-all`), pre-commit hooks, and CI. A PR that violates any rule cannot be merged.

### Development History

| Phase | Agent | Work |
|-------|-------|------|
| Scaffold | OpenAI Codex (GPT-5.4) | Initial runtime, bootloader, HAL, codec, oracle, scan FSM skeleton |
| Wave 1 | Claude Opus 4 | Descriptor engine: rotation primitives, merge+validate, load/read descriptor, scan mask functions, deep FSM integration |
| Wave 2 | Claude Opus 4 | Full initialize_scan_slot, seed recomputation chain, slot-level operations, app main loop skeleton |
| Adversarial Round 1 | Claude Opus 4 (8 agents) | 60 findings across C11 UB, silent failures, determinism, type design, ENH compliance, wire protocol, FSM parity |
| Fix Batches 1-7 | Claude Opus 4 (7 agents) | All 60 findings fixed: safety, protocol, parity, tests, naming, docs, polish |
| Adversarial Round 2 | Claude Opus 4 (3 agents) | Re-scan found 4 residual MEDIUM findings |
| Fix Batch 8 | Claude Opus 4 | 4 residual findings fixed |
| Convergence Scan | Claude Opus 4 | Final scan: 0 CRITICAL, 0 HIGH, 0 MEDIUM — convergence achieved |
| Pipeline Integration | Claude Opus 4 | Determinism enforcement pipeline: 8 check scripts, pre-commit hook, GitHub Actions CI |

### Human Direction

**Razvan Dubau** — Project architect, hardware operator, adversarial review director. Provided:
- Architectural decisions and protocol specifications
- Decompiled original firmware (Ghidra export of production adapter)
- eBUS protocol documentation and timing references
- Review direction: what to attack, when to converge, quality criteria
- Hardware access: PIC16F15356 adapter, eBUS bus, Home Assistant deployment

### AI Agents

**OpenAI Codex** (GPT-5.4) — Initial scaffold, adversarial planning, code review
**Anthropic Claude** (Opus 4) — Implementation waves, adversarial analysis, fix cycles, convergence, pipeline integration

### License

This project is licensed under the GNU Affero General Public License v3.0 or later. See `LICENSE` for details.
