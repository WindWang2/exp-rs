# FINAL REPORT — Dataset / Experiment / Reproducibility / Scientific MLOps 9.0

Branch: `feat/scientific-mlops-9`
Worktree: `/home/kevin/projects/rs-studio/exp-rs-scientific-mlops-9`
Base: origin/master `132da5e998`; rebased onto `f316dfdbb4`
(#853-#882 remediation wave that landed during development).

## Status: COMPLETE (implementation + verification + adversarial review)

## Baseline & problem statement

See BASELINE.md and ISSUE_TRIAGE.md (re-verified against the execution-time
master, not historical line numbers). The 8.0 track (PR #843) delivered the
execution→experiment bridge and documented exactly the follow-ups this
branch implements: CLI pipeline auto-recording, resume recording args, plus
the scientific-MLOps scope below.

## Milestones delivered (goal §6 mapping)

| M | Deliverable | Code | Verification |
|---|---|---|---|
| M0 | #875 fix + total split-config validation (NaN/Inf/negative/overflow), degenerate-fold refusals, SpatioTemporalBlock, bounded summaries | src/dataset/split.*, dataset_types.* | test_mlops9_split 285 ✓ |
| M1 | version DAG: write-time parent/cycle validation, ancestors/children, derive | dataset_store.* | test_mlops9_version_dag ✓ |
| M2 | sample temporal-validity windows (additive, backward-compatible) | sample.* | test_mlops9_version_dag ✓ |
| M3 | CLI auto-record + resume opt-in + truthful-failure E2E, no fabrication | src/cli/*, bridge adapter | test_mlops9_cli_record 38 ✓ (+ mlops8 suites green) |
| M4 | evidence projection w/ typed completeness + metrics schema versioning | src/experiment/evidence.*, evaluation.* | test_mlops9_evidence ✓ |
| M5 | bounded experiment matrix: descriptor/ledger/aggregation/pareto | experiment_matrix.* | test_mlops9_evidence ✓ |
| M6 | RunComparison artifacts+runtime dimensions | experiment_types.* | test_experiment_evaluation 162 ✓ |
| M7 | replay deviation verdicts (typed, fail-closed) | replay_deviation.* | test_mlops9_evidence ✓ |
| M8 | promotion evidence seam (no registry; append-only approval trail) | promotion.*, experiment_store.* | test_mlops9_evidence ✓ |
| M9 | 100k stress, bounded queries, concurrency, corruption, injected fault | test_mlops9_scale | 34 assertions ✓ |

## Issue mapping

- #875 (owned): fixed with the superset guard (finite-positive block sizes
  for ALL spatial grid methods + NaN + overflow + degenerate folds). The
  master-side minimal guard that landed via f316dfdbb4 remains a strict
  subset; no double-fix conflict.
- #876 data side: store invariants (no fabricated terminal history)
  verified by the mlops8 bridge suite (150 ✓) and the mlops9 CLI E2E
  (unknown-run resume records nothing).
- All other open issues (#848-#874, #877-#882): out of scope, owned by the
  parallel tracks (see ISSUE_TRIAGE); none touched by this branch.

## Test evidence (local; CI explicitly NOT awaited)

Post-rebase battery, all on this worktree (Ninja Release, GCC, -fpermissive
host note in OVERLAP_MAP):

| Suite | Result |
|---|---|
| test_mlops9_split | 285 assertions, 11 cases ✓ |
| test_mlops9_version_dag | 104 assertions, 5 cases ✓ |
| test_mlops9_evidence | 94 assertions, 4 cases ✓ |
| test_mlops9_cli_record (real CLI subprocess) | 42 assertions, 3 cases ✓ |
| test_mlops9_scale (RUN_SERIAL) | 34 assertions, 4 cases ✓ |
| test_split_leakage (regression) | 672 assertions, 20 cases ✓ |
| test_dataset_core (regression) | 155 assertions, 12 cases ✓ |
| test_experiment_evaluation (regression) | 162 assertions, 14 cases ✓ |
| test_mlops8_bridge (regression) | 150 assertions, 17 cases ✓ |
| test_mlops8_e2e (regression) | 74 assertions, 6 cases ✓ |
| test_dataset_quality_scale (100k, regression) | 733 assertions, 3 cases ✓ |

Performance evidence: PERFORMANCE.md (100k seeding 23.3 s; paged reads
57 ms/5 pages; measured cold-path ref scan with documented bound).

## Adversarial review

- Round 0 self-review: 5 findings fixed.
- Round 1, two read-only subagents (A: architecture/correctness/concurrency/
  scientific — 0 P0, 1 P1, 5 P2, 8 P3; B: tests/performance/portability/
  resources/docs — 0 P0, 1 P1, 6 P2, 7 P3): **all P0/P1/P2 fixed**; P3 fixed
  except two accepted-with-rationale items. Full findings and dispositions
  in REVIEW_LOG.md. The battery re-ran green after remediation.

## Known limitations / follow-ups

- Cold-path execution-ref scan is linear (documented; O(log n) needs a
  dedicated column+index — store-owner follow-up).
- CLI resume recording relies on refs previously recorded; a resume of an
  execution that was never recorded correctly records nothing.
- The GDAL 3.13 host-compat issue in src/geospatial (masked by
  -fpermissive) belongs to geospatial-data-fabric-9; noted in OVERLAP_MAP.
- Evidence projection reads the primary metric record only; multi-protocol
  evidence merging is future work.
