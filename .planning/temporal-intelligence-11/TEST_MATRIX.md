# TEST_MATRIX — temporal-intelligence-11

| # | Capability (pkg) | Independent oracle | Test file / command | exit | evidence |
|---|---|---|---|---|---|
| T1 | Trend-break vs seasonal-break attribution (A) | Closed-form synthetic series: step change → trend; amplitude jump → seasonal; phase shift → seasonal; both → both; no-change → none | `test_temporal_selection` (corpus scenarios) | — | — |
| T2 | Deterministic model selection + penalty semantics (B) | Independent AICc recomputation from the reported RSS (epsilon 1e-9); candidate enumeration order fixed; tie→parsimony (−inf exact-zero-RSS rule); CV folds deterministic | `test_temporal_selection` | — | — |
| T3 | Analytic CI coverage (C) | Exact linear series: CI must contain truth; width scales with σ; weights shrink width; NaN weights refused | `test_temporal_uncertainty` | — | — |
| T4 | Bootstrap CI determinism + failure flags (C) | Fixed seed → identical CI twice; different seed moves bounds; >60% failed refits → NaN CI + "low_success_rate"; structural failures → "invalid_input"; NaN bounds on every refusal | `test_temporal_uncertainty` | — | — |
| T5 | Auto multi-cycle + cross-year + refusal (D) | Two-cycle analytic series → 2 windows; December-peaking season (peak doy ~360 of year Y) reported under seasonYear Y+1 with POS ≈ 360 — the harvest-year assignment; <min samples → valid=false + reason, no metrics; all-edge-peak series → "edge_truncated_series" | `test_temporal_phenology_multi` | — | — |
| T6 | Shared region membership (E) | Membership predicate parity verified by implementation reading (even-odd pixel centers, map coords); pre-refactor behavior guarded by the green `test_temporal_regions` + `test_temporal_operators_10` extract_series fixtures | `test_temporal_regions` + `test_temporal_operators_10` | — | — |
| T7 | New operator surfaces end-to-end (A/B/D) | Synthetic GTiff stack via TestScene pattern; CSV/band known answers | `test_temporal_operators_10` (extended) or new E2E file | — | — |
| T8 | Bit-exact perf refactor (G) | Byte-stable rerun anchor (test_temporal_algorithms "identical reruns are byte-stable", 791 assertions) unchanged after the scratch-reuse refactor | `test_temporal_algorithms` | — | — |
| T9 | Negative controls (H) | No-change / flat / missing-heavy corpora → 0 breaks, valid=false flags, no crash | all new test files | — | — |
| T10 | Capability knowledge drift | Guard tests (capability catalog/knowledge) stay green with new operators registered | `test_capability_knowledge` (+ help catalog if pinned) | — | — |

Final gate: rows T1–T10 executed twice consecutively in Phase 8 (commands logged in EVIDENCE.md).
