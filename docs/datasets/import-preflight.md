# Import preflight explanation report (`sicnu.import.preflight/1`)

R25-B feature 1. `ImportPreflight::inspect(path, manager, options)` in
`src/data/import_preflight.h` answers one read-only question before an
import: **what would happen, and why?** Nothing is registered, no network
packet leaves the process, and no catalog state changes.

## When to use it

- The import UI (D-track) renders the report before a commit so the user
  sees the recognized sensor/bands/CRS/time/NoData/quality, the ambiguity
  candidates, and the exact typed reason when a source is not importable.
- CLI / agent surfaces print `toJson()` for scripted triage of directories
  full of unknown provenance.
- Batch importers call it per file to produce a machine-readable per-item
  outcome instead of ok/failed counters.

## Verdicts

| verdict | meaning |
|---|---|
| `importable` | the source opens, facts are reported, no error findings |
| `needs_user_choice` | registering would proceed but a decision is left implicit (container subdatasets, ambiguous adapter claims) |
| `refused` | an error-severity finding exists (missing path, unreadable source, invalid product, unsupported product family/version) |
| `out_of_scope` | remote spelling (`http(s)://`, `s3://`, `/vsi*`) — local preflight never probes; register remote sources through their dedicated path |

## What the report carries

- **identity** — `li1:v1:` token, strength (`content`/`metadata`/empty),
  size and hashed bytes (fail-closed: unprovable is reported, never guessed).
- **dedup prediction** — read-only `DataManager::findByPath` lookup:
  `already_registered`, the existing asset id and revision, so the caller
  knows `registerSource` will reuse instead of adding.
- **recognition** — winning adapter id + `ProductKind`, the full candidate
  list (`adaptersAccepting()`), product completeness and missing
  constituents for directory products.
- **raster facts** — driver, size, band count, CRS (authid + WKT),
  geotransform/extent, per-band dtype/role/unit/NoData/wavelength,
  subdataset count; product semantics (sensor, platform, processing level,
  radiometric state, acquisition time, cloud cover, GSD) preferred from the
  product adapter when one claimed the source.
- **vector facts** — driver, layer count, layer names.
- **findings** — typed `{code, severity, message, remediation}` entries.
  Codes: `import.empty_path`, `import.path_missing`, `import.unreadable`,
  `import.remote_out_of_scope`, `import.identity_unprovable`,
  `import.already_registered`, `import.adapter_ambiguous`,
  `import.product_incomplete`, `import.product_invalid`,
  `import.product_unsupported`, `import.product_unsupported_version`,
  `import.container_subdatasets`, `import.crs_missing`,
  `import.geotransform_missing`, `import.nodata_undeclared`,
  `import.band_roles_unknown`.

## Contract notes

- Warnings never refuse an openable raster; `refused` is reserved for
  error-severity findings.
- The report never gates `registerSource` by itself — registration keeps
  its own typed refusals; the report explains them ahead of time.
- Options can skip the identity hash (`probeIdentity=false`) or the dedup
  query (`probeDedup=false`) for pure-recognition passes.

Tests: `tests/test_r25_b_import_preflight.cpp` (healthy raster, bare raster
quality findings, corrupt/missing/empty refusals, remote out-of-scope,
identity proof, dedup prediction against a production DataManager with
zero catalog side effects, netCDF container user-choice, vector facts,
schema serialization).
