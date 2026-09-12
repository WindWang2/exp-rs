# Lab Sample Data — `data/samples/`（本科实验样本数据）

Generated, deterministic sample assets for the undergraduate labs in
`docs/labs/`. Everything in `data/samples/` is **generated, never committed** —
one command rebuilds it byte-identically (see [Determinism](#determinism)).

```sh
cmake --build build --target sicnu_generate_samples
scripts/gen_samples.sh                # generates into data/samples/ + verifies
```

## Files

| File | Description | Used by labs |
|------|-------------|--------------|
| `landsat_sample.tif` | 7-band Landsat-like optical image, Float32 reflectance [0,1] | 1, 2, 3, 6, 7 |
| `dem_sample.tif` | Digital elevation model, Float32 meters | 5 |
| `change_before.tif` / `change_after.tif` | Before/after single-band pair for change detection | 4 |
| `training_samples.shp` (+`.dbf`/`.shx`/`.prj`) | 6 ROI polygons (one per class) for supervised classification | 3 |
| `landsat_truth.tif` | Byte class mask (ground truth of `landsat_sample`) | grading / self-check |
| `change_truth.tif` | Byte change mask (ground truth of the change pair) | grading / self-check |
| `dem_slope_truth.tif` / `dem_aspect_truth.tif` | Analytic slope/aspect (ground truth of `dem_sample`) | grading / self-check |

All rasters share one grid: **EPSG:32648 (UTM 48N)**, 30 m pixels, top-left
(500000 E, 4060000 N); `--profile=lab` is 256×256, `--profile=stress` is
2048×2048. GeoTIFF creation options: `COMPRESS=DEFLATE`, `TILED=YES`,
256×256 blocks, band interleave, predictor on (`LAYOUT=COG`-friendly); no
`.aux.xml` sidecars are produced (`GDAL_PAM_ENABLED=NO`).

## Land-cover classes

Class ids are consistent across `landsat_truth.tif`, the ROI `class_id` field
and the optical synthesis:

| id | name | signature behaviour (docs/labs table) |
|----|------|----------------------------------------|
| 1 | water | low reflectance everywhere, NIR lowest |
| 2 | vegetation | red low, NIR high (red edge) |
| 3 | urban | medium everywhere |
| 4 | bare soil | rises with wavelength |
| 5 | forest | like vegetation, higher NIR |
| 6 | shadow | very low everywhere |

The optical image is `signature + uniform noise (σ=0.02)`, clamped to [0,1].
Class regions are analytic functions of the pixel grid, so the class counts
are **seed-independent constants** (lab grid: water 9728, vegetation 32842,
urban 7854, bare 8242, forest 4814, shadow 2056).

The ROI polygons are *derived from the class map at generation time* by a
deterministic scan (first all-class rectangle ≥ W/16 per side, 2 px inset), so
training ROIs always sit on their class.

## DEM surface (closed form)

Elevation is a rational-analytic surface in normalized pixel coordinates
`nx=(x+0.5)/W`, `ny=(y+0.5)/H` (y grows southward):

```
z(nx,ny) = 100 + 200·ny + 8·(nx−0.5)(ny−0.5)
         + 150·b(nx−0.3, ny−0.4, R²=0.0225)
         + 100·b(nx−0.7, ny−0.6, R²=0.04)
         −  80·b(nx−0.5, ny−0.5, R²=0.0144)
b(dx,dy,R²) = (1 − (dx²+dy²)/R²)³   when dx²+dy² < R², else 0
```

`dem_slope_truth.tif` / `dem_aspect_truth.tif` are the **analytic** gradient of
exactly this surface (not finite differences): slope in degrees, aspect as the
compass bearing (° clockwise from north) of steepest descent, both quantized to
0.01°. Flat cells get aspect NoData −9999. Student finite-difference results
(`Raster > Terrain Analysis`) approximate these within a cell-scale tolerance —
the tolerance policy for automatic grading is D4's concern.

## Change model

`change_before.tif` = `0.40 + 0.40·(nx−0.5)(ny−0.5) + noise`;
`change_after.tif` replaces the deforestation disc `(nx−0.4)²+(ny−0.5)² < 0.03`
with `0.15 + 0.03·(nx−0.4) + noise`. `change_truth.tif` is 1 inside the disc,
0 outside (lab grid: 6180 changed pixels).

## Metadata contract

Dataset level (every raster): `SICNU_GENERATOR`, `SICNU_GENERATOR_VERSION`,
`SICNU_SEED`, `SICNU_PROFILE`, `SICNU_PRODUCT`.
Band level (`landsat_sample.tif`): `SICNU_BAND_ROLE` ∈
`coastal/blue/green/red/nir/swir1/swir2` (ADR 0065 ids) plus
`WAVELENGTH`/`WAVELENGTH_UNITS=nm`/`FWHM` (nm integers) — so NDVI, NDBI & co.
resolve bands semantically with no manual band numbers.
Band level (truth masks): `SICNU_CLASS_NAMES`, `SICNU_CLASS_COUNT`.

## Determinism

Same host + same GDAL build + same profile + same seed ⇒ **byte-identical**
files and manifest. The contract (enforced in `tools/sample_foundry.h/.cpp`):

* PRNG is `std::mt19937` with a portable uniform transform — no
  `std::distributions`, no `std::rand`; draw order is fixed and documented
  (row-major pixels, bands innermost, before-noise then after-noise).
* Synthesis arithmetic is +,−,*,/ only (FP contraction compiled off); the only
  transcendentals are `atan`/`atan2` in the slope/aspect truth, which are
  host-stable. Cross-host bit-identity is therefore expected for every product
  except the two aspect/slope truth grids at the last-ulp level.
* Truth *pixel content* is seed-independent; the files still differ byte-wise
  across seeds because every file carries the `SICNU_SEED` provenance stamp.
* The shapefile DBF creation date is pinned to 2000-01-01 so vector output has
  no wall-clock dependence.

`manifest.json` (schema: [manifest.template.json](../../data/samples/manifest.template.json))
records seed, profile, grid, GDAL version, and per-file SHA-256 + size, plus a
`self_fingerprint` computed over the manifest with that field removed (ADR 0134
semantics). Re-check a directory against its manifest with:

```sh
sicnu_generate_samples --out=data/samples --verify   # exit 0 = intact, 4 = drift
```

## Generating subsets for new experiments

`--spec=<file-or-dir>` takes JSON files declaring what an experiment needs
(contract for D3, see ADR 0146):

```json
{ "experiment": "lab-3-classification", "products": ["landsat_sample", "training_samples", "landsat_truth"] }
```

Unknown products, missing files, duplicates or mismatched experiments are typed
refusals (exit 3) — never a silent default. Without `--spec` the full set is
generated. Exit codes: 0 ok · 1 generation/I/O · 2 usage · 3 spec refusal ·
4 verify drift.
