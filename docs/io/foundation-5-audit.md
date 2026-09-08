# Remote Sensing I/O & Interoperability — Foundation 5.0 Audit

> Track E — `feat/remote-sensing-io-interoperability-5`
> Baseline: master @ `93a7fb0b` (4.0 platform series complete).
> Scope: systematize professional RS data I/O, sensor product understanding,
> normalized metadata, streaming reads, remote assets, format interop and
> export. This document is the Phase-1 gap analysis required by the 5.0 goal.

## 1. Where data enters exp-rs today (architecture map)

```text
Path / URI (QString)
  ↓
SourceDescriptor {providerKey, canonicalSource, subdataset, dataOptions, authConfigId}
  ↓
internal::SourceProvider registry (src/data/internal/source_provider_registry.*)
  ├─ GdalRasterSourceProvider   (src/data/providers/gdal_raster_source_provider.*)
  ├─ OgrVectorSourceProvider    (src/data/providers/ogr_vector_source_provider.*)
  ├─ VirtualRasterSourceProvider(src/data/providers/virtual_raster_source_provider.*)
  └─ WMS / WMTS / TMS remote-map providers + NetworkProbe host seam
  ↓
ResolvedSource → DataAsset {AssetKind, AssetState, AssetCapabilities, AssetStructure}
  ↓
DataManager (src/data/data_manager.*) — catalog authority: leases, revisions,
  dedup by SourceKey, dependency DAG, reap/promote, external-mutation watcher
  ↓
Consumers: RSOperators (rs:/gdal:/opencv:/otb:), processing algorithms,
  QGIS display layers, CLI, MCP/Pi tools, governance (import_center, metadata_pipeline)
```

### I/O seams that already exist on master

| Seam | Location | Notes |
|---|---|---|
| Source provider contract | `src/data/internal/source_provider.h` | `supports()` + `resolve()` → `ResolvedSource` |
| Structural metadata | `RasterStructure`/`VectorStructure` in `src/data/data_asset.h` | driver, size, bands (dtype/noData/colorInterp/`SICNU_BAND_ROLE`), CRS WKT, geotransform, extent |
| Canonical identity | `SourceDescriptor::sourceKey()` | provider + canonical path + subdataset + options |
| Remote rasters | `/vsicurl/` normalization in `GdalRasterSourceProvider` | bounded HTTP defaults via `configureRemoteCachingDefaults()`; app-thread deferred open |
| Remote handle pool | `RemoteDatasetPool` (`remote_source_cache.h`) | bounded per-URL handles, serialized access, staleness validator seam |
| Virtual rasters | `VirtualRasterRecipe` + preflight + `createVirtualRaster()` | recipe = identity; `.vrt` is disposable artifact |
| Band roles | `src/data/band_role.h` (15 roles) + `SICNU_BAND_ROLE` band metadata | round-trip string vocabulary |
| Product import | `SatelliteProducts` (Landsat MTL / S2 SAFE / MODIS) + `CollectionImportService` probe→commit | grid-grouped children, atomic collection+children registration |
| Radiometric state | `SICNU_RADIOMETRIC_STATE` + `SICNU_NUMERIC_SCALE` (ADR 0114) | written by calibration/atmospheric operators |
| STAC (UI) | `src/app/stac_client.*` (search-only, SSRF-guarded) + temporal `StacItem` adapter | nothing wrote STAC before 5.0 |
| Governed import | `ImportCenter` (scan bounds, batches, cancellation) + `metadata_pipeline` GDAL facts | workspace 3.0 |
| Output atomicity | `OutputCommitter` validate→staged rename→register | TaskCenter output seam |
| Geospatial foundation | **`src/geospatial` (this track, ported from I/O Foundation 4.0 + extended)** | Qt-free core: canonical metadata, CRS policy, windowed readers, atomic writers, COG, format profiles, STAC mapper, multidim, product adapters, doctor |

### Direct file I/O that bypasses DataManager (audit result)

* `src/processing/algorithms/*` read inputs through GDAL directly (sanctioned:
  algorithms operate on resolved paths from the asset resolver, outputs go
  through OutputCommitter).
* `src/app/dialogs/*` product import goes through `CollectionImportService`
  (sanctioned seam).
* Classification/segmentation label writers (`RsSegmentMap::toGeoTIFF`,
  `RsPostProcess::saveLabelRaster`) write outputs directly — registered via the
  commit pipeline afterwards (documented, acceptable; atomicity applied by the
  committer, not the writer).
* `src/agent/spatial_tools/raster_inspect_tool` / vector inspect open GDAL
  read-only for bounded dumps (agent seam, read-only).

