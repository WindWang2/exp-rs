# CAPABILITY_MATRIX — temporal-intelligence-11 (before → after)

Legend: ✅ implemented & locally verified · 🟡 degraded/documented limit · ❌ not supported (documented).

| Capability | Before (origin/master a5b11b7f) | After (this PR) |
|---|---|---|
| Trend break detection | ✅ `rs:temporal_breakpoints`, `rs:temporal_harmonic_breaks` (trend-of-residual) | unchanged |
| Seasonal amplitude/phase break detection | ❌ (declared debt ADR 0148) | ✅ `rs:temporal_seasonal_breaks` + `seasonalComponentBreaks` kernel; per-break attribution {none/trend/seasonal/both} |
| Full BFAST / CCDC parity | ❌ (documented as non-goal) | 🟡 unchanged non-goal; "inspired" scope documented per kernel |
| Per-segment model selection | ❌ (declared debt) | ✅ `rs:temporal_model_select` + bounded grid {harmonics 0..3}×{none,linear}×{0..2 breaks}, AICc/BIC/block-CV, deterministic tie-break, degraded refusal |
| Fit uncertainty (CI) | ❌ | ✅ analytic coefficient CI + seeded residual bootstrap (break date/magnitude/phenology), opt-in params |
| Quality-weight propagation into fits | ❌ (weights only in composite best-pixel) | ✅ quality band → weights in new fits (existing weighted-LS machinery) |
| Irregular/missing sampling in CI | ❌ | ✅ bootstrap over observed indices; success-count flags; NaN CI on low success |
| Multi-cycle phenology (auto) | 🟡 ≤2 caller-declared windows; years mixed in raster op | ✅ `rs:temporal_phenology_multi`: automatic cycle candidates (bounded), per-window metrics |
| Cross-year (wrapped) season windows | 🟡 wrapped windows exist; no year-spanning assignment | ✅ harvest-year assignment for startDoy>endDoy windows |
| Phenology quality flags / refusal | 🟡 valid flag per cycle | ✅ per-metric flags {sampleCount, gapFraction, amplitudeRatio, coverage}; refusal on low coverage |
| Batch region tables | ✅ `rs:temporal_extract_regions` / `region_features` (O(R), cancellable) | unchanged + opt-in seasonal-break attribution columns |
| Shared point-in-polygon | 🟡 two implementations (extract_series vs region_table) | ✅ single membership authority; extract_series rewired |
| Region-table GUI | ❌ (declared debt) | ✅ new algorithms surfaced in `TemporalAnalysisDialog` (existing seam) + agent spatial tool |
| Fit scratch reuse | ❌ per-call Gram allocations | ✅ caller-owned scratch; bit-exact outputs (determinism anchor green) |
| Tile batching / SIMD | 🟡 tiled streaming exists; no SIMD | 🟡 unchanged (SIMD: not-executed with reason D-TI11-8) |
| Synthetic corpus tests | ❌ per-feature ad-hoc fixtures | ✅ `tests/temporal_corpus.h` deterministic scenario corpus incl. negative controls |
