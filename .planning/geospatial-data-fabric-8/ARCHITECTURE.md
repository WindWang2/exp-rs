# ARCHITECTURE — 8.0 design notes

## Layering after 8.0

```
hosts (src/app main, src/cli main_cli)          [Qt + QGIS]
   └── installs at startup:
        sicnu::data::installGeospatialInputIdentityResolver()
src/data (sicnu_data)                            [Qt core only]
   execution_identity_resolver  (seam, 7.0)
   execution_identity_bridge    (QString adapter, 8.0)  → links Sicnu::Geospatial PRIVATE
src/processing (temporal_workspace collectors)
   consults executionIdentityResolver() when a remote input
   cannot resolve through the DataManager catalog (fail-closed)
src/geospatial (sicnu_geospatial)                [Qt-free: C++20 + GDAL + jsoncpp]
   remote/remote_identity_token  ← remote_source_validator ← resource_uri
   util/sha256, util/time_normalization
   stac/*, multidim/*, doctor/*, formats/* (unchanged authorities)
```

Dependency rules preserved:
- `sicnu_geospatial` stays Qt-free (CPL owns all network I/O — ADR 0139).
- `sicnu_data` gains a PRIVATE `sicnu_geospatial` link only; its forbidden-link
  guard (Qt6::Widgets / qgis_gui / Qt6::Network) still passes and the public
  interface stays Qt-only headers.
- Collectors depend on the *seam* (`execution_identity_resolver`), not on
  geospatial: hosts choose the identity policy; tests install fakes.

## Cross-track seams touched (minimal, additive)

| File | Owner track | 8.0 footprint |
|---|---|---|
| `src/processing/algorithms/temporal/temporal_workspace.cpp` | execution plane 7.0 | two narrow fallback branches consulting the installed resolver |
| `src/app/main.cpp`, `src/cli/main_cli.cpp` | host composition | one install call (+include) each |
| `tests/CMakeLists.txt` | all tracks | appended target wiring only |

## Key invariants

1. Identity tokens are content identities, not authorization: "" ⇒ uncacheable.
2. `remoteIdentityToken` never downloads content (bounded probe ≤1 MiB).
3. Token basis excludes credential-shaped query keys entirely (a signed URL is
   one opaque credential bundle); non-credential queries (content selectors)
   stay part of identity.
4. STAC wire forms are never rewritten; UTC fields are parse-time derivations.
5. String-axis selection is exact (label or offset-normalized equal instant) —
   no nearest-instant guessing.
6. Doctor findings remain advice-only (`auto_fixable=false` everywhere).
7. VSI handler objects are owned by GDAL from `InstallHandler` onward.