No new bypasses are introduced by 5.0; `src/geospatial` is a library layer, not
an entry point.

## 2. Capability matrix at baseline (master @ 93a7fb0b)

Legend: ● = working & tested on this branch; ◐ = partial/driver-gated;
○ = absent. "Certified" = round-trip tested in this repo (see
`docs/interoperability/format-matrix.md` for the 5.0 final matrix).

| Format / capability | Probe | Read | Window | Write | Remote | Subdataset | Multidim | Metadata norm. | Tests |
|---|---|---|---|---|---|---|---|---|---|
| GeoTIFF | ● | ● | ● | ● | ● (vsicurl) | n/a | n/a | ● | ● io + data |
| COG | ● validator | ● | ● | ● presets | ● | n/a | n/a | ● | ● io_cog |
| VRT | ◐ (driver) | ● | ● | ◐ (via virtual recipes) | ● | n/a | n/a | ◐ | ● roundtrip |
| GeoPackage | ● | ● | n/a | ● | ○ | n/a | n/a | ● | ● io_vector |
| GeoJSON | ● | ● | n/a | ● | ○ | n/a | n/a | ● | ● io_vector |
| Shapefile | ● | ● | n/a | ● (group-atomic) | ○ | n/a | n/a | ● | ● io_atomic |
| CSV | ◐ | ◐ | n/a | ◐ | ○ | n/a | n/a | ◐ | ◐ |
| NetCDF | ● | ● | ● (slice) | ○ | ◐ | ● | ● | ● | ● io_multidim |
| HDF4/5 | ◐ | ◐ | ◐ | ○ | ○ | ● | ◐ (HDF5) | ◐ | ● gated |
| Zarr | ◐ capability-gated | ◐ | ◐ | ○ | ◐ | ● | ◐ | ◐ | ● gated |
| GeoParquet | ◐ capability-gated | ◐ | n/a | ○ | ○ | n/a | n/a | ◐ | ● gated |
| STAC | ● | ● (item→canonical) | n/a | ● (item export) | ● bounded fetch | n/a | n/a | ● | ● io_stac |
| Sentinel-2 SAFE | ● adapter | ● | ● | n/a | ◐ | ◐ (granules) | n/a | ● | ● io_products |
| Landsat C1/C2 | ● adapter | ● | ● | n/a | ○ | n/a | n/a | ● | ● io_products |
| Sentinel-1 SAFE | ● adapter | ● | ● | n/a | ○ | ◐ (measurements) | n/a | ● | ● io_products |
| MODIS HDF | ● adapter | ● | ● | n/a | ○ | ● | n/a | ● | ● satellite_products |

## 3. Gap analysis → 5.0 work items

