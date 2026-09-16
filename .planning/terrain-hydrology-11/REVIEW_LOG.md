# REVIEW_LOG — terrain-hydrology-11

## Phase 7 — independent adversarial review (subagent #2, read-only, 2026-09-16)

Scope: full `origin/master...HEAD` diff (191 files). Science preamble verified by the
reviewer: D∞ facet algebra (α/β wedge basis, |w|², w·e₁ = s₁) is Tarboton-1997-exact;
receiver exit-point ray/segment intersection correct; shadow carry recurrence is
shift-invariant with test-before-carry ordering; Spencer coefficients + acos azimuth
with afternoon mirror correct; TPI integral images exclude the centre consistently;
geomorphon packing/arc counting sound; viewshed predecessor guarantees hold for r ≥ 2;
sidecars match live schemas; test oracles genuinely independent.

Verdict: **P0 = 0, P1 = 1, P2 = 3, P3 = 8.**

| # | Sev | Location | Finding | Disposition |
|---|---|---|---|---|
| 1 | P1 | rs_terrain_flow_operator.cpp | flat_resolve / flow_direction_inf surface cancellation as ComputationError (contract: Cancelled); e2e cancel test only covered viewshed | **FIXED** — `context.throwIfCancelled()` re-check before ComputationError in both branches; e2e cancel coverage extended to `flat_resolve` (pre-cancelled run throws + leaves no output). |
| 2 | P2 | rs_terrain_flow_operator.cpp | `undecidedCells` counted NoData passthrough as undecided when the sentinel is negative | **FIXED** — counts only true −1 cells (NoData/NaN excluded); doc updated. |
| 3 | P2 | rs_terrain_landform_operator.cpp | memory estimate/guard not scaling with scale count (peak ≈ 24 + 8·S B/cell; 16 scales on 2²⁸ cells ≈ 34 GB slipped through) | **FIXED** — dynamic estimate parses `radii` and uses 24 + 8·count B/cell; static anchor 48 B/cell kept; metadata limitation reworded. |
| 4 | P3 | rs_terrain_landform_operator.h | header claimed the packed ternary pattern is exported (it is not) | **FIXED** — header corrected (pattern computed in-kernel, not exported; form histogram in result JSON). |
| 5 | P3 | tests/test_terrain_solar.cpp | stale comment contradicted the (correct) assertions | **FIXED** — comment matches the west-of-wall geometry. |
| 6 | P3 | tests/test_terrain_landform.cpp | wrong hand constant (stdTPI_outer ≈ 2.15; correct ≈ 1.29) | **FIXED** — comment corrected; assertion unchanged and still valid. |
| 7 | P3 | terrain_solar.h | header said Cooper 1981, code/docs say Spencer 1971 | **FIXED** — header updated to Spencer. |
| 8 | P3 | terrain_hydrology.cpp | epsilon comment claimed 2⁶ ulp margin; actual 2³ | **FIXED** — comment corrected (margin still comfortably above absorption). |
| 9 | P3 | terrain_landform.h | header ridge/valley arc wording stricter than the code ("one arc" vs "arcs only") | **FIXED** — header reworded to match the implementation ("− arcs only, no +"). |
| 10 | P3 | rs_terrain_viewshed_operator.cpp | product=viewshed silently dropped extra observers; visibleFraction denominator differed from the agent tool | **FIXED** — >1 observer on viewshed → InvalidParameter (use cumulative); fraction denominator = analysed (non-NoData) cells on both surfaces + `analysedCells` exported; docs updated. |
| 11 | P3 | rs_terrain_flow_operator.cpp | stream_network/outlets masks wrote 0 on DEM-NoData while declaring the sentinel | **FIXED** — NoData cells carry the sentinel (#783 spirit); docs updated. |
| 12 | P3 | terrain_viewshed.cpp / terrain_landform.cpp + two operators | duplicated nearest-cell LoS march and cell-pair parsing (two places for sampling decisions to drift) | **WONTFIX (follow-up)** — behavioural-parity risk too close to the PR; hoisting a shared `marchCells(...)` helper + shared cell-pair parser recorded as the first follow-up. |

Post-fix regeneration: `capability_knowledge_tool gen-meta` (135 sidecars) + `gen-pages`
(zero diff) after the landform metadata reword; test_capability_knowledge 12/12 PASS.

Final gate: all 8 terrain suites + capability suite green, run twice back-to-back on the
final tree (see EVIDENCE.md Phase 8).
