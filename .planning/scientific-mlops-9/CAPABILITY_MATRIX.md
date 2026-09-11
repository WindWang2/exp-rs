# CAPABILITY MATRIX — 9.0 milestone scope vs. baseline `132da5e998`

Verified against code (headers + 8.0 planning evidence), not docs. Status:
Implemented / Partial / Missing / Refused-by-contract / verify-at-milestone.
This file is updated per milestone when verification refines a row.

## M0 — Split & leakage correctness

| Item | Status | Evidence / gap |
|---|---|---|
| 12 deterministic split methods | Implemented | `dataset_types.h` SplitMethod, `split.cpp` |
| #875 SpatialKFold blockSize validation | **Missing (bug)** | `SplitConfig::validate()` guards SpatialBlock only; `generateFolds` divides by 0.0 → UB |
| NaN parameter rejection (block sizes, buffer, ratios) | **Missing** | `blockSizeX <= 0.0` false for NaN; NaN buffer passes; negative ratios pass ratio-sum |
| float→int overflow guard in block grid (`qint64(floor(x/size))`) | **Missing** | tiny blockSize → quotient ≫ 2^63 → UB |
| Degenerate fold refusal (foldCount > samples/blocks/groups) | **Missing** | KFold with foldCount > N produces empty folds silently |
| Deterministic seed | Implemented | `DeterministicRandom::seedFor`, Strict grade default |
| Geographical block isolation / atomic blocks | Implemented | #775 fix: whole-block budget walk |
| No train/val/test overlap | Implemented by construction (one assignment per sample; duplicate ids refused) — regression suite verify-at-milestone | |
| Leakage report | Implemented | `leakage_audit.*`, 13 kinds, audited-checks list |
| Fold audit + replay verification | Implemented | `fold_audit.*` |
| Class distribution reporting | Partial | Stratified splits honor classes; an explicit per-role class-distribution report in split summaries — verify-at-milestone |
| Small/degenerate dataset refusal | Partial | zero-sample refused; 1-sample folds/degenerate grouped handled via refuse-or-repair — tighten in M0 |
| Cross-region / cross-year protocols | Partial | LORO/LOYO exist as folds; protocol-level descriptors (region-holdout as declared evaluation protocol) — M0/M5 |
| Spatiotemporal split (space × time joint isolation) | **Missing** | no joint method; M0 adds `SpatioTemporalBlock` |

## M1 — Dataset version DAG

| Item | Status | Evidence |
|---|---|---|
| Immutable version identity (draft→stage→commit→deprecate) | Implemented | `dataset_store.h`; ADR 0134 byte-stable commits |
| Parent/source lineage | Partial | parent id on manifest/fork; explicit lineage DAG queries — verify-at-milestone (`src/experiment/lineage.*` covers runs; dataset-side lineage verify) |
| Membership digest | Implemented | `dataset_fingerprint.*` |
| Diff between versions | verify-at-milestone | store facets — likely partial |
| Tags / reproducibility pins | Partial | run pins exist (8.0); dataset-version tags — verify |
| 100k+ scale | Implemented | `test_dataset_quality_scale` (100k samples) |

## M2 — Sample & annotation governance

| Item | Status | Evidence |
|---|---|---|
| Ontology / label schema | Implemented | `label_schema.*` |
| Provenance (human/field/model/weak/pseudo/external) | Implemented | `annotation.*`, `dataset_types.h` AnnotationSourceType |
| Review status | Implemented | AnnotationReviewStatus Pending/Approved/Rejected |
| Quality flags / external verification | Implemented | `dataset_quality.*` |
| Conflict/duplicate handling | Implemented (digest-based, honest) | store conflict diagnostics |
| Temporal validity | verify-at-milestone | sample temporal payloads exist; validity windows verify |
| No hidden mutation of pinned versions | Implemented | immutability contract + tests |

## M3 — Experiment lifecycle 9.0

| Item | Status | Evidence |
|---|---|---|
| Truthful lifecycle + monotonic transitions | Implemented (8.0) | bridge + store transition validation |
| Interrupted reconciliation (restart, same identity) | Implemented (8.0) | markInterrupted/markResumed, checkpoint evidence |
| Ghost/fabricated history rejection | Implemented (8.0) + store-side invariant tests extended in M3 (#876 data side) | |
| Terminal flush / crash recovery | Implemented (8.0, with documented race: rare ghost reported next reconciliation) — M3 tightens tests | |
| Bounded metadata | Implemented (≤256 steps) | |
| **CLI pipeline auto-record** | **Missing** (8.0 documented follow-up; verified absent) | M3/M5 |
| **resume recording args surface** | **Missing** | M3 |

## M4 — Automatic scientific evidence

| Item | Status | Evidence |
|---|---|---|
| Metrics/confusion matrix/per-class P/R/F1/IoU/kappa | verify-at-milestone | `evaluation.h` exists with rich surface — 8.0 called it implemented |
| Recorded-run auto evidence | Implemented (step evidence 8.0) — metric-level auto-compute verify | |
| Artifact digests, model/data/split/env identity | Implemented (pins 8.0) | |
| Metrics schema versioning | verify-at-milestone | |
| Spatial/temporal slice metrics, calibration summary | **Likely Missing** | M4 |

## M5 — Experiment matrix

| Item | Status | Evidence |
|---|---|---|
| Sweep descriptor (region × year × sensor × model × seed × preprocessing) | **Missing** | M5 new |
| Runs through existing execution chain | Implemented as the only path | no new scheduler |
| Grouping / aggregation / missing-run handling / pareto | **Missing** | M5 new |

## M6 — Comparison & diagnostics

| Item | Status | Evidence |
|---|---|---|
| Protocol/schema compatibility, paired metrics, non-comparable reasons | Implemented | `comparison_ext.*`, RunComparison |
| Identity diff (dataset/split/model), artifact diff, runtime diff | Partial (RunDiffItem exists) — extend | M6 |
| Evidence completeness / typed incomparability | Partial | M6 |

## M7 — Replay / reproduce

| Item | Status | Evidence |
|---|---|---|
| Reproduction bundle 2.0 (manifest/checksums/secrets/schema) | Implemented | `reproduction_bundle.*` |
| Replay readiness (Exact/Compatible/BestEffort/Impossible) | Implemented | `replay_readiness.*`; hooks caller-supplied |
| Fail-closed on missing identity | Implemented | readiness contract |
| Replay through existing execution chain + deviation report | Partial — verify MCP/CLI replay verb; deviation report verify | M7 |

## M8 — Promotion seam

| Item | Status | Evidence |
|---|---|---|
| Result → candidate evidence, criteria, approval metadata, benchmark sets | **Missing** | M8 new (via stable model catalog interface; NO new registry) |

## M9 — Scale & storage

| Item | Status | Evidence |
|---|---|---|
| 100k runs scale | Implemented | `test_mlops8_scale` (20k seeded; 9.0 extends) |
| Indexes/bounded queries/SQLite tx correctness | Implemented (store) — concurrent readers/writers verify | M9 |
| Export/import bundles | Implemented | bundle exporter |
| Corruption detection / interrupted process | Partial | M9 |
