# CAPABILITY MATRIX — baseline → target

| Capability | Baseline (7d78059d1a) | Target (this track) |
|---|---|---|
| scenes vs collection inputs | unified via prepareTemporalRun except monitor (T-1) | monitor included |
| regular calendar output | none (T-2) | rs:temporal_regularize: nearest/window_mean/linear/whittaker + cadence + provenance |
| gap fill between acquisitions | linear/nearest, max-gap, no extrapolation | unchanged (contract intact) |
| joint trend+seasonal change | none (T-3) | rs:temporal_harmonic_breaks: per-segment harmonic+trend, magnitudes, disturbance onset/recovery |
| breakpoint segmentation | greedy piecewise linear (BSFAST-lite) | retained; harmonic variant added |
| trend | OLS + Sen/MK + seasonal MK/CUSUM/EWMA | unchanged |
| phenology | single season window, threshold fraction | + cycles_per_year multi-cycle, per-cycle metrics |
| smoothing | SG / Whittaker / MA | + whittaker_robust (IRLS) |
| multi-ROI extraction | single point/polygon (C-2) | rs:temporal_extract_regions: batch points/polygons, region ids, CSV/JSON table |
| ML feature artifact | none | rs:temporal_region_features: typed region feature table + schema sidecar |
| scale evidence | buffer accounting per operator | + benchmark_temporal10 (1000 scenes / 100k regions synthetic) |
| time semantics | real UTC instants, doy, date-vs-datetime | unchanged; calendar grid added on top |
