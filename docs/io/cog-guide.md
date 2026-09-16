# COG Guide — producing Cloud Optimized GeoTIFFs

COG is a first-class product in ExpRS (ADR 0130). Production always goes
through the COG driver with a **safe preset** plus **pre-publish validation**;
hand-rolled `gdal_translate` calls bypass the fidelity policy and are not
sanctioned for products.

## Operators / API

```
io:make_cog   { input, output, preset, creationOptions[] }
C++: sicnu::geo::makeCog(input, target, CogPreset, extraOptions, progress)
CLI check:    sicnu_geo_rs_cli data doctor <file.tif>
```

`makeCog` stages beside the target, runs the COG driver (CreateCopy path via
the translate kernel), **validates the staged file** (`validateCog`: tiled
power-of-two layout, overview pyramid down to ≈ one tile, compression
declared, float predictor advisory), fsyncs and only then publishes. A file
that fails COG validation never reaches the target path.

## Presets

| Preset | Compression | Predictor | Use | Notes |
|---|---|---|---|---|
| `lossless_scientific` | DEFLATE (level 6) | 2 (int) / 3 (float) | scientific products | bit-exact pixels |
| `categorical` | LZW | none | class maps, QA rasters | **lossless by policy**; refuses float input (`fidelity_loss`) |
| `continuous_float` | DEFLATE | 3 (float; falls back to 2 with a surfaced warning on ints) | float grids | bit-exact |
| `sar` | DEFLATE | 3 when float | amplitude/intensity | convert complex products to amplitude first |
| `visualization` | JPEG (quality 75, YCbCr) | — | display only | **LOSSY** — explicit opt-in; result payload carries the lossy advisory |

All presets pin `BIGTIFF=IF_SAFER` (no silent >4 GB corruption),
`OVERVIEWS=AUTO` (driver-chosen tiled overview stack), `NUM_THREADS=ALL_CPUS`,
512 px tiling. (An earlier revision of this page claimed `OVERVIEWS=ALL`; the
code has always emitted `AUTO` — corrected in the 11.0 pass.)

### Explicit production options (11.0)

`io:make_cog` accepts explicit knobs on top of the preset, merged by the
planner (`geospatial/io/cog_options`) with REPLACE semantics — the COG driver
reads creation options first-match-wins, so appending a duplicate `-co` never
overrides a preset value:

- `blocksize` — power of two, 128..4096 (default: preset's 512).
- `overviews=false` — emits `OVERVIEWS=NONE`; the validator then states the
  missing-overview case instead of silently passing.
- `deterministic=true` — `NUM_THREADS=1` plus a pinned DEFLATE `level`
  (`deflateLevel`, 1..9, default 6). Byte-identical output for identical
  input **within one GDAL/libtiff build**; no cross-platform byte guarantee.
- `creationOptions` — arbitrary extras merged replace-or-append, each
  replaced preset key surfaced as a warning ("explicit beats preset").

The result JSON carries a `cog_plan` block: every key annotated with its
source (`preset` | `override` | `caller`) and the determinism scope. NoData
and alpha are deliberately not planner options: they travel from the source
dataset only (declared-only policy).

## Rules

1. **Categorical scientific products default to lossless.** Lossy compression
   for class/QA products is rejected (`GeoError::FidelityLoss`), not warned.
2. Warnings travel in the result JSON (`warnings[]`) — nothing is swallowed.
3. Check your product: `data doctor out.tif` reports the certified profile and
   structure; `validateCog` returns a machine-readable check list.
