# HANDOFF — R35-T03
- To T04 (chunk/exec): TilePlan::slice now throws GeoError(InvalidArgument) on out-of-grid tile
  indices (was silent int UB). All current in-repo callers loop in-grid (verified: raster_reader
  iterateTiles, lab_grader_kernels, perf test); new T04 code must respect the typed contract.
  inspectOpenRaster(void* GDALDatasetH, path, options) added in canonical_metadata.h for any
  consumer that holds a dataset handle and needs same-generation metadata.
- To T01 (training consumers of DatasetManifest): fromJson is now strict — present-but-wrong-typed
  fields (entries/source_assets as strings, negative/fractional/string revision, tags/bands with
  non-strings, schema/resolution/label_schema/spatial_extent/provenance as non-objects) return
  Result::failure("dataset.manifest_invalid") instead of folding to empty/0. revision 0 remains
  the legal "not pinned". Producers must keep writing integral revisions (all in-repo writers do).
- To T05/T06: catalog instrument queries are exact-token case-insensitive on every backend
  ("MSI" no longer substring-matches "MSI2" in any UI that feeds CatalogQuery.sensorInstruments).
