# GOAL — Dataset / Experiment / Reproducibility Platform 7.0

Track: `feat/dataset-experiment-7`
Worktree: `C:\Users\wangj.KEVIN\projects\exp-rs-dataset-experiment-7`
Base: master @ `2041f6fa` (merge of PR #821, includes #818/#822 Foundation 6.0 fixes)

## Mission

Promote the established Dataset/Experiment Foundation (PRs #770, #818) from a
persistence library into a research data platform that end-to-end connects:
sample production → train/val/test splitting → experiment execution →
evaluation → comparison → reproducibility.

Completion definition (from the goal brief): users and the Harness can answer —
which dataset version produced this result, which samples, how it was split,
whether leakage was audited, which model/params/environment produced it,
whether two experiments are truly comparable, and whether a run can be replayed.

## Hard constraints (from the goal brief)

- Max 2 subagents total (read-only analysis/review preferred).
- master is read-only; all work in this worktree/branch.
- Local build/test evidence only; no CI/CD dependency. Build parallelism ≤ 2,
  CTest parallelism 1.
- No duplicate development: anything already in master is extended, never
  rebuilt (see BASELINE.md).
- No second agent foundation, no second scheduler, no second map engine.
- FAIL is never wrapped as success; unvalidatable ⇒ warning/unknown.

## Deliverable sections (A–G of the brief)

- A: Dataset/Experiment MCP surface (thin tools over authoritative stores)
- B: Pipeline → SampleRecord promotion (classification/segmentation/annotation)
- C: TaskCenter/Workflow → ExperimentRun authoritative run records
- D: Fold-level split & leakage (per-fold audit, comparability, replay)
- E: Dataset quality at scale (indexed facets, bounded memory, 100k→1M)
- F: Reproducibility 7.0 (replay readiness, diagnostics, historical comparison)
- G: Experiment comparison (protocol/schema compatibility, paired runs)
