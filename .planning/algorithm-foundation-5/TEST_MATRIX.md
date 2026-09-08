# Test Matrix — Foundation 5.0

Follows `docs/processing/validation-policy.md` (grades: exact / bit-exact /
tolerance; 10-item fixture taxonomy). This file tracks per-new-operator
evidence. Grades are declared up front and may never be widened to green.

| Operator / kernel | Known-answer | Synthetic | Degenerate | Streaming≡ref | Grid refusal | Cancel/no-partial | Grade |
|---|---|---|---|---|---|---|---|
| primitives:histogram | A.1 hand bins | ✔ | ✔ | n/a | n/a | n/a | exact (counts) |
| primitives:percentile | A.2 closed forms | ✔ | ✔ | n/a | n/a | n/a | bit-exact |
| primitives:morphology | A.4 patterns | ✔ | ✔ | n/a | n/a | n/a | exact |
| primitives:connected_components | A.5 labels | ✔ | ✔ | n/a | n/a | n/a | exact |
| primitives:distance_transform | A.6 Euclid closed forms | ✔ | ✔ | n/a | n/a | n/a | bit-exact |
| rs:topographic_correction | B.1 C/Minnaert closed forms | tilted-DEM scene | ✔ | ✔ | ✔ | ✔ | tolerance 1e-6 |
| rs:spectral_derivative | B.3 polynomial exact | ✔ | ✔ | ✔ | n/a | ✔ | bit-exact |
| new index aliases | B.2 hand-derived | ✔ | ✔ | reuse index streaming compare | n/a | ✔ | bit-exact |
| rs:sid | C.1 closed form (small spectra) | ✔ | ✔ | ✔ | ✔ | ✔ | bit-exact |
| rs:matched_filter | C.2 planted target | ✔ | ✔ | ✔ | ✔ | ✔ | tolerance 1e-5 |
| rs:ace | C.3 planted target | ✔ | ✔ | ✔ | ✔ | ✔ | tolerance 1e-5 |
| rs:sar_dualpol_features | D.2 hand formulas | ✔ | ✔ | ✔ | ✔ | ✔ | bit-exact |
| sar range-doppler subset | D.3 plane geometry | orbit synthetic | ✔ refusal | ✔ | ✔ | ✔ | tolerance |
| rs:temporal_cusum/ewma | E.1 hand series | ✔ | ✔ | n/a (per-pixel) | n/a | ✔ | bit-exact |
| rs:temporal_seasonal_mk | E.2 hand series | ✔ | ✔ | n/a | n/a | ✔ | exact ties / bit-exact S |
| rs:temporal_phenology (multi-season) | E.3 planted seasons | ✔ | ✔ | n/a | n/a | ✔ | exact breaks |
| terrain curvature etc. | F.2–F.4 analytic surfaces | tilted/quadratic DEM | ✔ | ✔ | ✔ | ✔ | bit-exact/tolerance |
| rs:fill_sink/flow (F) | F.5–F.6 hand DEMs | ✔ | ✔ | ✔ | ✔ | ✔ | exact/bit-exact |
| rs:resample | G.1 shift-invariance cases | ✔ | ✔ | n/a | ✔ | ✔ | tolerance (interp) |
| rs:align | G.2 grid identity cases | ✔ | ✔ | n/a | ✔ (refusal set) | ✔ | exact (NN) |
| rs:rasterize | G.3 hand polygons | ✔ | ✔ | n/a | n/a | ✔ | exact |
| rs:sieve / fill_holes / clump | G.4/G.6 patterns | ✔ | ✔ | n/a | n/a | ✔ | exact |
| rs:proximity | G.5 closed forms | ✔ | ✔ | ✔ | ✔ | ✔ | bit-exact |
| rs:zonal_stats | G.7 hand zones | ✔ | ✔ | ✔ | ✔ | ✔ | bit-exact stats |
| rs:focal_stats | G.8 hand windows | ✔ | ✔ | ✔ (edge policy) | n/a | ✔ | bit-exact |
| rs:local_extrema | G.9 patterns | ✔ | ✔ | n/a | n/a | ✔ | exact |
| rs:morphology / rs:connected_components ops | G.10/G.11 | ✔ | ✔ | n/a | n/a | ✔ | exact |
| kNN/min-dist/Mahal/ML/logistic backends | H.1–H.4 separable 2-class | ✔ | degenerate classes | n/a | grid ✔ | ✔ | tolerance/exact labels |
| rs:isodata | H.5 planted clusters | ✔ | ✔ | n/a | ✔ | ✔ | tolerance (declared) |
| spatial CV / metrics parity | H.6–H.8 hand confusion matrices | ✔ | ✔ | n/a | n/a | n/a | exact |

Rules:
- A row without a checked column cannot leave its milestone.
- Degenerate set (minimum): all-NoData, all-NaN, ±Inf, constant, single valid
  pixel, 1×N / N×1, non-tile-multiple dims, mismatched grid (typed refusal),
  missing CRS (typed refusal where arithmetic).
- Streaming-vs-reference comparisons use `tests/raster_bit_compare.h`
  (bit-exact) or NaN-aware tolerance per declared grade.
- Cancel tests reuse the TemporalOutputGuard / abandon() RAII patterns.
