# R14-A Oracles — per work package

Format: Oracle ID → Failing Input → Old Behavior → Fix Diff → Run Evidence → Residual Risk.
Run evidence is filled in TEST_RESULTS.md as builds/tests complete (build serialized behind the
cross-track global lock; see BASELINE.md).

| ID | Failing input | Old behavior | Fix (commit) | Status |
|---|---|---|---|---|
| A1 | EVI/SAVI temporal series, unit reflectance, one UNDECLARED -9999 fill pixel in a probed tile | probe used std::abs → maxAbs=9999 > kDnScaleThreshold → sceneDivisor=10000 → whole-scene EVI off by ~1e4 | 8d2c0dc52 (positive-only probe, #856 parity) | RED test written (test_temporal_algorithms.cpp); run pending |
| A2 | local RX Diagonal mode, all-constant window (or loading=0 + zero-variance band) | var[b]+load==0 → Inf/NaN → NonFiniteScore → dualWindowRx false → operator throws for whole raster (contract violation) | 836558ad5 (SingularBackground before dividing) | RED test written (test_spectral_local_rx.cpp); run pending |
| A3 | sam_classify raster with band1 NoData=0, band2 NoData=-9999; pixel void only in band 2 | first-band sentinel only → pixel classified from -9999 as data | 532c8ca53 (per-band wash) | RED test written (test_spectral_classification.cpp); run pending |
| A4 | continuum_removal raster, heterogeneous sentinels; spectrum void in band 2 | band-1 sentinel only → -9999 entered convex hull → garbled Rc; band-1 void written as sentinel 0.0 | 0c3647582 (per-band wash + NaN tags) | RED test written (test_spectral_classification.cpp); run pending |
| A5 | radiometric_qa on raster declaring NoData=-9999 with fill pixels | fill entered stats as FlagNegative (false physics anomaly) | f5ef98cd7 (wash to NaN → FlagNotFinite) | RED test written (test_radiometric_qa.cpp); run pending |
| A6 | image fusion, pan/band1 sentinel 0.0 + band2 sentinel -9999; pixel void in band 2 | single working sentinel (pan's) → -9999 fused as data → garbage output | 29e8577b2 (read-boundary per-band wash, NaN working sentinel, NaN tags) | RED test written (test_image_fusion.cpp); #700 test updated to NaN convention; run pending |

## Residual risks recorded up front

- A6: GDAL resampling blends sentinel edges before the wash (exact-match only) — pre-existing
  resample-then-mask limitation, documented in the commit; not worsened.
- A3: undeclared-band policy differs between rs_spectral_similarity (-9999 fallback) and
  rs_sam_classify (NaN-only) — deliberately PRESERVED (changing it would alter existing outputs);
  recorded as P3 observation for the owning future round.
- A4/A6: output NoData representation for voids moved from finite-sentinel echo to NaN+NaN-tag —
  matches the spectral-family convention (rs_spectral_index, band_math); consumers keying on the
  tag follow automatically; consumers comparing raw values to a sentinel must switch to
  std::isnan (tests updated accordingly).
