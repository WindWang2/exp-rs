# PR_BODY — temporal-intelligence-11

(filled in Phase 8; skeleton now)

## Summary

Temporal Phenology & Change Intelligence 11.0 — seasonal-component break attribution, bounded
per-segment model selection, fit uncertainty (analytic + bootstrap CI), phenology 2.0
(auto multi-cycle, cross-year, quality flags), shared region membership, region surfaces
(dialog + agent tool), scratch-reuse performance refactor, deterministic synthetic corpus tests.

## Baseline

- origin/master at branch creation: `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`

## Dedupe / parallel-track ownership

- Open PR #1008 (radiometric/spectral): zero temporal-file overlap; shared CMake/.gitignore edits kept append-only.
- Concurrent local branches (insar/cn-eo-physics/execution-runtime/spectral): no temporal overlap (verified by diff).
- D18/#991 and D19/#992 merged into the baseline; D18-owned mission-mount files untouched.
- Open issues #1001–#1007: dataset/workflow/georef/io/agent — out of scope, not touched.

## Delivery / evidence

See `.planning/temporal-intelligence-11/` (CAPABILITY_MATRIX, TEST_MATRIX, EVIDENCE, PERFORMANCE, REVIEW_LOG, DECISIONS).

## Local evidence only; no online CI dependency
