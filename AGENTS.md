# Helianthus PIC eBUS Adapter Firmware

## Repository status and purpose

> **DEPRECATED — historical deterministic PIC firmware and validation reference.**

This repository preserves clean-room PIC16F15356 firmware, its ENH/ENS adapter
protocol model, deterministic-timing checks, and historical validation/oracle
material for the Helianthus eBUS adapter v3.x. It is retained for study,
regression analysis, and reproducible validation of the archived design.

It is not an active product-development repository. Do not start new production
firmware, hardware, deployment, or feature work here by default. Any exception
must be explicitly scoped by the operator before implementation begins.

## Ownership boundaries

- This repository owns the archived PIC firmware, bootloader model, host-side
  simulation, deterministic checks, and their tests.
- Keep eBUS adapter framing, timing, and PIC-specific implementation details
  here. Do not introduce gateway, consumer, vendor, credential, network, or
  private-laboratory dependencies.
- Treat recovered behavior as evidence-bound: distinguish implemented and
  tested behavior from inference or hardware-unverified assumptions.
- Preserve valid state on partial failures when the relevant module supports
  field-level retention; do not replace last-known-good state wholesale.

## Normal documentation and code workflow

1. Reconcile `origin/main`, local changes, and open issues/PRs before work.
2. Create one scoped issue and an `issue/<id>-<slug>` branch from current
   `main`; never alter another contributor's branch or PR.
3. Keep changes within the issue acceptance criteria. Use focused tests for
   documentation-only work and RED-first evidence for behavior, protocol,
   persistence, recovery, concurrency, or safety changes.
4. Run all applicable checks listed below before opening a PR.
5. Open a linked PR recording commands, results, scope, and residual risk.
6. Resolve P0-P2 findings, then obtain a fresh exact-HEAD
   `NO_BLOCKING_FINDINGS` review verdict.
7. Squash merge only after applicable checks are green and blocking review
   findings are resolved. Verify remote `main` after merge.

## Firmware invariants

Preserve the deterministic constraints documented in `DETERMINISM.md`:

- no recursion, dynamic allocation, variable-length arrays, or floating point;
- bounded loops and bounded ISR-context work;
- PIC16 call-stack, RAM, ring-buffer, and dispatch-table constraints remain
  enforced by the repository checks;
- use hardware timers rather than blocking delays on timing-critical paths;
- do not weaken protocol parsing, bounds checks, CRC/error handling, or oracle
  parity merely to simplify an implementation.

Changes to protocol behavior or timing require proportionate deterministic,
oracle, and target-aware validation. Do not claim physical-device equivalence
from host-side simulation alone.

## Validation

Host-side validation requires a C11 compiler, Python 3, and `cppcheck` for the
full determinism suite:

```bash
make test
make check-all
bash tests/test_checks.sh
git diff --check
```

`make test` builds and runs runtime, adapter-protocol, firmware-oracle, and
bootloader-oracle tests. `make check-all` enforces the enabled deterministic
constraints. Optional XC8-oriented checks remain target-specific and must be
reported separately when relevant.

`make oracle-check` is an additional historical cross-project comparison. Run
it only when its external oracle prerequisite has been deliberately provided
for the scoped task; it is not a baseline checkout requirement and must not
make ordinary repository validation depend on a sibling checkout.

Run any repository CI checks available on the PR and report their exact state.
The project does not declare the workspace-wide T01..T88 transport matrix;
use this repository's deterministic, test, and oracle evidence instead.

## Hardware and release safety

Never flash a PIC, connect to a live eBUS installation, alter bootloader/EEPROM
state, or perform any live-hardware action without explicit operator
confirmation at action time. Keep credentials, captures, serials, network
coordinates, and private lab details out of git and out of public issue/PR
content.

This repository has no default release, deployment, or hardware-validation
track. Stop and request direction if proposed work would cross that boundary.
