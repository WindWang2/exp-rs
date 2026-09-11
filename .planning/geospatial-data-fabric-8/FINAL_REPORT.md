# FINAL REPORT — Cloud-Native Geospatial Data Fabric 8.0

Branch: `feat/geospatial-data-fabric-8` (worktree from `origin/master` @ `322dfd3876`)
Status: feature-complete; local evidence recorded; adversarial review below.

## Problem & baseline

The 7.0 goal series (#823, #833–#835) shipped a strong Qt-free geospatial I/O
foundation: remote validators, bounded HTTP fetch, a `/vsirangecache/` VSI
handler, STAC search, multidim slices, atomic writers, doctor v2, and a
canonical metadata vocabulary. The 8.0 audit (see `BASELINE.md`) found the
remaining gaps were *integration-depth* gaps, not missing frameworks:

1. the execution-identity seam was contract-only — remote inputs could never
   participate in cache reuse;
2. STAC datetimes were kept verbatim and series ordered by raw strings
   (mixed offsets order wrongly);
3. the range cache lacked fault/concurrency evidence and carried a real
   handler-lifetime bug (P0);
4. GeoParquet was read-only by claim although the local GDAL proves write
   fidelity;
5. multidim could not select on the string datetime axes real EO cubes use;
6. doctor/CLI surfaces stopped short of the identity/cache story.

## What 8.0 delivers (behavior changes)

See the commit messages and `TEST_MATRIX.md`; summary:

- **P0 fix** — range-cache VSI handler lifetime (use-after-free/double-free
  with GDAL ≥3.9 RemoveHandler semantics; reproduced under ASan, now owned by
  GDAL, never static).
- **Fixture hardening** — loopback HTTP fixtures no longer hang teardown
  (accept-wake + bounded recv + in-flight client shutdown).
- **Remote identity contract + token** — `remoteIdentityToken()`: fail-closed
  (strong ETag only), credential-safe (credential-shaped query values
  stripped from the SHA-256 basis; re-signed URLs keep identity), never a
  content download (1 KiB bounded probe).
- **Execution-cache bridge** — hosts install the geospatial resolver;
  `fingerprintInputsForOperatorParams` fingerprints unregistered remote
  inputs through the token (`valueDomain=remote_identity`); "" stays
  uncacheable — fail-closed.
- **STAC UTC** — strict ISO-8601/RFC 3339 parsing (Qt-free), normalized UTC
  instants on `StacItem` (wire forms verbatim), instant-based series order
  with deterministic tie-breaks, duplicate-acquisition reporting.
- **Range-cache 8.0** — fault evidence (concurrency, resets, overlap,
  changed content, COG overviews) + the `updateEntrySize` unknown-size guard.
- **Multidim** — string datetime axis capture (with JSON symmetry fix for
  7.0's dropped numeric axis values), exact-label / equal-instant selection,
  typed misses, end-to-end bounded EO-cube workflow over netCDF-4.
- **GeoParquet** — certified round-trip through the atomic staged writer
  (driver-gated); format profile upgraded with honest capability gating;
  7.0's broken `GDALCreateCopy` assumption replaced.
- **Doctor 3.0** — `cacheability`, `reproducibility`, `resampling_risk`,
  `multidim_axes` (advice-only; `auto_fixable` stays false).
- **CLI** — `data identity [--revalidate]`, `data cache check [--bytes N]`.
- **Docs** — `docs/io/cloud-credentials.md`, certified-formats row,
  stac-interop section, CHANGELOG entry.

## Compatibility & migration

- All new APIs are additive; no existing signature changed.
- `buildTemporalSeries` behavior change is the documented *intent* (temporal
  order) — raw-string order for mixed offsets was the defect; ties break by
  item id (deterministic).
- GeoParquet certification upgrades a claim, gated by driver capability at
  runtime; capability queries keep degrading honestly.
- Unregistered remote inputs that previously *failed* fingerprinting now
  succeed when their identity is provable — strictly wider reuse eligibility
  under the same fail-closed rule; unprovable inputs fail exactly as before.

## Adversarial review

Recorded in `REVIEW_LOG.md` (findings, classifications, remediation).