| # | Capability | Current state | Gap | Risk | Action (this branch) |
|---|---|---|---|---|---|
| 1 | URI / resource abstraction | provider paths + ad-hoc VSI checks | no unified resource-kind model (file/dir/http/vsi/stac/subdataset/memory); Windows Unicode/UNC/traversal under-tested | path corruption, silent misclassification | `resource_uri.h` model + strict parser + `test_io_uri` |
| 2 | Probe / sniff / open contract | `detectProductKind` + provider `supports/resolve` | no single probe pipeline with bounded signature reads; extension-only trust | silent misclassification | `probe.h` `ResourceProbe` chain: URI kind → signature → GDAL driver → product adapter → capability |
| 3 | Format capability model | `FormatProfile` (certified/accessible/unsupported + fidelity flags) | missing explicit window/block/subdataset/mask/overview capability flags | upper layers guess | extend `FormatProfile` + doctor/CLI exposure |
| 4 | Window/block/chunk reading | `RasterReader::readWindow/readFull(budget)` | no block-aligned iteration/tile walker; no overview selection policy | unbounded memory in callers | `readBlock`/`iterateTiles` + overview policy (`exact/nearest/auto`), cancel hooks |
| 5 | NoData / mask / scale-offset | reader reports; `applyScaleOffset` explicit; mask read | alpha/internal/external mask unification partial; Track A policy alignment doc | third NoData semantics | document + unify via doctor/reader contract tests |
| 6 | Overviews | overview count reported | no selection strategy; algorithms could silently sample overviews | wrong results | explicit overview policy in reader |
| 7 | COG | validator + 5 presets + `io:make_cog` | remote range-read proof harness missing | "COG over HTTP = full download" undetected | local HTTP range fixture + benchmark proof |
| 8 | VRT | GDAL-driven; virtual recipes | no explicit VRT source-ref validation before use | silent missing-source success | preflight source existence in conversion paths |
| 9 | NetCDF/HDF multidim | `MultidimView` lazy slices + tests | time coordinate mapping (CF) partial; doc of support boundary | fabricated time axis | time-axis coordinate read; explicit unknown handling |
| 10 | Zarr | GDAL driver presence unknown on Windows build | no capability detection / explicit unsupported | silent failure | driver-gated capability + diagnostics (no new runtime dep) |
| 11 | GeoParquet | GDAL/Parquet driver presence unknown | same | same | capability-gated, explicit unsupported |
| 12 | Product adapters | Landsat/S2/S1/MODIS in `products/*` + legacy `SatelliteProducts` | S2 multi-resolution grid awareness, Landsat C2 scale/offset from MTL, S1 measurement/annotation asset enumeration, product registry seam, completeness verdicts | wrong band roles / silent resample | deepen adapters; `ProductAdapterRegistry`-style seam inside `src/geospatial` |
| 13 | STAC | item ⇄ canonical + tests | Catalog/Collection traversal, asset-role selection, remote fetch bounds | only-item support | extend mapper + bounded fetch with timeout |
| 14 | Remote I/O | vsicurl defaults + handle pool (data layer) | geospatial layer has no remote probe; no explicit timeout/retry/offline taxonomy in GeoError | hangs, misreported errors | remote probe (bounded HEAD/range) + error codes |
| 15 | Cache/staging | `RemoteDatasetPool` + GDAL block cache bounds (data layer) | documented cache taxonomy for the foundation layer | unbounded growth claims | document + benchmark assertions only (reuse data-layer pool) |
| 16 | Vector/table interop | batched reader/writer | encoding (Chinese field names), date/time, 64-bit round-trips untested | silent mojibake | fidelity tests with UTF-8 fixtures |
| 17 | Export service | `io:convert_format`, writers, doctor | no unified ExportRequest/options schema + loss-negotiation warnings | silent metadata loss | `convert/` extension: conversion diff report |
| 18 | Integrity/validation | doctor findings | product completeness (complete/partial/invalid/unsupported-version) | disguised partial products | product completeness verdicts |
| 19 | I/O error model | `GeoError` 11 codes | missing NetworkError/Timeout/ResourceExhausted/InvalidMetadata/CorruptData taxonomy; GDAL context capture partial | undiagnosable failures | extend `ErrorCode` + scoped CPL error capture |
| 20 | Concurrency | serialized handles (data layer); reader contract | foundation-layer thread-safety not documented/tested | cross-thread handle sharing | document + test reader-per-thread |
| 21 | Cancellation | operator context wiring | core iteration lacks cancel hooks | unstoppable exports | cancel callback in tile iteration/writers |
| 22 | CLI | `data inspect/doctor` | probe/validate/convert/product/stac surface | discoverability | extend CLI command set (existing envelope style) |
| 23 | Pi/MCP | `io:` prefix allow-list | no data.* / product.* / format.* tools | Pi cannot ground data questions | thin MCP tools over the core (no parsers in wrappers) |
| 24 | Benchmarks | `benchmark_io` (open/inspect/window/multiband/scan/write/reproject/COG/vector/remote-sim) | NetCDF slice, product probe, STAC, export bench | unmeasured claims | extend harness |
| 25 | Docs | 4.0 docs ported under `docs/io/` | products/interoperability docs, matrices, examples | docs drift | write + CI-independent |

## 4. Non-goals (inherited from the 5.0 goal §81)

Scientific algorithm library, Dataset/Experiment platform, cartography,
workbench UI, Pi runtime, model runtime, plugin SDK, TaskCenter/JobEngine,
STAC server, object storage, distributed compute, 100 GB fixtures.

## 5. Ownership boundaries with parallel tracks

* **A (algorithms)**: consumes `RasterReader`/canonical metadata/product
  metadata; 5.0 does not rewrite kernels.
* **B (solutions/templates)**: reads product modality/band-role semantics.
* **C (desktop UX)**: consumes probe/inspect/export services + options schemas;
  no UI in this branch beyond existing dialogs.
* **D (dataset/experiment)**: may key asset identity onto `SourceKey` +
  canonical metadata + product identity; seam is the existing DataManager
  registration path (no hard dependency on unmerged D code).

## 6. Verification strategy

* Qt-free core + light GDAL tests (`test_io_*`) keep the loop fast on Windows.
* Full-chain integration build (operators/CLI) once per milestone; regression
  subsets per affected target; `ctest -j1`, build `-j2` (drop to `-j1` on
  memory pressure).
* Remote I/O proven against a local HTTP range-request fixture — no public
  network dependency for core tests.
