# ORACLES — flash-workflow-engine-12

Objective completion criteria (from track prompt) mapped to concrete, reproducible
checks. Every gate must pass TWICE consecutively before declaring done.

## O1 — IR version migration & round-trip
- vN-1 fixture (V1.0 ADR 0149 doc + a 2.x doc written at an earlier schema) loads and
  migrates to current IR; S(D(J)) == J byte-for-byte for canonical docs.
- Unknown future version (e.g. "99.0") is rejected with a typed, named error —
  not silently dropped or clamped.
- New fields added in this track round-trip losslessly (no silent drop).
- Evidence: `ctest -R workflow_ir` (targeted suite incl. new fixtures) x2.

## O2 — Minimal invalidation on param change
- Changing one node's parameter re-executes only that node + downstream dependents;
  independent branches keep CacheHit. Test asserts per-node executor call counts.
- Evidence: `ctest -R (workflow|pipeline)` targeted suite x2.

## O3 — Resume contract across process boundaries
- Interrupted / Failed / Canceled runs resume per contract both in-process and
  from a fresh process (checkpoint on disk). Verified with synthetic executors.
- Evidence: dedicated resume fixtures in the workflow test suite x2.

## O4 — No false CacheHit on tampered artifacts
- Replacing, editing, or moving an artifact out of the run dir (or a fingerprint/
  size/mtime mismatch) forces re-execution — never a served stale hit.
- Evidence: tamper fixtures in suite x2.

## O5 — Workflow + recovery/fuzz targeted gate, twice
- `ctest -R workflow` (or the named family list) with `QT_QPA_PLATFORM=offscreen`,
  `-j1`, `--output-on-failure`, run inside build dir — two consecutive green runs.

## Supporting checks
- `git diff --check` clean; `git status` shows no stray artifacts.
- Independent review: P0/P1 = 0 before PR.
