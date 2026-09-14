# REVIEW_LOG — temporal-eo-phenology-change-10

Cross review (Phase 7): two read-only subagents over the full track diff.
Subagent A = architecture + scientific correctness; Subagent B = adversarial
test credibility + performance/resource + lifecycle. Findings merged and
de-duplicated; every disposition verified in code by the main agent.

| ID | Sev | Source | Location | Finding | Disposition | Evidence |
|---|---|---|---|---|---|---|
| P0-1 | P0 | B | rs_temporal_region_features_operator.cpp | missing per-region 4 M-pixel window guard (sibling extract_regions has it) → unbounded insideOffsets/window allocation | **FIXED** | guard copied verbatim; test build green |
| P1-1 | P1 | B | temporal_change.cpp final refit | no-fit segments reported slope/intercept 0.0 into slope_first/slope_last bands | **FIXED** | !ok branch sets NaN; harmonic-breaks E2E green |
| P1-2/F1 | P1 | A+B | temporal_calendar.cpp node mapping | "average" was recency-biased iterated midpoint (¼,¼,½ for 3 obs) | **FIXED** | running mean over observedCount in double |
| P1-3/F13 | P1 | A+B | rs_temporal_region_features_operator.cpp | sidecar write unchecked + written after CSV commit → corrupt/missing sidecar on disk-full | **FIXED** | sidecar flushed & byte-count verified BEFORE csvGuard.commit; both committed together |
| P1-4 | P1 | B | tests/test_temporal_calendar.cpp gap-split | assertions could not catch a maxGapNodes regression (vacuous) | **FIXED** | split nodes pinned NaN + side values pinned ≈1.0/≈2.0 |
| P1-5 | P1 | B | tests (monitor T-1) | test never exercised the schema fix it names | **FIXED** | schema pin: `scenes` property exists; required == {output, method} |
| P1-6/F5 | P1 | A+B | phenology cycles=2 | headline dual-cycle feature had zero behavioral coverage | **FIXED** | kernel known-answer test (complementSeasonWindow + phenologyCyclesPerYear, wrapped windows, sparse gate) + operator E2E (15 bands, c2_* names, cycle_count=2, c2 SOS in complement doy range). The new E2E immediately caught a REAL bug: the band write loop emitted only the first 7 bands (fixed: loop bound bandCount) |
| F2 | P1 | A | temporal_change.cpp totalSseFor | pruning objective scored unfittable segments as SSE 0 → spurious breaks retainable | **FIXED** | unfittable configuration returns +∞ (never preferred) |
| F3/P3-2 | P2 | A+B | rs_temporal_harmonic_breaks_operator.cpp minSegment | std::clamp lo>hi UB for 4-5 scene series | **FIXED** | hi = std::max(3, sceneCount/2) |
| F4/P2-3 | P2 | A+B | temporal_region_table.cpp id parsing | non-string id threw jsoncpp LogicError instead of typed InvalidParameter | **FIXED** | isString() guard before asString() |
| P2-2 | P2 | B | CSV writers | region ids with , " newline corrupt tables | **FIXED** | parseRegionsJson refuses such ids (typed, house style) |
| P2-1 | P2 | B | regularize/harmonic_breaks guards | 2 GiB guard omitted own output buffers (~2-4x understated) | **FIXED** | tileFloatsPerPixel = 2·scenes + outputs (+ estimateWorkingSetBytes corrected) |
| P2-5 | P2 | B | regularize per-pixel loop | no cancel checkpoint in heavy loop | **FIXED** | throwIfCancelled every 4096 pixels |
| P2-4 | P2 | B | ARCHITECTURE_V3.md | "independent of image size" overstated | **FIXED** | reworded to O(R)+O(total region-window pixels) with the per-region cap |
| F11 | P3 | A | phenology/features cycles=2 | full-year window complement duplicates cycle 1 silently | **FIXED** | logWarning in both operators |
| F12 | P3 | A | phenology season2 params | half-specified window silently ignored | **FIXED** | typed InvalidParameter when exactly one of season2 doy > 0 |
| F10 | P3 | A | temporal_calendar.h | whittaker epsilon misquoted (1e-4 vs 1e-5) | **FIXED** | 1e-5 for whittakerSmooth; 1e-4 attributed to whittakerSmoothRobust |
| F15 | P3 | A | temporal_calendar.cpp comment | final-segment comment named wrong mechanism | **FIXED** | comment now cites the lastFiniteDay refusal |
| P3-3 | P3 | B | duplicate instants | exact-hit picks the LATER duplicate (keep_all) | **FIXED** | firstAtInstant walks to the earliest finite sample; known-answer test added |
| P3-6 | P3 | B | buildRegularCalendar | grid materialized before the 2000-point operator guard | **FIXED** | >1e6-point spans return empty before allocating |
| P3-7 | P3 | B | CsvOutputGuard | guard bound before open → deleted pre-existing file on open failure | **FIXED** | guard binds after successful open (both CSV operators) |
| P3-8 | P3 | B | RegionDateReducer::endDate | sort unclamped against mis-declared counts | **FIXED** | defensive clamp in endDate |
| F7 | P3 | A | temporal_change.cpp | dead break-budget guard (>= maxSeg) | **FIXED** | corrected to >= maxSeg - 1 with comment |
| F8 | P3 | A | temporal_change magnitude | magnitude used adjacent fitted samples, not both models AT tBreak | **FIXED** | per-segment coefficients stored; magnitude = \|fitL(tBreak) − fitR(tBreak)\| |
| F9 | P3 | A | pruning comment | claimed "same rule as forward split" (different denominator) | **FIXED** | comment documents the total-SSE denominator as deliberate |
| F14 | P3 | A | temporal_fit.h robust wording | "iterations of IRLS" off by one | **FIXED** | header reworded (maxIter total solves; loop bounded) |
| F6 | P2 | A | pointInPolygon/mapToPixel duplicated | comment claimed "shared" | **ACCEPTED with cross-ref** | comment now states the textual-twin status + sync duty; hoisting into a detail header is the recorded follow-up (would touch extract_series beyond this track's surgical scope) |
| P3-4 | P3 | B | monthly calendar test | `>= 4` admits off-by-one at range end | **ACCEPTED** | exact count pinned via the monthly known-answer (grid[0..3] dates); the k±2 guard window is covered by the [1,366]-complement and clamp cases |
| P3-5 | P3 | B | median_budget_mb | unvalidated range | **FIXED** | setRange + typed refusal outside [0, 65536] |
| P3-9 | P3 | B | benchmark naming | region_reduce measures the accumulate loop | **ACCEPTED** | PERFORMANCE.md scopes the measurement explicitly ("reduce time", "I/O dominates separately"); renaming the artifact key would break comparability with the pinned evidence |
| — | P3 | B | unused epochQDate (P3-1) | dead variable | **FIXED** | removed |

## Post-fix verification

- All 4 new suites green: calendar 84 assertions / change 54 / regions 46 /
  operators E2E 418.
- Baseline temporal regression green: core 367, fit 162, algorithms 791,
  workspace 267, agent_tools 234, spatiotemporal_contracts 103.
- P0/P1 count after fixes: **0**. Accepted debts: F6 (dup kernel, cross-ref),
  P3-4 (partial pin), P3-9 (benchmark scope) — reasons above.

## Cross-review environment note

- During the build verification the host ran concurrent 10.0-track builds
  (load ~16); two GCC internal compiler errors (different vendored-QGIS
  files each run) disappeared on an unchanged-source `-j1` retry — judged
  load-induced, not code defects.
