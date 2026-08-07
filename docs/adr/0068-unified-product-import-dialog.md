# ADR 0068: Unified Product Import Dialog

## Context

The Landsat import dialog (`LandsatImportDialog`) was the only GUI path for
complex-product import. Its logic was already sensor-agnostic — it drove the
`CollectionImportService` probe-preview-commit transaction, and the probe
auto-detects Landsat / Sentinel-2 / MODIS from the selected path via
`SatelliteProducts::discoverProduct` — but its class name, strings, help keys,
and window title were Landsat-specific. Sentinel-2 had operator + CLI/MCP
import but no UI (only the STAC browser). The mission's Priority 0 says:
generalize the product-aware import approach into a unified product importer
"rather than accumulating unrelated per-sensor dialogs".

## Decision

1. **`LandsatImportDialog` → `ProductImportDialog`**
   (`src/app/dialogs/product_import_dialog.{h,cpp}`): the same
   probe-preview-commit transaction, with a `setProductFamily(family)`
   ("landsat" / "sentinel2" / "modis" / "auto") that shapes only presentation —
   window title, path placeholder, browse caption, and help tool key
   (`landsat_import` / `sentinel2_import` / `modis_import` / `product_import`).
   The probe and commit logic are untouched; product recognition stays in
   `CollectionImportService` / `SatelliteProducts`.

2. **Menu entries** (工程 menu): "导入产品..." (auto) plus per-family entries
   "导入 Landsat 产品..." and "导入 Sentinel-2 产品...", all opening the same
   dialog with the matching family. `openLandsatImportDialog` becomes
   `openProductImportDialog(family)`.

3. **Tests**: `test_landsat_import_dialog` → `test_product_import_dialog`
   (headless probe/commit/subset/cancel/invalid cases unchanged) plus a
   Sentinel-2 SAFE probe-and-commit case proving the same dialog imports an
   S2 product (4 × 10 m band children) with the family label applied.

## Consequences

- Sentinel-2 products are importable through the normal desktop UI, completing
  the "Import Sentinel-2 → semantic bands → NDVI" path with role-aware
  defaults (ADR 0065).
- No per-sensor dialog accumulation: future optical sensors (MODIS, others)
  reuse the same dialog with a family label and help key.
- The Landsat import behavior and its transaction tests are preserved verbatim
  (only names/presentation changed).
