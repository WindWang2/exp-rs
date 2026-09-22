## Hardening track: temporal-change-phenology (19/20)

Goal: only optimize/fix/complete the existing temporal fit, irregular-series,
change/breakpoint, phenology, gap-fill and decomposition modules. No new
product directions. Campaign slice 19/20, track `temporal-change-phenology`.

### Recon baseline

- `origin/master` = `a9dc33fa7` (2026-09-23; identical to the 2026-09-22 seed).
- Open PRs #1237/#1238/#1239 (teaching/experiment UI): zero file overlap with
  this branch (verified via `gh pr diff --name-only`).
- Open issues: 0. `agent/rs14-*` branches fully merged; `agent/flash-*` and
  `rs14-unified-verifier` mined as hints only, nothing ported.
- Already-fixed surfaces (#1200 MAD/MOSUM/SOS-EOS/leap, #1229 365.25
  doyOf/day-366/MOSUM σ√h/true MAD/interpolated crossings) treated as
  regression-only; where this branch touches them it is to RESTORE a contract
  those commits unintentionally broke (below).

### Root causes fixed (all reproduced on master `a9dc33fa7`)

**RC1 — #1229 regression: phenologyThreshold lost its edge-anchored
SOS/EOS.** Strict interpolated crossings replaced the quantized
first/last-sample-≥-threshold rule, so any season window that opens at/above
the threshold (season already running at the window edge) or a flat season
resolves no rising bracket → invalid metrics. Master HEAD is RED in three
untouched suites because of this (`test_temporal_irregular` ×2,
`test_temporal_change` ×1, `test_temporal_phenology_multi` partially).
Fix: edge anchoring — when no rising crossing exists before the peak and the
first in-season sample already sits at/above the threshold, SOS anchors at
that sample (EOS symmetric at the tail); limb metrics stay NaN (never
fabricated); interpolated midpoint semantics from #1229 unchanged for
well-bracketed seasons. Additionally `out.pos` is published only when the
metrics are valid (previously a refused cycle carried a fabricated peak doy;
`SeasonalMetrics` documents `-1 = undefined`).

**RC2 — #1200 follow-up: day-axis trend default eats the annual signal.**
`seasonalDecompose` correctly moved to the day-axis Whittaker penalty, but
the default λ=1e4 is the index-axis-era scale: at the annual frequency the
day-axis penalty response is ~1e-3 at every cadence, so the trend absorbs
the seasonal sinusoid, the climatology degrades to noise, and
`phenologyMultiCycle` invents 3 cycles/year on a single-season corpus
(measured: seasonal range 0.032 for a 0.7-amplitude input). Fix: default λ
1e4 → 1e8 in `PhenologyMultiOptions`, the `seasonalDecompose` fallback and
the `rs:temporal_decompose` operator (response at annual ≈ 1e4; cutoff
period ≈ 2π·λ¼·h ≥ 1.7 y at any cadence). The existing λ=1e8 day-scale
known-answer test (`test_temporal_fit.cpp` "Seasonal decomposition…")
already pins this scale.

**F1 — `BreakpointCandidate.index` broke its documented contract under NaN
or unsorted input.** The header promises "series index of the segment
boundary", but the code stored the split position in the compacted finite
sub-vector (off by the NaN count before the break). All existing tests fed
complete ascending series, so the defect was invisible. Fix: map through
`originSorted` (identity for the old inputs).

**F2 — day-366 samples silently dropped from their own season.**
`phenology_metrics` emits DOY ∈ [1,366] (leap bucket = 366), but
`extractMultiCycle`/`fitDoubleLogistic` used the `[1,365]` season window —
the exact peak sample of a late/leap season could vanish. Every other caller
uses the widest window `1..366`. Fix: align to `1..366`.

