# Quality Mosaic & Fusion (F15, ADR 0163)

Production-grade multi-scene mosaicking and fusion-quality reporting.

## `rs:quality_mosaic`

Mosaic multiple co-registered rasters with seamlines, radiometric balancing,
feather blending, quality scoring and per-pixel provenance.

**Inputs** (`inputs`): array of paths or objects:

```json
{
  "path": "/data/scene_b.tif",
  "priority": 10,
  "cloudFraction": 0.2,
  "quality": 0.9,
  "timeDays": 12.0,
  "viewAngleDeg": 7.5,
  "cloudMask": "/data/scene_b_cloud.tif"
}
```

* `priority` — higher wins ties in composite order (after quality score).
* `cloudFraction` / `quality` / `timeDays` / `viewAngleDeg` — scene-level
  quality metadata; each dimension is scored 0..1 and combined with the
  weights below (renormalized over the provided dimensions).
* `cloudMask` — optional per-pixel sidecar, **must be co-registered** with its
  scene (same geotransform and size); values in [0,1], ≥1 excludes the pixel.

**Key parameters**

| Param | Default | Meaning |
|---|---|---|
| `method` | `seamline` | `seamline` (DP seams) or `quality` (per-pixel best score) |
| `bandCount` | all common bands | bands to mosaic (≤ smallest input band count) |
| `balancing.enabled` | `true` | inter-scene relative radiometric normalization |
| `balancing.referenceIndex` | auto | reference scene (auto: largest eligible area) |
| `balancing.rejectPolicy` | `fail` | `fail` aborts; `drop` proceeds without rejected scenes |
| `seamline.weights` | 1 / 0.5 / 2 / 1 | radiometric / gradient / cloud / edgeDistance weights |
| `blending.mode` | `feather` | `none` or `feather` (`featherWidth` px, default 32) |
| `provenance` | `true` | append dominant-source band (0 = unfilled, i+1 = input i) |
| `reportOutput` | – | JSON report path (`exp-rs/quality-mosaic-report@1`) |
| `overviews` | `[2,4,8]` | overview factors; build failure degrades to a warning |

**Guarantees**

* **Grid plan first**: inputs must share CRS, pixel size and orientation;
  mismatched sets fail with an actionable message (reproject/resample first —
  use the gdal reproject operator). Sub-pixel offsets are snapped with a
  warning naming the input.
* **Balancing** is relative (overlap statistics), robust to clouds (residual
  MAD trimming) and refuses anomalous corrections (cumulative gain outside
  [0.5, 2] × overlap std bias gate) instead of writing wrong radiometry.
* **Seams** avoid clouds and footprint edges; ties are deterministic.
* **Blending never cracks**: per-pixel NoData falls back to the valid side;
  feather weights always sum to 1.
* **Every output pixel is traceable**: the provenance band records the
  dominant contributing input; the report records per-input contribution
  counts and the fitted corrections.
* **Atomic publication**: the mosaic is written to `<output>.part.tif` and
  renamed on success; any failure removes the temp file. Output is a tiled,
  deflate-compressed GeoTIFF with overviews (COG-friendly).

**Example**

```json
{
  "inputs": ["/data/a.tif", {"path": "/data/b.tif", "cloudMask": "/data/b_cloud.tif",
                              "timeDays": 16}],
  "output": "/out/mosaic.tif",
  "reportOutput": "/out/mosaic.report.json",
  "blending": {"mode": "feather", "featherWidth": 48}
}
```

## Fusion quality (`rs:image_fusion` + kernel)

`rs:image_fusion` supports `linear`, `brovey`, `pca`, `ihs`, `gram_schmidt`
and `hpf` methods. With `qualityReport: <path>` the operator additionally
writes a fixed-schema fusion quality report comparing the fused output with
the resampled MS reference:

* Wald metrics: ERGAS, mean CC, RMSE, SSIM
* Q (universal image quality index) and RASE
* per-band mean/std ratios — the spectral-distortion signature
* a pass/fail verdict against configurable thresholds
  (ERGAS ≤ 2.5, mean CC ≥ 0.94, Q ≥ 0.80, |meanRatio−1| ≤ 0.05,
  |stdRatio−1| ≤ 0.10 by default)

The report flags **spectral distortion** (mean-ratio shift = color cast,
std-ratio drift = contrast change) instead of silently returning a fused
raster. The kernel-level `rs::fusion::evaluateFusionQuality` accepts
degraded fused bands for a full Wald-protocol evaluation; the operator's
`qualityReport` compares at full resolution against the upsampled reference
(cheaper, documented as the fidelity form).

Kernel-level multiband (Laplacian) seam blending is available in
`processing/algorithms/mosaic_blend.h` for callers that need low-frequency
softening beyond feathering.

## Scale & resources

Streaming 512² tiles; peak memory is bounded by tile buffers plus ≤512×512
seam cells — independent of the mosaic extent. See
`.planning/mosaic-fusion-11/PERFORMANCE.md` for the measured evidence and the
`EXP_MOSAIC_SCALE_E2E=1` opt-in real-raster run.
