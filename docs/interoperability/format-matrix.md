# Format Support Matrix (Foundation 5.0)

> Certified = round-trip tested **in this repository** (tests below).
> Accessible = GDAL can typically open it; exp-rs makes no fidelity claim.
> Runtime truth: `sicnu_geo_rs_cli data capabilities <dataset>` — a profile
> whose driver is missing in the loaded GDAL build reports
> `unavailable_in_build`. "GDAL has a driver" is never reported as "tested".

| Format | Probe | Read | Window read | Write | Remote range | Subdataset | Multidim | Metadata normalized | Certification | Tests |
|---|---|---|---|---|---|---|---|---|---|---|
| GeoTIFF / BigTIFF | ● | ● | ● | ● | ● | n/a | n/a | ● | Certified | test_io_raster_contract, test_io_fidelity, test_io_roundtrip_matrix |
| COG | ● (structural validator) | ● | ● | ● (safe presets + validation) | ● | n/a | n/a | ● | Certified | test_io_cog, test_io_probe |
| VRT | ● | ● | ● | ◐ (recipe-driven virtual rasters) | ● | n/a | n/a | ◐ | Certified (simple source lists) | test_io_roundtrip_matrix |
| NetCDF (CF) | ● | ● | ● (variable/time/level slices) | ○ (export out is certified; CF writing is not) | ◐ | ● | ● | ● | Certified (read/slice) | test_io_multidim |
| HDF4 / HDF5 | ● (driver-gated) | ◐ | ◐ | ○ | ○ | ● | ◐ (HDF5) | ◐ | Accessible | driver-gated suites |
| Zarr | ◐ capability-gated | ◐ | ◐ | ○ | ◐ | ● | ◐ | ◐ | Accessible when the GDAL Zarr driver is present | capability queries only |
| GeoParquet | ◐ capability-gated | ◐ | n/a | ○ | ○ | n/a | n/a | ◐ | Accessible when GDAL carries the Parquet/Arrow driver | capability queries only |
| GeoPackage | ● | ● | n/a | ● (atomic) | ○ | n/a | n/a | ● | Certified | test_io_vector_contract, test_io_roundtrip_matrix |
| GeoJSON / GeoJSONSeq | ● | ● | n/a | ● (atomic) | ◐ | n/a | n/a | ● | Certified | test_io_vector_contract |
| Shapefile | ● | ● | n/a | ● (sidecar group published atomically) | ○ | n/a | n/a | ◐ (field-width limits documented) | Certified | test_io_atomic_failures |
| FlatGeobuf | ● | ● | n/a | ● | ◐ | n/a | n/a | ◐ | Accessible | — |
| CSV (XY) | ● | ● | n/a | ● | ○ | n/a | n/a | ◐ (CRS never guessed) | Accessible | — |
| PNG / JPEG | ● | ● | ○ | ● (visualization only) | ○ | n/a | n/a | ○ (no CRS/NoData fidelity) | Accessible | — |
| STAC Item | ● | ● (item → canonical) | n/a | ● (canonical → item) | ● (bounded fetch) | n/a | n/a | ● | Certified | test_io_stac |
| Remote HTTP(S) | ● (bounded remote probe) | ● (through /vsicurl/ and friends) | ● | ○ | ● | — | — | inherited from format | Accessible (per-format) | test_io_remote_range |

## Notes and limitations

* **Zarr / GeoParquet**: this track introduces no new runtime dependency.
  Both formats appear through the capability model when the loaded GDAL
  carries the driver, and degrade to explicit `unavailable_in_build`
  otherwise. Certification (round-trip proof) is a follow-up.
* **NetCDF writing**: exporting *out of* NetCDF (slice → GeoTIFF) is
  certified; writing CF-compliant NetCDF is not offered by this layer.
* **Shapefile**: field-name truncation and the 2 GB limit are format
  constraints; the atomic group publish covers the sidecar family.
* **Remote aux files**: GDAL probes `<url>.aux.xml` and friends; on remote
  origins these probes cost round trips (disable with `GDAL_PAM_ENABLED=NO`,
  as the remote range test does).
