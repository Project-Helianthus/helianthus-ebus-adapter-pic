---
description: "Launch all 7 adversarial review agents in parallel on current branch"
---

You are the adversarial review orchestrator for `helianthus-ebus-adapter-pic`.

## Your Mission

Launch all 7 specialized adversarial review agents in parallel using the Agent tool. Each agent analyzes the current branch (vs `main`) from a different angle. Wait for all to complete, then compile a summary table.

## Agents to Launch

Launch these 7 agents **in a single message** (all in parallel):

1. **C11 UB & Correctness** — `/review-ub-correctness` scope
2. **Silent Failure Hunter** — `/review-silent-failures` scope
3. **ENH Protocol Compliance** — `/review-enh-protocol` scope
4. **Scan FSM Parity** — `/review-scan-fsm-parity` scope
5. **Type Design** — `/review-type-design` scope
6. **Determinism Compliance** — `/review-determinism` scope
7. **Test Coverage** — `/review-test-coverage` scope

For each agent, use the Agent tool with `subagent_type: "general-purpose"` and include the full skill prompt content from the corresponding `.claude/commands/review-*.md` file in the agent prompt. Add context:

- Repository: `helianthus-ebus-adapter-pic` (PIC16F15356 eBUS adapter firmware)
- Working directory: the repo root
- Branch: current branch vs `main` baseline
- Key files: `runtime/src/runtime.c`, `bootloader/src/picboot.c`, `runtime/src/codec_ens.c`, `runtime/src/codec_enh.c`, `tests/test_runtime.c`, `tools/picboot_oracle_check.c`, `DETERMINISM.md`
- The agent should read files, run tests, and produce a verdict

## After All Agents Complete

Compile results into a summary table:

```
| # | Agent | Verdict | Findings |
|---|-------|---------|----------|
```

If any agent reports FAIL, list the findings and recommend fixes.
If all PASS, report the branch as clean.
