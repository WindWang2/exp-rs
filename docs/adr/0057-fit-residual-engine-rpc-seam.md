# ADR 0057: Consolidate the Fit/Residual Engine and Give RPC an Interface Seam

## Status
Accepted

## Context
`RsGeoreferencingSession::refit()` hand-orchestrated enabled-GCP collection,
min-count gating, the RPC before/after double-fit, and per-point source-pixel
residuals — math duplicated with `pixelRms()`, a triplicated min-GCP probe,
and shell re-validation of what `createWarpSnapshot()` already gates.
`QgsRpcGcpTransformer` configuration lived behind concrete-only methods,
forcing four `dynamic_cast<QgsRpcGcpTransformer>` sites.

## Decision
1. **Host the fit/residual engine on `QgsGeorefTransform`**: static
   `fit(gcps, method, rasterPath, demPath, demZOffset, invertYAxis)` returns
   one `RsGeorefFitResult` (moved here from the session header) — collection,
   gating, RPC double-fit, residuals, RMS. Shared `enabledGcpCount` /
   `collectEnabledGcps` / `minimumGcpCountFor` statics absorb the triplicated
   probe and shell re-validation; `refit()` collapses to one call.

2. **Give RPC a real interface seam**: one optional virtual
   `QgsGcpTransformerInterface::setRpcOptions(sourceRasterPath, demPath,
   zOffset, refine)` (default no-op returning FALSE) plus a `demPath()`
   query; the four downcasts are deleted, and the clone path copies RPC state
   through the implementation's own `clone()` instead of
   `copyRpcStateIfPresent()`.

## Consequences
- **One fit seam**: residual math appears once; session shrinks ~130 lines;
  RMS values, error strings, and RPC refinement semantics unchanged.
- **No fragile downcasts**: RPC configuration flows through the interface;
  `RsWarpTask` uses the new concrete `cloneTransform()`.
- **Vendored files touched minimally**: two additive virtuals on the
  interface; `setRpcOptions` gains the source-path argument.
