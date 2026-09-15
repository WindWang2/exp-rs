# TEST_MATRIX — temporal-intelligence-11

| # | Capability (pkg) | Independent oracle | Test file / command | exit | evidence |
|---|---|---|---|---|---|
| T1 | Trend-break vs seasonal-break attribution (A) | Closed-form synthetic series: step change → trend; amplitude jump → seasonal; phase shift → seasonal; both → both; no-change → none | `test_temporal_selection` (corpus scenarios) | — | — |
| T2 | Deterministic model selection + penalty semantics (B) | Hand-computed AICc/BIC on small exact series; candidate enumeration order fixed; tie→parsimony; CV folds deterministic | `test_temporal_selection` | — | — |
| T3 | Analytic CI coverage (C) | Exact linear series: CI must contain truth; width scales with σ; weights shrink width | `test_temporal_uncertainty` | — | — |
| T4 | Bootstrap CI determinism + failure flags (C) | Fixed seed → identical CI twice; >40% failed refits → NaN CI + flag | `test_temporal_uncertainty` | — | — |
| T5 | Auto multi-cycle + cross-year + refusal (D) | Two-cycle analytic series → 2 windows; Dec–May window → harvest-year assignment; <min samples → valid=false + reason, no metrics | `test_temporal_phenology_multi` | — | — |
| T6 | Shared region membership (E) | `extract_series` polygon output byte-equal to pre-refactor behavior on fixture; known-answer membership from region-table tests | `test_temporal_regions` + operator E2E | — | — |
| T7 | New operator surfaces end-to-end (A/B/D) | Synthetic GTiff stack via TestScene pattern; CSV/band known answers | `test_temporal_operators_10` (extended) or new E2E file | — | — |
| T8 | Bit-exact perf refactor (G) | Byte-stable rerun anchor + golden band hashes unchanged | `test_temporal_algorithms` determinism test | — | — |
| T9 | Negative controls (H) | No-change / flat / missing-heavy corpora → 0 breaks, valid=false flags, no crash | all new test files | — | — |
| T10 | Capability knowledge drift | Guard tests (capability catalog/knowledge) stay green with new operators registered | `test_capability_knowledge` (+ help catalog if pinned) | — | — |

Final gate: rows T1–T10 executed twice consecutively in Phase 8 (commands logged in EVIDENCE.md).
