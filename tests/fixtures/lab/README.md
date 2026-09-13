# Lab grading fixtures (D4)

Deterministic, closed-form scenes for the lab auto-grading corpus. Regenerate with:

    python3 tests/fixtures/lab/generate_fixtures.py   # requires python3-osgeo (GDAL)

Bytes are reproducible (no compression, no timestamps, no RNG). Scene algebra lives in the
generator docstring; the graded expectations live in the matching
`data/labs/grading/<lab_id>.rules.json` (each rule carries its derivation).

## References (must score 100)

| fixture | scene |
|---|---|
| `ndvi_basics_reference.tif` | 128² Float32 NDVI: water rows 0..31 at −0.2, vegetation rows 32..127 at 21/29; 128 declared-invalid inputs → NoData |
| `ndvi_bandpair_reference.tif` | 16² 2-band Float32 DN (scale 1e-4): quadrant index −1/7, 5/7, −1/13, 1/2 |
| `terrain_slope_reference.tif` | 32² Float32 Horn slope of z=2x, 1 m cells: interior atan(2), 124-px border NoData |
| `planck_temperature_reference.tif` | 128² Float32 K: 300 + 2·sin(2π·col/128) (mean 300, σ=√2) |
| `landcover_reference.tif` (+ `landcover_truth.tif`) | 32² Byte classes 1..4 by row bands; 32 truth-NoData pixels |
| `change_detect_reference.tif` | 128² Byte mask: exactly 3072 changed px, 64 NoData px |

## Wrong answers (declared score bands in `wrong_answer_corpus.json`)

Each fixture deliberately commits one classic error class: wrong stretch, wrong band pair /
order, no calibration (gain mismatch), forgot to mask, masked everything, wrong sign, wrong
axis, degrees-vs-radians, Celsius-vs-Kelvin, DN-path inversion, legend shift, over/under
detection. All bands lie strictly below the 60-point pass line — the grader is required to
fail every one of them and to name the assertions that fired.

`reference_corpus.json` and `wrong_answer_corpus.json` are the machine-readable corpus
contracts asserted by `tests/test_lab_grading.cpp`.
