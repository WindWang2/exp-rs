# ADR 0067: QA / Cloud / Cloud-Shadow / Snow Masking

## Context

The analysis-ready optical workflow had no quality-mask capability at all:
imports preserved Landsat QA_PIXEL/QA_RADSAT and Sentinel-2 SCL bands, but
nothing interpreted them. Every downstream consumer (indices, change
detection, classification) had to accept obscured pixels or hand-roll per-file
filters. The mission's Priority 0 requires first-class mask handling with
configurable rules, mask statistics, and provenance.

Sentinel-2 discovery did not even expose the auxiliary layers: its band regex
matched only `B…`/`B8A`, so SCL / MSK_CLDPRB were never discovered.

## Decision

1. **`rs:qa_mask` operator** (`src/operators/rs/rs_qa_mask_operator.{h,cpp}`)
   over the `QaMask` kernels (`src/processing/algorithms/qa_mask.{h,cpp}`):
   - **Sources**: `landsat_qa_pixel` (Collection 2 bit flags: fill, dilated
     cloud, cirrus, cloud, cloud shadow, snow, water), `sentinel2_scl`
     (SCL class ids, 16-entry class table), `generic_bitmask` (explicit
     `bits`). `auto` resolves from the QA band's semantic role
     (`scene_classification` → SCL, `qa` → Landsat), falling back to the band
     name.
   - **QA band resolution**: explicit `qa_band` wins; otherwise the band is
     resolved from `SICNU_BAND_ROLE` product metadata (scene_classification
     preferred, then qa) — the ADR 0065 semantic roles are the seam, no
     hard-coded band numbers.
   - **Mask selections**: `cloud_and_shadow` (default), `cloud`, `cloud_shadow`,
     `snow`, `water`, `all` — mapped per source (e.g. SCL cloud = classes
     8/9/10; Landsat cloud = bits 1/2/3).
   - **Output**: UInt8 binary mask (1 = masked, 0 = clear) sharing the input
     grid, with `SICNU_QA_MASK_SOURCE`/`SICNU_QA_MASK_SELECTION` metadata.
   - **Statistics**: result JSON carries `maskedPixels`, `totalPixels`,
     `maskedPercent`.

2. **Sentinel-2 discovery** (`satellite_products.cpp`) now discovers `SCL` and
   `MSK_CLDPRB` auxiliary layers regardless of the preferred optical
   resolution (they are resolution-independent product members); they keep the
   `SceneClassification`/`QA` roles from ADR 0065 and remain excluded from
   default optical stacks (`bandIsQa`).

3. **UI**: `QaMaskDialog` (预处理 → "QA 掩膜（云/云影/雪）...") drives
   `rs:qa_mask` through the shared `runOperatorTask` seam; the QA band combo
   preselects the role-resolved band.

## Consequences

- The preprocessing pipeline can now derive cloud / cloud-shadow / snow masks
  from real product QA bands with automatic band resolution and actionable
  class selection, headless (operator/CLI/MCP) and in the GUI.
- Mask statistics are part of the operator result, feeding later quality
  reporting and masking-application workflows (apply-mask-to-product is a
  follow-up that can reuse the mask raster).
- Sentinel-2 product imports surface SCL without polluting optical stacks.
- No FMask-style morphological buffering yet; SCL cloud-shadow interpretation
  uses the class only (no probability thresholds) — documented in the
  operator metadata as a limitation.