**F3 — `rs:temporal_decompose` silently published a fabricated decomposition
on degenerate time axes.** With `duplicate_policy=keep_all` (the default),
two same-day scenes made the trend kernel refuse (all-NaN trend) while the
climatology fallback produced an all-ZERO seasonal band — exit success, no
warning. An untimed scene degrades to day offset 0 the same way. Fix: typed
`InvalidInputData` refusal before any output is created (same contract as
`rs:temporal_smooth`'s day-axis methods), naming the colliding scenes and
the remediation knobs.

**F5 — gap-fill provenance artifact could not actually wire into the
downstream statistics.** `provenance_output` writes ONE multi-band GeoTIFF
(`prov_<date>` band per scene), while the downstream `provenance` parameter
expects one per-scene raster per entry and reads band 1 of each: the
artifact passed once was rejected (count mismatch), and the same path
repeated N times silently applied scene 1's channel to EVERY scene —
excluding the wrong dates from every statistic. Fix: `ProvenanceChannel`
accepts the artifact once and maps band s+1 → scene s (prov_-prefixed band
names required), and refuses named-but-mismatched per-scene entries with a
typed error. Schema help of the four consumers updated. Per-scene array form
unchanged (unnamed rasters untouched).

Non-behavioral: stale `madScale` convention comment corrected (F4).

### Test evidence (kill-proof)

Every behavioral fix is bound to an oracle that is RED on master and GREEN
here (see `docs/development/hardening/temporal-change-phenology/02-test-ledger.md`):

- Pre-existing master RED fixed: `test_temporal_irregular` (2 cases),
  `test_temporal_change` (1), `test_temporal_phenology_multi` (7 checks) —
  all green again without weakening any assertion.
- New oracles: breakpoint index mapping under NaN gaps; day-366
  multi-cycle retention; decompose duplicate-time typed refusal (+ no output
  raster left behind); smooth/phenology provenance-consumption E2E (NaN to
  the kernel, not post-hoc); provenance artifact wiring E2E (n=3 not 6;
  repeated-path misuse refused, no output left behind).
- Full temporal suite matrix green: temporal_fit, temporal_irregular,
  temporal_change, temporal_core, temporal_calendar, phenology_extraction,
  bfast_harmonic_breaks, temporal_phenology_multi, temporal_uncertainty,
  temporal_algorithms (38→41 cases), plus the wider temporal surface
  (selection/regions/operators_10/ti11/workspace/agent_tools/d16 suites/
  spatiotemporal_contracts) — run twice consecutively on the final commit.
- Changed TUs introduce no new compiler warnings (remaining warnings are
  pre-existing in untouched files).

### Performance / resources

- `detectHarmonicBreaks`'s greedy search is O(n²·p²) per accepted break, but
  its only callers on master are tests/e2e (the operator-facing
  `fitSeasonalTrendBreaks` already uses O(1)-per-candidate cumulative sums);
  no micro-optimization was fabricated (recorded as known limitation).
- F3/F5 add O(sceneCount) validation scans only.

### Compatibility

- `BreakpointCandidate.index` becomes MORE correct per its own doc comment;
  identity for complete ascending inputs (all prior expectations preserved).
- `provenance` parameter accepts a strictly larger set of inputs; previously
  "working" misuses (repeated multi-band path) were silently wrong and are
  now typed refusals.
- `rs:temporal_decompose` fail-closed change: runs that previously "succeeded"
  with all-NaN trend + all-zero seasonal now refuse with remediation hints.
- Default `trend_lambda`/`trendLambda` change: documented default in the
  schema help, no capability-JSON/surface drift (defaults are not pinned in
  the capability snapshots).

### Dedup / ownership

- No overlap with open PRs #1237/#1238/#1239 (teaching/experiment UI), no
  open issues, no remote branch carries any of these fixes
  (`agent/flash-*`/`rs14-unified-verifier` inspected, nothing ported).
- QGIS-vendored `src/core/*temporal*` untouched.

### Known limitations (classified "not done")

- Owned by other tracks: teaching/UI temporal panels (#1237/#1238/#1239);
  QGIS upstream temporal properties.
- Not reproducible on master: the campaign-seed defects (MAD/MOSUM/SOS-EOS/
  leap) — fixed by #1200/#1229, regression-tested here.
- Explicit future direction (recorded, not implemented): in-place
  `solveSmallDense` scratch for the per-pixel fit loop;
  `detectHarmonicBreaks` split-search complexity (test-only consumer today).
- Operator-level provenance ordering remains a documented contract for
  unnamed per-scene rasters (unnameable inputs cannot be validated).

### Rollback

Each commit is an independent conventional fix; reverting the branch restores
master semantics, including the (red) master test state.

---
Online CI not awaited. Reviewer verdict and double-run evidence in
`docs/development/hardening/temporal-change-phenology/`.
