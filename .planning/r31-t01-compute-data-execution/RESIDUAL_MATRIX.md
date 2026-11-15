# Residual Matrix

Statuses reflect evidence on base `85ce917bea0e`, not issue titles. `CONFIRMED_RESIDUAL` may begin from a direct production-code proof, but DONE still requires the WP oracle. Static candidates that need an execution reproduction remain NEEDS_REPRO.

| WP | Investigation status | Current evidence and next oracle | Delivery status |
|---|---|---|---|
| 01 | DONE | Latest upstream/PR/issues/branches/instructions refreshed; #2061 full file set, patch and review document read; configure exit 0 with real Ninja files. | DONE (facts refreshed; residual rows below track delivery) |
| 02 | CONFIRMED_RESIDUAL | `task_center.cpp:2204-2209` collects arbitrary strings; `:3371-3394` prefers `output`/first string/result output without descriptor semantics. Mixed-report public-seam regression added. | IMPLEMENTED, RED pending (test-only baseline build queued behind stale build lock) |
| 03 | CONFIRMED_RESIDUAL | IO report schemas and dispatcher indicate metadata may enter commit. Real-registry chain test added (`io:inspect/doctor/subdatasets/verify_dataset` + real producer). | IMPLEMENTED, unverified |
| 04 | CONFIRMED_RESIDUAL | `output_committer.cpp:434-444` returned existing ID solely from path after structural open; no asset state/content/provenance check. | IMPLEMENTED (state/kind/provenance-aware reconcile + finalize-manifest content gate in chunked_run), unverified |
| 05 | CONFIRMED_RESIDUAL | `pipeline_run_coordinator.cpp:1406` assigned the resume argument verbatim to `state->checkpointPath`; persist used that member. | IMPLEMENTED (absolute path pinned before the coordinator thread), unverified |
| 06 | IN_PROGRESS | #2055 CAS/wait baseline identified. Rerun host-lane/wait/cartography tests after output changes; no new race claimed. | NOT_EXECUTED |
| 07 | CONFIRMED_RESIDUAL | Recipe declares FillValue only for uncovered pixels (`virtual_raster_recipe.h:45-48`), but direct VRT wrote source `NODATA=fill` (`virtual_raster_source_provider.cpp:354-360`) and warp set both source/destination NoData (`:757-773`). | IMPLEMENTED (source NoData from input only; warp target-only fill; `preflight.fill_value_alias` disclosure), unverified |
| 08 | CONFIRMED_RESIDUAL | `stagedMatchesPinnedGrid` checked width/height/CRS and transform 0/1/3/5 only, bandCount>=1; omitted required bands/dtype/NoData/mask and transform 2/4. | IMPLEMENTED (`sicnu.vrt.prepared/1` sidecar: content SHA-256 + full geometry re-hash before reuse), unverified |
| 09 | NEEDS_REPRO | Post-publish sweep exists; whether an old delayed GDAL reader remains valid through its handle and how leases map must be measured, not assumed. | NOT_EXECUTED |
| 10 | IN_PROGRESS | #2059 propagation implementation is baseline. Need A→B→C delete/replace/restore and duplicate/failed-generation facts. | NOT_EXECUTED |
| 11 | CONFIRMED_RESIDUAL | `rs_training_data_extraction.cpp:159-174` explicitly said pixelOwner is already materialized and unbounded before maxCandidates. | IMPLEMENTED (`RsPixelRasterizer::rasterizeEach` + PixelSink admission before unique-candidate materialization), unverified |
| 12 | CONFIRMED_RESIDUAL | candidates, byClass copies, matchCount, keepRows, cappedRows and X×bands coexist without a complete byte admission; portable Fisher–Yates/cap-after-NoData must remain exact. | NOT_EXECUTED |
| 13 | CONFIRMED_RESIDUAL | R30 global CC/sieve is baseline. `rs_post_process.h:7-10` still declares a full-Mat path; `labelSameValueComponents` retains full records/frontier and only the INT_MAX component-count guard exists. | NOT_EXECUTED |
| 14 | IN_PROGRESS | Wide/stable evaluation baseline identified. Reuse existing numerical/e2e suites; add only newly exposed gaps. | NOT_EXECUTED |
| 15 | CONFIRMED_RESIDUAL | `ModelRuntimeRegistry::release` compared identity to text after final `\|`, incompatible with the `\|load:` contract suffix and package fields. | IMPLEMENTED (raw identity releases all device/load configs; `\|load:` releases exactly one), unverified |
| 16 | CONFIRMED_RESIDUAL | digest memo re-check overwrote an existing entry's sample while returning its old digest — a mixed entry later probes trust; hash/sample straddle could pair snapshots. | IMPLEMENTED (wholesale replace + return own digest; before/after probe pair gate), unverified; concurrent-interleaving oracle needs a seam — see LEDGER |
| 17 | CONFIRMED_RESIDUAL | `effectiveBatchSize` used a fixed 4× input-channel multiple and `batchWorkingSetMiB` a 2× bundle; neither scaled with output classes while `estimateExecution` already did. | IMPLEMENTED (`declaredOutputChannels` shared basis; undeclared heads keep the disclosed bundle), unverified |
| 18 | IN_PROGRESS | Per-invocation cancellation baseline preserved. Real HTTP/available CPU and optional ORT recovery matrix pending. | NOT_EXECUTED |
| 19 | IN_PROGRESS | ADR0172 production verifier authority is #2055 baseline; output/report and reconcile changes must retain truthful verification facts. #1601 is FIXED_OPEN for production docs, not a new migration task. | NOT_EXECUTED |
| 20 | IN_PROGRESS | Five-chain acceptance, independent R1/R2, final refresh/rebase and PR remain. | IN_PROGRESS |
