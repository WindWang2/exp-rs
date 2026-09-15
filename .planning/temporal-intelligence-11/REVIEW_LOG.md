# REVIEW_LOG — temporal-intelligence-11

## Self-review (main agent, full diff origin/master...HEAD)

Pre-review pass found and fixed before the independent review: F-test numerator signs
(flipped restricted/unrestricted SSEs — caught by the corpus tests, fixed by the symmetric
nested design), −inf scores being disqualified by an isfinite guard, over-strict attribution
tolerances, E2E band-index groupings, leap-year DOY misalignment in E2E fixtures, and the
circular-doy window representation (replaced by time-range windows).

## Independent adversarial review (subagent #2, read-only, full diff)

13 findings. Dispositions:

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P0 | pi/knowledge pages regenerated but uncommitted; probe debris in tree; commit message over-claimed | **FIXED** — pages committed (see drift commit), probe files deleted, wording corrected here |
| 2 | P1 | compute_ci wrote break-1 CI into every slot + maxBreaks× redundant bootstrap | **FIXED** — bootstrap hoisted per pixel; per-break ordinal statistic; per-(pixel,break) seeds; breaks beyond the refit count are NaN |
| 3 | P2 | phenology: peaks-but-no-proposals → valid=false with reason=null | **FIXED** — new stable code "edge_truncated_series" |
| 4 | P2 | cycle cap applied per peak-year, reported per harvest-year | **FIXED** — authoritative clamp at harvest-year index assignment (cycleIndex ≥ maxCyclesPerYear unscored), documented in header |
| 5 | P2 | trendMagnitude doc said "NaN when untestable" but keeps segmentation jump | **FIXED** — doc corrected (segmentation jump kept deliberately: informative even when the nested test cannot run) |
| 6 | P2 | bootstrap refusal left bounds 0.0; low_success_rate misused for structural failures | **FIXED** — bounds NaN on all refusal paths; new "invalid_input" code for structural failures |
| 7 | P2 | TEST_MATRIX overclaims (no hand-computed AICc, no harvest-year assertion, no byte-equal fixture, REVIEW_LOG pending) | **FIXED** — added decemberPeak scenario with seasonYear-2022 assertion; added independent AICc recomputation test; matrix amended to name the actual anchors (test_temporal_algorithms determinism rerun); this log filled |
| 8 | P2 | region_table "textual twin" comment pointed at the deleted copy | **FIXED** — comment now states single authority |
| 9 | P3 | NaN weights passed `w <= 0` guard, poisoning Gram | **FIXED** — `!(w > 0.0)` rejects NaN and non-positive weights |
| 10 | P3 | tile budget undercounts (2×sceneCount vs 3; 4 vs 6 per cycle) | **FIXED** — multipliers corrected to 3·scenes / 6·cycles |
| 11 | P3 | seed-decorrelation wording; dead merge pass | **FIXED** (wording documents tile-local scope + per-break streams; merge pass kept as defense-in-depth with the dominance filter) |
| 12 | P3 | "allocations go away" overstated (solver still copies by value) | **FIXED** (comment now says Gram/solution allocations go away; solver copies remain — in-place elimination is a recorded follow-up); PERFORMANCE.md amended |
| 13 | P3 | doc nits: seasonal-only fStatistic, schema band order, valid semantics, uniform_real_distribution caveat, medianSpacing fallback | **FIXED** in headers/schema/corpus docs |

## Control evidence for pre-existing failures (not this PR's debt)

- `test_capability_drift`: 3 failures at HEAD vs 4 with master's data tree (control: stashed
  data/ changes, reran) — remaining: cartography spatial tools uncovered, io:/mnf/spectral
  operators uncovered, a recipe-seeding test. None temporal.
- `test_help_coverage`: 12 failures — `workflow.new/open/save/run` shell commands without
  knowledge entries, `rs_glossary.json: invalid id ''`, composition errors listing the same
  resources. None reference temporal.
