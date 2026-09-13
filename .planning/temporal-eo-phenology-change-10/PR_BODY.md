# feat(temporal): Temporal EO / Phenology / Change Intelligence Platform 10.0

**Baseline**: `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55` (recorded in
`.planning/temporal-eo-phenology-change-10/BASELINE.md`; rebase-verified current at PR time).
**Branch**: `zcode/temporal-eo-phenology-change-10` · worktree `../exp-rs-temporal-eo-phenology-change-10`.
**Local evidence only; no online CI dependency** — every capability claim below maps to a local
build/test/benchmark command + exit code in `.planning/temporal-eo-phenology-change-10/EVIDENCE.md`.

## Problem

The temporal operator family (Temporal 1.0–3.0, PRs #712/#732/#733/#738/#742) shipped streaming
multi-temporal kernels over the ADR 0125 workspace, but four platform gaps — registered in
`ISSUES.md` and re-verified open at the baseline SHA — blocked a real time-series modeling
platform: `rs:temporal_monitor` refused inline `scenes` (T-1); no operator could re-cast an
irregular series onto a regular calendar (T-2); nothing modeled seasonal + trend regime changes
jointly (T-3); extraction handled exactly one ROI per call (C-2). No temporal output formed an
ML-consumable artifact (G).

## Architecture (docs/adr/0148, docs/temporal/ARCHITECTURE_V3.md)

Three new pure kernels + one shared detail header in `sicnu::temporal`, four thin operators on
the existing `prepareTemporalRun` seam, additive extensions to three existing operators:

| Layer | Deliverable | Closes |
|---|---|---|
| `temporal_calendar` | irregular -> regular calendar (day/monthly cadence anchored on the collection epoch); nearest / window_mean / linear / whittaker-defined-on-the-grid; `maxGapNodes` bridging guard; **no method extrapolates past the observed span**; per-point `validObservations` + `filled` provenance | T-2 |
| `temporal_change` | greedy seasonality-adjusted trend-break segmentation with per-segment harmonic + linear-trend refit, backward pruning (unfittable = +inf), break magnitudes as abs(fitL(tBreak) - fitR(tBreak)) via stored per-segment models, disturbance onset/recovery semantics | T-3 |
| `temporal_region_table` | multi-region parsing (typed refusals: duplicate/CSV-unsafe/non-string ids), pixel-center polygon membership (twin of extract_series semantics), streaming per-date Welford reducer with bounded exact median | C-2 |
| `temporal_linalg_detail` | dense solver extracted from temporal_fit.cpp (no second copy) | - |

**Operators**: `rs:temporal_regularize` (T-2; valid_count + filled_count bands),
`rs:temporal_harmonic_breaks` (T-3; break day/magnitude/slope bands + onset/recovery),
`rs:temporal_extract_regions` (C-2; region x date CSV, streaming by date — each scene read
once, O(regions) state, 4 M-pixel per-region guard), `rs:temporal_region_features` (G; typed
per-region ML feature table + versioned schema sidecar `exp_rs_temporal_region_features/1` for
label joins by `region_id`).

**Extensions**: monitor accepts `scenes` (T-1 — schema pin + run seam already shared);
`whittaker_robust` (IRLS Cauchy) in smooth; `cycles=2` double-cropping phenology (`c2_*` +
`cycle_count` bands; per-year cycle metrics via `phenologyCyclesPerYear`).

## Honesty rules enforced in code

- The joint model is described everywhere as "BFAST/CCDC-*inspired* greedy segmentation", never
  as BFAST/CCDC (no iterative alternation, no L1/model selection).
- Synthetic values are always distinguishable: `filled` flags, `filled_count` bands,
  `emptyCells` accounting, `medianEnabled` degradation.
- Determinism grades: bit-exact (nearest/window_mean/linear, reducer, change model) vs
  tolerance (whittaker 1e-5, robust 1e-4) — per-operator metadata matches the kernels.

## Compatibility

- Descriptor schema unchanged (new kernels are derived, not persisted); `cycles=1` phenology
  output is byte-identical to baseline (7 bands, same names); smooth keeps its method enum with
  one appended value; monitor's `required` shrinks from `{collection,output,method}` to
  `{output,method}` — `parseCollection` already refused neither-present (baseline behavior).
- Shared files touched append-only: `rs_operators_init.cpp` (4 registration lines + 4 includes),
  both CMakeLists (source lists), `data/agent/capabilities/temporal.json` (4 extends-entries),
  4 new `data/processing/algorithm_meta` sidecars, `.gitignore` (track whitelist).

## Tests (all local, `QT_QPA_PLATFORM=offscreen`, serial)

- New: `test_temporal_calendar` (84 assertions/10 cases), `test_temporal_change` (54/6),
  `test_temporal_regions` (46/4), `test_temporal_operators_10` (418/7 — regularize known
  answers, deforestation-like break localization, monitor T-1 schema+run, region CSV contract,
  feature table + sidecar, dual-cycle phenology).
- Baseline regression: core 367, fit 162, algorithms 791, workspace 267, agent_tools 234,
  spatiotemporal_contracts 103, sar_temporal_stats — all PASS.
- The new dual-cycle E2E caught and fixed a real bug pre-review: the phenology band write loop
  emitted only the first 7 bands.

## Performance / resource (evidence, never gates)

`benchmark_temporal10` (full tier, schema `exp.bench.temporal10.v1`): regularize 19.1k series/s
(1000 obs -> 16-day calendar); harmonic_breaks 1227 fits/s (500 samples; ~0.8 ms/pixel, the
documented dense-fit cost); region reducer 6.2M samples/s at the 100k-region target (~1.6 s
reduce time per 100 dates); phenology cycles 4.1k series/s (10 y x 2 cycles). Memory: tile
working-set guards count inputs AND outputs; per-region windows capped; median scratch bounded
with declared degradation.

## Review findings (Phase 7, 2 read-only subagents)

Full log with dispositions: `.planning/temporal-eo-phenology-change-10/REVIEW_LOG.md`.
All P0/P1 **closed**: missing region-features window guard (P0), NaN-honesty for unfitted
segments, recency-biased whittaker node folding, sidecar/CSV commit atomicity, three
test-credibility gaps (vacuous assertions -> pinned behaviors), pruning objective (unfittable =
+inf), guard arithmetic, duplicate-instant tie-break, plus ~15 P2/P3 hardenings. Accepted debts
(3, with reasons in the log): duplicated pixel-membership kernel (cross-ref comment, hoisting
is a follow-up), partial monthly-calendar pin, benchmark measures the accumulate loop (scoped).

## Known limitations

- Break days resolve at acquisition spacing; harmonics use a fixed 365.25-day period; the
  change model detects trend-component breaks (seasonal-component breaks are a follow-up).
- Region rows are emitted only for region x date cells with >= 1 valid sample (`emptyCells`
  reports the rest); median beyond its budget degrades to NaN (`medianEnabled=false`).
- `phenologyCyclesPerYear` needs >= 3 valid samples per year x cycle window (else that cycle is
  invalid for the year, reported, never guessed).
- Pre-existing baseline failure (untouched by this track, app-layer ownership):
  `test_temporal_scene_model` QA-string rendering — recorded in EVIDENCE.md OUT_OF_SCOPE.

## Follow-ups

Hoist the point-in-polygon kernel into a shared detail header; per-segment model selection;
seasonal-component break detection; dataset-foundry ingestion of the region-feature artifact;
GUI dialog for the region table; hoist fitSegment scratch buffers if harmonic_breaks tile
costs matter on production rasters.

## Verification snapshots (final HEAD, after rebase to `origin/master`)

- `git diff --check origin/master...HEAD` -> clean (exit 0).
- Conflict-marker scan over changed files -> none. Secret scan -> only baseline-pre-existing
  CHANGELOG text (same count at baseline and HEAD; none in this track's sections).
- Runbook existence assertions (skills/docs/planning references) -> no MISSING.
- All suites re-run green on the final HEAD commit (see EVIDENCE.md Phase 8).
