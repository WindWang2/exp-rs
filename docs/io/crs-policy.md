# CRS Policy — Geospatial I/O Foundation 4.0

Normative reference: `src/geospatial/crs/crs_policy.h`. Coverage:
`tests/test_io_crs_policy.cpp` (4326, UTM, projected, polar, axis-order
sensitivity, antimeridian, missing/invalid CRS, coordinate epoch).

## Rules

1. **Explicit at the boundary.** Every coordinate transformation declares its
   source CRS, target CRS and working axis order:
   ```cpp
   auto t = sicnu::geo::CrsTransform::create(
       sicnu::geo::Crs::fromAuthid("EPSG:4326"),
       sicnu::geo::Crs::fromAuthid("EPSG:32648"),
       sicnu::geo::AxisOrder::TraditionalGis);
   ```
2. **Axis order is declared, not implicit.** `TraditionalGis` (lon/lat for
   EPSG:4326) is the foundation default and matches the geotransform/extent
   convention; `Authority` (lat/lon) is one declaration away and test-covered.
   Geotransform and extent coordinates are **always** traditional-GIS order,
   regardless of the CRS's authority axis order.
3. **No guessing.** A dataset without CRS resolves to `MissingCrs`. The only
   fallback is a caller-declared one:
   ```cpp
   sicnu::geo::CrsPolicy policy;
   policy.allowDeclaredFallback = true;
   policy.fallbackCrs.authid = "EPSG:4326";
   ```
   and the resolution result reports `usedDeclaredFallback = true` — callers
   (operators, doctor) surface that in their outputs.
4. **Invalid CRS is `InvalidCrs`.** A broken WKT with a usable authority id is
   repaired from the id (declared-by-dataset data repair, tested).
5. **Epoch-aware.** Coordinate epochs are carried through `Crs`/`CrsInfo` and
   attached to PROJ transforms (dynamic datums).
6. **Bounds are densified.** `forwardBounds` samples the box edges (corner-only
   sampling is wrong across curved projections) and reports
   `crossesAntimeridian` after longitude normalization — an antimeridian
   crossing is surfaced, not smeared into a nonsense envelope.
7. **Ballpark is visible.** When PROJ can only offer a ballpark datum path, the
   transform's `diagnostics().ballpark` is true — never a silent approximation.
