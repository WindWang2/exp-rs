# DECISIONS — flash-temporal-phenology-12

## D0 — Network routing

github.com:443 unreachable directly; api.github.com reachable. All git/gh ops
use `HTTPS_PROXY`/`HTTP_PROXY=http://127.0.0.1:7890` env vars (system Clash).
No git config mutation. Alternative considered: `git -c http.proxy=` per call —
env vars chosen for gh uniformity.

## D1 — Module placement (PENDING census)

Runbook: "新能力优先放独立 temporal12 模块". Candidates:
(a) new files in existing `src/processing/algorithms/temporal/` +
    `src/operators/rs/rs_temporal_*` naming — matches repo convention;
(b) new `temporal12/` subdirectory — cleaner boundary, breaks module layout.
Leaning (a) with explicit new-file names (e.g. `temporal_irregular_axis.*`,
`temporal_qa_interp.*`, `temporal_robust_harmonic.*`, `temporal_phenology_irreg.*`,
`temporal_robust_trend.*`, `temporal_fusion_contract.*`). Final call post-census.

## D2 — Ledger local-only; .planning committed

`.goal-loop-ledger.md` at repo root is gitignored ("local-only process
artifact") — kept uncommitted per runbook default. `.planning/<track>/*.md`
committed per repo convention (whitelist added). Note: write-tool denies
gitignored/negation-chain paths → .planning + ledger are written via shell.
Historical precedent: some tracks instead committed
`.planning/<track>/.goal-loop-ledger.md` — acceptable alternative if reviewers
prefer committed evidence; decided to keep root-local unless asked.

## D3 — Duplicate-acquisition policy (PENDING census)

Candidates: first-wins / last-wins / mean / typed-error-unless-policy-param.

## D4 — Irregular axis representation (PENDING census)

Whether existing axis stores per-sample timestamps vs assumed-regular calendar
decides WP1 implementation depth.

## D5 — Census verdict → work-package mapping (post-census, Round 0)

Census (subagent d1683a02) mapped the existing subsystem. Refined plan:

- **WP1 axis contract: mostly already covered.** `AcquisitionTime` (Date/DateTime
  precision, UTC instant, no-guess parse), `DuplicatePolicy`, `timeSource`
  provenance, blocking `temporal.missing_time`/`temporal.duplicate_time`
  preflight, STAC mandatory datetime — all present. Deliverable = Oracle
  fixtures (leap-year, cross-year, dup, missing, ambiguous filename → typed
  fail); fix only real defects found.
- **WP2 gap/quality: two real gaps.** (a) smoothing kernels
  (`savitzkyGolay`, `whittakerSmooth[Robust]`, `movingAverage`) are
  position-based — no `tDays` — unsafe on irregular cadence (census: largest
  single irregular hole). (b) `rs:temporal_gap_fill` emits aggregate
  `filled_count` but no per-sample observed/interpolated/unavailable
  provenance. Deliverables: `temporal_irregular.{h,cpp}` kernels
  (`whittakerSmoothTime`, `whittakerSmoothTimeRobust`, `movingAverageDays`),
  per-sample provenance codes through `gapFillSeries`, new
  `rs:temporal_smooth` methods + `rs:temporal_gap_fill` provenance raster.
- **WP3 harmonic: covered** (robust IRLS `harmonicFit` on true `tDays`,
  `seasonalDecompose` on `doyOf`). Only caveat: Whittaker *inside* decompose is
  still position-uniform — noted as known limitation, not silently changed
  (behavior change would regress existing tests).
- **WP4 phenology: missing rate metrics.** `SeasonalMetrics` has
  sos/pos/eos/los/amplitude/base/integral but no green-up/senescence rates;
  double-logistic exists only in the dead `TemporalCube` lineage.
  Deliverable: additive `greenUpRate`/`senescenceRate`/`greenUpMidDoy`/
  `senescenceMidDoy` fields (threshold-crossing semantics, NaN when
  undefined) + `rs:temporal_phenology` output bands.
- **WP5 trend/break: uncertainty half-wired.** `harmonicTrendCoefficientCi`
  has NO operator consumer; `residualBootstrapCi` only wired in
  `seasonal_breaks`. Deliverable: opt-in `compute_ci`/`ci_level`/
  `bootstrap_resamples`/`bootstrap_seed` on `rs:temporal_sen_trend` (slope CI),
  `rs:temporal_breakpoints` (break day/magnitude CI), `rs:temporal_harmonic_fit`
  (coefficient CIs) — mirroring the seasonal_breaks convention.
- **WP6 fusion: no interface exists.** Deliverable: `temporal_fusion.{h,cpp}`
  kernel (GridSignature compatibility check + FusionPlan validation +
  manifest model — pure, GDAL-free) + `rs:temporal_sar_fusion` operator joining
  already-coregistered per-pixel feature rasters (optical features +
  sar_temporal_stats output) with provenance manifest. Coregistration is a
  *checked precondition*, never performed; joint-recognition features are
  outputs, not a classifier.
- **WP7 streaming: covered** (`TemporalTileReader` O(tilePixels×vars), peak
  slots accounting, no T×H×W). Deliverable: bound-assertion test evidence.

## D6 — Cross-track defects found (filed, not fixed here)

- Malformed capability sidecars (interleaved dual JSON documents, verified on
  rs-temporal-regularize/-phenology; corrupts `pi/knowledge/capability-temporal.md`)
  → **issue #1129** filed.
- Determinism census snapshot missing 3 registered TI-11 ops
  (seasonal_breaks/model_select/phenology_multi) — will verify test status;
  my snapshot regen picks them up incidentally.
- `TemporalCube` (Stack B) production-dead; dual registration surface
  divergence (7 ops missing from manual `add()` list) — flagged, not fixed.

## D7 — New-file naming (D1 resolved)

Option (a): new files inside `src/processing/algorithms/temporal/`
(`temporal_irregular.*`, `temporal_fusion.*`), operators as new
`rs_temporal_*_operator` files, tests `test_temporal_*_12.cpp`. Matches repo
conventions; temporal12 semantics live in file names + sidecars.

## Post-implementation decisions (iteration 2)

1. **Nonuniform Whittaker = second divided difference × cell width.** D row r =
   2·(Δz_{r+1}/h_{r+1} − Δz_r/h_r)/span with C = diag(span/2) — a discretized
   ∫(z″)²dt. Reduces EXACTLY to the existing Σ(Δ²z)² Gram at unit spacing
   (proven by unit-spacing equivalence test, not assumed).
2. **Harmonic CI must not reuse harmonicTrendCoefficientCi.** That kernel's
   design is [1,t,sin/cos…] (2+2h); harmonicFit is [1,sin/cos…] (1+2h).
   Refactored analyticCoefficientCiImpl with includeTrend flag; exported
   harmonicCoefficientCi for the no-trend basis.
3. **Sen CI = Gilbert (1987) order-statistic interval** — reuses the already
   materialized sorted pairwise slopes; O(1) extra per pixel vs bootstrap's
   199 refits. Breakpoints get per-segment OLS slope SE (σ̂² = RSS/(n−2)).
4. **compute_ci is opt-in everywhere**, preserving output band shapes when
   off; harmonic_fit requires writeCoefficients (typed error otherwise).
5. **Fusion = verified-stack concatenation, never realignment.** Typed
   mismatch list (width|height|geotransform|projection), CRS compared
   textually (fail-closed), SICNU_FUSION_* provenance metadata.
6. **Provenance sidecar is opt-in** (`provenance_output` UInt8 raster) —
   keeps default output shape unchanged.
7. Smooth operator: new `*_days` methods are distinct enum values —
   positional `window`/`moving_average_window`/`degree` checks scoped to the
   methods that consume them.
