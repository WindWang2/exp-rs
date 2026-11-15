# Oracles and Public Seams

These seams are explicitly specified by the R31-T01 request, so tests target them rather than private helpers or source-string checks.

## E2E-1 — reports vs artifacts

**Seam:** existing registry→JobEngine→TaskCenter→ToolCallDispatcher→OutputCommitter/DataManager.

- Mixed `driver/kind/path/digest/output` report: all values survive, no outputLayerPath/commit/cache/autoLoad/asset/provenance.
- `io:inspect`, `io:doctor`, `io:subdatasets`, `io:verify_dataset`: fixture hash and directory snapshot unchanged, asset/revision/lease facts unchanged, no `*_committed`, no false verified=true.
- `io:translate` control: real GDAL pixels and committed asset/provenance reopen correctly.

## E2E-2 — source/VRT/task/delivery

**Seam:** DataManager and existing VRT provider→real task reader→OutputCommitter.

- Independent GDAL pixel/mask/NoData/CRS/grid comparisons for same-CRS/reproject/rotation and nearest/bilinear.
- Source NoData, valid zero, valid value equal to fill, uncovered fill and NaN stay distinct or are explicitly refused under a declared limitation.
- Old/new readers retain their own generation; failed publish/cancel cannot remove reachable dependencies; released generations converge.

## E2E-3 — science

**Seam:** real small raster/ROI→training extraction→classification/postprocess→evaluation/provenance.

- Literal independent X/y/groupIds/order/seed and NoData/overlap references.
- BudgetExceeded precedes the bounded collection/matrix allocation; cancel publishes no partial result.
- Any bounded/chunked implementation is pixel-identical to the R30 reference, including component numbering and sieve votes.

## E2E-4 — model

**Seam:** existing ModelRuntimeRegistry/provider/model execution service.

- Loopback configurations produce distinct sessions without exposing secrets; equal bytes/config share only as allowed.
- A's cancel never affects B; the next uncancelled request succeeds; release is directed and live holders remain valid.
- Controlled digest/sample interleavings cannot create a persistent mixed identity; plan/admission includes output classes and matches real allocations.

## E2E-5 — workflow recovery/cache

**Seam:** PipelineRunCoordinator and existing TaskCenter/chunked-run/OutputCommitter delivery.

- Relative checkpoint is pinned before a controlled CWD change; only the original file updates, with lock/attempt/provenance intact.
- Error/missing asset recovery with identical bytes restores usability and stable AssetId; changed bytes cannot hit; unchanged hits do not advance revision/reload.

Every entry in `TEST_LOG_INDEX.md` and `ACCEPTANCE.md` must bind these observations to real commands on the current HEAD. Missing platforms/providers are NOT_EXECUTED or ENV_LIMITED, not passes.
