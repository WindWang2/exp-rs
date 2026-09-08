# Processing: Scientific Algorithm Foundation 5.0 — Primitives & Certification

> Authority for the shared primitive layer introduced by Foundation 5.0 and
> the per-operator certification evidence. Companions:
> [validation-policy.md](validation-policy.md) (grades and fixture taxonomy),
> [nodata-and-statistics.md](nodata-and-statistics.md),
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md),
> [sar-domain.md](sar-domain.md).

## 1. Shared primitives (`src/processing/algorithms/primitives/`)

The platform's cross-family numeric kernels. Family kernels and operators
consume these instead of re-deriving formulas; re-implementing one of these
inline is a review-rejected pattern (the change-detection delegation is the
model: same values, one owner).

| Primitive | Contract highlights | Certified by |
|---|---|---|
| `raster_histogram` | platform binning `bin = (v−min)/range·(bins−1)` clamped; width reconstructed as range/(bins−1) (#700); NaN/±Inf never binned; Otsu with tied-maxima (empty-gap) averaging; nearest-rank quantile with in-bin linear interpolation | `test_primitives5` closed forms + the change-detection delegation suites |
| `morphology` | 0/1/255 byte masks (255 protected), 4/8-conn, replicate border (erode keeps border foreground; dilate does not grow inward) | `test_primitives5` + change-cleanup parity |
| `connected_components` | two-pass union-find, compact 1..K labels in raster order of first pixel; `removeSmallObjects` sieve | `test_primitives5` connectivity/NoData/sieve cases |
| `distance_transform` | exact Euclidean (Felzenszwalb separable); sources = 1-cells; unreached → +inf; 255 treated as non-source | `test_primitives5` closed-form rings |
| `percentile` | exact small-array quantiles; `NearestRank` = platform threshold convention, `Linear` = interpolated statistics — callers declare which | `test_primitives5` |
| `window` | edge-policy contract (Replicate/Reflect/Constant) + halo rule for streaming tiles | consumed by focal/extrema operators |

## 2. Foundation 5.0 operator families

| Family | Operators | Determinism grade | Key refusals |
|---|---|---|---|
| Optical | `rs:topographic_correction` | bit-exact (single-threaded, fixed order) | grid mismatch; degenerate C-regression (|b| < 1e-6); sun geometry out of range |
| Optical | `rs:spectral_index` + GNDVI/NDMI/MSAVI/ARVI/EVI2/BAI/UI/BUI | bit-exact (streaming element-wise) | band out of range (existing) |
| Spectral | `rs:spectral_derivative` | bit-exact | missing/non-ascending wavelength axis (index-space derivatives are refused) |
| Spectral | `rs:matched_filter`, `rs:ace` | bit-exact | wrong-length target; singular background; degenerate target |
| SAR | `rs:sar_dualpol_features` | bit-exact | unknown domain; band issues; nonpositive power → NaN (never clamped) |
| SAR | `rs:sar_terrain_masks` | bit-exact | incidence outside (0,90); heading outside [0,360) |
| Temporal | `rs:temporal_monitor` | bit-exact | `lambda` outside (0,1]; `max_pairwork` exceeded; scenes < min_observations |
| Terrain | `rs:terrain_analysis` + curvature/hillshade_md/relief products | bit-exact | invalid cell size (existing) |
| Terrain | `rs:terrain_flow` | bit-exact | empty DEM |
| Raster spatial | `rs:morphology`, `rs:connected_components`, `rs:fill_holes`, `rs:sieve`, `rs:proximity` | bit-exact | non-binary mask (typed, points at threshold/recode); all-background proximity |
| Raster spatial | `rs:local_extrema`, `rs:focal_stats` | bit-exact | even window |
| Classification | `rs:supervised_classification` + knn / min_distance / mahalanobis | tolerance (declared per backend) | unfitted/empty training |

## 3. Known limitations (declared, not hidden)

- Topographic correction, SAR terrain masks and the terrain-flow family
  reuse the #612 degrees→metres cell-size conversion inline (three copies);
  consolidation into one shared helper is tracked debt.
- Full range-Doppler RTC is not approximated — see sar-domain.md §3.
- Filled-flat routing in `rs:terrain_flow` is a sink (direction 0); no
  epsilon-gradient flat resolution.
- `rs:temporal_monitor` CUSUM/EWMA standardize against the full series
  (retrospective monitoring); a baseline-window variant is future work.
- ISODATA and logistic-regression classifiers are documented deferrals;
  max-likelihood classification is covered by NormalBayes.
- `rs:resample`/`rs:align`/`rs:rasterize`/`rs:zonal_stats` remain future
  work: they wrap or extend the `gdal:` seam and the vector boundary and
  need their own grid-target contract decision (ADR-worthy).
- `rs:clump` is deliberately absent: it is `rs:connected_components` under
  GIS naming; the alias is documented instead of a second implementation.
