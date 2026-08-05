# ADR 0056: Collapse the GCP type Duplication onto QgsGcpPoint

## Status
Accepted

## Context
`RsGeorefGcpPair` (session) duplicated `QgsGcpPoint`'s four core fields
plus pointType, forcing the shell to convert both ways around the `.points`
codec. The save conversion constructed an empty `QgsCoordinateReferenceSystem()`,
so saved GCPs lost their destination CRS. `QgsGcpPoint::mResidual` was a
third residual store whose only writer was that save path, and its doc
referenced the deleted `QgsGCPList::updateResiduals()`.

## Decision
1. **Session stores `QgsGcpPoint` values**; `RsGeorefGcpPair` is deleted and
   both shell conversion loops removed. GCPs are created with the panel CRS
   (or the coord-dialog CRS) at add time.

2. **`.points` v2 gains an optional 10th `crs` column** (authid); the loader
   prefers it per-point and falls back to the caller-supplied CRS for older
   files.

3. **`QgsGcpPoint::residual()`/`mResidual` removed** — residuals live only in
   `RsGeorefFitResult`, pushed to the view layer via
   `QgsGeorefDataPoint::setResidual()`. The codec writes format-compat zeros.

## Consequences
- **Lost-CRS bug fixed**: saved GCPs round-trip their destination CRS (regression-tested).
- **One GCP vocabulary** across session, warp snapshot, codec, and table.
- **One residual owner** (`RsGeorefFitResult`); stale doc references gone.
