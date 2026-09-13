# ADR 0148: Temporal Platform 10.0 — Regular Calendars, Joint Change Models, Multi-Region Extraction

## Context

The temporal operator family (Temporal 1.0–3.0) shipped streaming
multi-temporal kernels over the ADR 0125 workspace: collection descriptors,
preflight, tile streaming, and fourteen `rs:temporal_*` operators. Four
platform gaps remained (registered in `ISSUES.md` by the D3 lab track and
re-verified at master `7d78059d1a`): `rs:temporal_monitor` refused inline
`scenes` (T-1); gap filling could not re-cast a series onto a configured
calendar (T-2); no operator modeled seasonal + trend regime changes jointly
(T-3 — `rs:temporal_harmonic_fit` is global, `rs:temporal_breakpoints` is
piecewise-linear); extraction handled exactly one ROI per call (C-2). None
of the outputs formed an ML-consumable artifact (G).

## Decision

1. **Regular-calendar normalization as its own operator**
   (`rs:temporal_regularize` + `temporal_calendar` kernel), not a gap-fill
   mode. Gap fill's contract — same calendar as the input, interpolation
   between real acquisitions — stays intact; re-casting the calendar changes
   the output geometry and the provenance model (every point synthetic or
   aggregated) and deserves its own schema. Whittaker regularization is
   defined on the calendar grid (Eilers 2003 banded solve reused), with
   `maxGapNodes` bounding bridging and a no-extrapolation refusal carried
   over from gap fill.

2. **Joint seasonal-trend change as a greedy segmentation with an honest
   name** (`rs:temporal_harmonic_breaks` + `temporal_change` kernel). Each
   segment fits `[1, t, harmonics]`; breaks are detected on the
   seasonality-adjusted residual by the existing greedy RSS kernel;
   magnitudes are fitted-level jumps; disturbance onset/recovery are
   operator-level semantics over the fitted series. Descriptions say
   "BFAST/CCDC-inspired" — this is not BFAST (no iterative trend/seasonal
   alternation) and not CCDC (no L1, no per-segment model selection).

3. **Multi-ROI extraction as a first-class operator**
   (`rs:temporal_extract_regions` + `temporal_region_table` kernel).
   Caller-owned region ids (duplicates refused), pixel-center polygon
   membership shared with `rs:temporal_extract_series` semantics, date-
   outermost streaming so each scene is read exactly once, O(R) state with
   a bounded CSR median scratch that degrades to NaN beyond its budget.

4. **ML features as a typed table artifact**
   (`rs:temporal_region_features`): versioned schema sidecar
   (`exp_rs_temporal_region_features/1`) pinning feature order; join with
   label tables by `region_id`. No Dataset-store coupling in this track —
   the artifact is a plain CSV + sidecar that the foundry can ingest later.

5. **Additive extensions**: monitor schema accepts `scenes` (T-1 — the
   shared `parseCollection` seam already parsed both inputs);
   `whittaker_robust` (IRLS Cauchy) in `rs:temporal_smooth`; `cycles=2`
   double-cropping phenology bands in `rs:temporal_phenology`, with
   per-year cycle metrics living in the region-features surface.

## Consequences

- Per-operator scientific contracts are updated in
  `docs/processing/temporal.md` in the same PR (Foundation 4.0 policy).
- The dense normal-equation solver is shared (`temporal_linalg_detail.h`);
  the calendar Whittaker and the change model reuse existing kernels rather
  than growing second implementations.
- New operators surface automatically through CLI/MCP/agent via the registry
  mirror (ADR 0120) and schema-form; capability knowledge
  (`data/agent/capabilities/temporal.json`) and sidecars
  (`data/processing/algorithm_meta/`) are appended.
- Known follow-ups (documented, not claimed): per-segment model selection,
  seasonal-component break detection (trend-only today), GUI dialog for the
  region table, dataset-foundry ingestion of the feature artifact.
