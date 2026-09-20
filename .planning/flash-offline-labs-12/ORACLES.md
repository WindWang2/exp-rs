# ORACLES — flash-offline-labs-12

Objective completion conditions from the track file, made checkable. Each oracle must be run twice
consecutively (clean pass both times) before completion is declared.

## O1 — Full-chain offline lab execution
**Condition:** At least several representative lab specs execute generate → run → grade → report
fully offline (no network).
**Verify:** for ≥3 representative labs (different modalities: raster-stats, classification,
change/temporal): run the track's E2E driver in a temp dir with network-disabled seam; exit 0,
report artifact exists, grade verdict present.
**Command:** `scripts/run_lab_e2e.sh --lab <id> --offline <tmpdir>` (new track-owned driver) —
refine after census.

## O2 — Determinism matrix, two-pass stable
**Condition:** Same seed generate+verify twice → byte-equivalent outputs; full↔subset switching
leaves no stale owned files (files not in the current manifest are pruned or reported).
**Verify:** generate scene set with seed S twice into two dirs; compare SHA256 manifests identical;
then regenerate a subset over the full dir; verify reports exactly the stale files and prune removes
them.
**Command:** `tools/sample_foundry generate ... && verify` ×2 + `tests/test_sample_fixtures` new cases.

## O3 — Malicious/broken grading rules → typed validation error
**Condition:** Grader validates rules before executing; malformed/malicious rules (unknown type,
out-of-range bounds, type confusion, path traversal, absurd sizes) return a typed validation error;
grader never crashes / no OOB.
**Verify:** fixture suite of bad rules, each yielding structured error `{code, path, message}`;
grader exit code non-zero but process clean (no signal); Catch2 tests fail-before/pass-after.

## O4 — Batch classroom isolation
**Condition:** Dozens of submissions run isolated with timeout + concurrency cap; a timed-out or
corrupt single submission never blocks others; CSV/JSON report counts match inputs.
**Verify:** batch fixture with N submissions incl. 1 hang (killed by timeout), 1 corrupt, 1 failing
grade: all N appear in report with correct statuses; wall time bounded; report row count == N.

## O5 — Cross-platform launcher parity
**Condition:** Windows cmd/PowerShell and POSIX launchers agree on argument parsing (spaces,
Unicode, empty args, `--out=` style), and the real exit code propagates unwrapped.
**Verify:** POSIX fixtures executed here; Windows behavior locked by (a) shared parse logic or
(b) fixture transcripts + reviewed-by-construction script tests; documented in bundle README.
No swallowed exit codes: wrapper exit == child exit for failure fixtures.

## Non-goals guard
- No real student data in repo; no online license/service dependency; no grading-rule loosening.

## Evidence discipline
Every oracle run is logged in `.goal-loop-ledger.md` with command + exit + key numbers.
