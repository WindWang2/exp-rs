# GOAL — Cloud-Native Geospatial Data Fabric 8.0

Repository: https://github.com/WindWang2/exp-rs
Branch: `feat/geospatial-data-fabric-8` (worktree `../exp-rs-geospatial-data-fabric-8`)
Baseline: `origin/master` = `322dfd3876c34ed62b42846598cacd711c8c91d6` (verified 2026-09-10,
identical to the planning snapshot; no drift).

Mission: deepen the post-#823 Cloud-Native Geospatial I/O foundation into a unified,
governed, identity-aware spatial data fabric across local files, COG/HTTP, STAC,
multidimensional data, GeoParquet, Zarr/NetCDF/HDF, and cloud object sources — while
`src/geospatial` stays the single I/O authority.

## Track contract (work packages A–I → what 8.0 actually does here)

| Pkg | Prompt ask | Baseline state (verified) | 8.0 action |
|---|---|---|---|
| A | Remote identity as first-class contract | `RemoteSourceValidator`/`RemoteSourceIdentity` complete for validators/states; canonical request URL is private; no optional content digest | Additive: expose canonical identity + optional bounded digest capture; comparison semantics already Same/Changed/Unknown/Offline — document + test |
| B | Execution-cache identity bridge | `execution_identity_resolver` seam is **SEAM ONLY**, zero collectors consult it | Implement geospatial fail-closed identity token; wire collectors in `fingerprintInputsForOperatorParams`; install at hosts; tests |
| C | Range cache 8.0 | Cache correct for coalescing/LRU/stale-policy; fault tests cover single-connection faults | Add concurrent-reader/overlapping/reset/changed-content/COG-overview evidence; fix `updateEntrySize` unknown-size hazard; no policy change without evidence |
| D | STAC production surface | Search/pagination/filters/bounded searchAll done; datetime kept verbatim; series sorts raw strings | UTC-instant normalization (mixed offsets), deterministic series order + duplicate handling; tests over loopback fixture |
| E | Multidim EO cubes | Named dims, numeric coordinate axes, nearest/exact, windows, blockShape, NoData | String/datetime axis capture + selection; end-to-end bounded EO cube workflow test (netCDF driver-gated) |
| F | GeoParquet round-trip | Read-only "Accessible" profile; ogr_Parquet plugin present locally and can write | Certified round-trip where locally proven: write/read/CRS/null/empty semantics; honest profile upgrade |
| G | Cloud/object credentials | CPL-owned creds; redaction for userinfo + credential-shaped query | Document capability/security boundary; redaction tests for /vsis3-class spellings; no secret persistence |
| H | Data Doctor 3.0 | doctor v2 with identity/format/grid/remote sections | Additive checks: cacheability verdict, multidim availability, modern-vector posture, reproducibility advice |
| I | CLI/operator/agent projections | `data inspect|doctor|probe|capabilities|product|stac` | `data identity` (probe/revalidate), `data cache` (config/telemetry), richer `data stac`; help descriptors synced |

Non-negotiable: Pi stays the single agent loop; no second scheduler; RSOperatorRegistry,
IModelRuntime, QGIS, DatasetStore/ExperimentStore, and the governed publish seams stay
authoritative. All new surface is additive to existing seams.
