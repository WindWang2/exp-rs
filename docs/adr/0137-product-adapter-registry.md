# ADR 0137: Sensor Product Adapter Registry & Normalized Product Metadata (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: two product-parsing generations coexist —
  `src/processing/algorithms/satellite_products.*` (Landsat/S2/MODIS discovery
  for import+stacking) and `src/geospatial/products/product_adapters.*`
  (metadata enrichment). Neither enumerates product *constituents* as first
  class assets (S2 granule per resolution, S1 measurement/annotation/
  calibration families), neither reports *completeness* (a SAFE missing its
  XML must not import as a silently partial product), and consumers (Dataset
  foundation, solution templates) still lack one stable identity surface.
- Decision:
  1. **One adapter contract inside `src/geospatial`** (Qt-free):
     `ProductAdapter { id, probe(path)→verdict, readMetadata, enumerateAssets }`
     plus a static `ProductAdapterRegistry` (Landsat MTL, Sentinel-2 SAFE,
     Sentinel-1 SAFE, MODIS container, GenericRaster fallback). Adapters
     *understand products*; pixel I/O stays delegated to GDAL.
  2. **Normalized product metadata** keeps the `ProductMetadata` vocabulary
     (platform/sensor/level/time/orbit/polarization/cloud/resolution/CRS hint)
     and preserves raw source metadata alongside the normalized fields
     (`extra` passthrough, bounded). Nothing is fabricated: unknown stays
     unknown; filename heuristics never override authoritative sidecars.
  3. **`ProductAssetSet`**: constituents enumerated as logical assets with
     role (measurement/annotation/calibration/mask/browse/thumbnail),
     native band name, band role (vocabulary of `src/data/band_role.h` —
     SAR polarizations map onto the same string vocabulary, extended), and
     resolution. Sentinel-2 keeps its resolution groups distinct — the
     adapter never resamples 10 m bands onto the 20 m grid.
  4. **Completeness verdicts**: `ProductProbeVerdict ∈
     Complete | PartialReadable | Invalid | UnsupportedVersion` with per-missing
     constituent severities (core file missing → PartialReadable/Invalid by
     role; unknown product version → UnsupportedVersion, never guessed).
  5. The legacy `SatelliteProducts` discovery stays for the processing import
     flow; the geospatial registry is the semantic authority and both share
     the band-role vocabulary. No adapter ever opens pixels during probe.
- Consequences: Track A reads product inputs through one contract; Dataset (D)
  can anchor identity on `(productId, assets, acquisition)`; a truncated SAFE
  reports exactly which constituents are missing instead of failing deep in a
  band open.
