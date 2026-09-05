# GOAL — Spatial Execution & Data Plane 3.0

> Branch: `zcode/spatial-execution-data-plane-3` (worktree `exp-rs-execution-plane-3`, off `origin/master` @ bf26b74a34)

## Mission

Upgrade exp-rs from "reliable desktop DAG execution framework" to a
**High-Performance Spatial Execution & Data Plane 3.0** that stays
correct / bounded-memory / recoverable / cache-safe / provenance-safe /
responsive / observable under:

- very large rasters, multi-temporal stacks, many models
- high-concurrency DAGs, 100k+ assets, GPU inference, remote COG
- long workflows, high-frequency agent execution

## Ownership (files this epic may change)

`src/jobs/`, `src/workflow/`, `src/runtime/`, `src/data/`,
`src/processing/framework/`, `src/processing/algorithms/temporal/` (seams only),
`src/cli/`, `src/agent/` (thin seams), `tests/`, `scripts/`, `docs/`.

Off-limits except execution seams: `src/analysis/`, remote-sensing algorithms,
cartography/layout UI, Pi UX.

## Hard constraints

1. master is read-only; all work in this worktree/branch; staged commits; final PR.
2. No online CI/CD dependency: local build + ctest + bench + fault injection only.
3. Build resources capped: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `ctest -j2` (stress `-j1`).
   Never `-j$(nproc)` (16 logical CPUs / 62 GB RAM host, other epics co-running).
4. No full rebuilds of vendored QGIS/OTB/ITK; target + incremental builds; ccache.
5. Scientific correctness > hit rate. Cache default policy conservative (off unless
   determinism gate passes — matches existing ADR 0124 gating).

## Phases

A Benchmark Harness 2.0 → B Chunk Execution Graph (additive contracts) →
C Intermediate Materialization Elimination → D Artifact Store →
E Content-Addressed Cache → F Remote COG Tile Cache →
G Resource Scheduler 3.0 → H GPU Execution Plane → I Persistent Workspace Catalog →
J Persistent Workflow Runtime → K Worker Process Architecture →
L Observability → M Fault Injection / Chaos Lab →
Review passes 1-4 → master sync → push + PR.

Details: `PHASES.md`. Baseline: `BASELINE.md`. Risks/limits: `MEMORY_BUDGET.md`.

## Success = acceptance checklist (§26 of the goal)

worktree/branch ✓, master untouched ✓, baseline ✓, chunk contracts ✓,
materialization elimination on representative DAG ✓, ArtifactStore ✓,
cache upgrade ✓, remote COG cache ✓, richer scheduler ✓, GPU session plane ✓,
persistent catalog ✓, persistent workflow runtime ✓, local worker isolation ✓,
observability ✓, fault injection ✓, bounded memory ✓,
invariant tests (identity/destination/revision/cache-equivalence/provenance/
resume/GC) ✓, local build+test ✓, adversarial review ✓, staged commits ✓,
push ✓, PR ✓, no CI wait ✓.
