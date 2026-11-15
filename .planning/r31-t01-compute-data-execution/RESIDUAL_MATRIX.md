# Residual Matrix

Statuses reflect evidence on base `85ce917bea0e`, not issue titles. `CONFIRMED_RESIDUAL` may begin from a direct production-code proof, but DONE still requires the WP oracle. Static candidates that need an execution reproduction remain NEEDS_REPRO.

| WP | Investigation status | Current evidence and next oracle | Delivery status |
|---|---|---|---|
| 01 | IN_PROGRESS | Latest upstream/PR/issues/branches/instructions refreshed; #2061 full file set, patch and review document read. Fresh configure hit missing OpenCV discovery, then host-lock exit 75; retry pending. | IN_PROGRESS |
| 02 | CONFIRMED_RESIDUAL | `task_center.cpp:2204-2209` collects arbitrary strings; `:3371-3394` prefers `output`/first string/result output without descriptor semantics. Mixed-report public-seam regression added. | IN_PROGRESS (RED not yet executed) |
| 03 | NEEDS_REPRO | IO report schemas and dispatcher indicate metadata may enter commit. New real-registry chain test is next after WP02 slice. | NOT_EXECUTED |
| 04 | CONFIRMED_RESIDUAL | `output_committer.cpp:434-444` returns existing ID solely from path after structural open; no asset state/content/provenance check. Need Error→same bytes recovery and changed-content cases. | NOT_EXECUTED |
| 05 | CONFIRMED_RESIDUAL | `pipeline_run_coordinator.cpp:1406` assigns the resume argument verbatim to `state->checkpointPath`; persist uses that member. Need controlled CWD-change test; #2055 attempt cap/run lock preserved. | NOT_EXECUTED |
| 06 | IN_PROGRESS | #2055 CAS/wait baseline identified. Rerun host-lane/wait/cartography tests after output changes; no new race claimed. | NOT_EXECUTED |
| 07 | CONFIRMED_RESIDUAL | Recipe declares FillValue only for uncovered pixels (`virtual_raster_recipe.h:45-48`), but direct VRT writes source `NODATA=fill` (`virtual_raster_source_provider.cpp:354-360`) and warp sets both source/destination NoData (`:757-773`). Source=-9999 can lose its invalid semantics. Valid-fill remains a representation/mask limitation requiring explicit disclosure or a mask; GDAL oracle still required. | NOT_EXECUTED |
| 08 | CONFIRMED_RESIDUAL | `stagedMatchesPinnedGrid` checks width/height/CRS and transform 0/1/3/5 only, bandCount>=1; omits required bands/dtype/NoData/mask and transform 2/4. | NOT_EXECUTED |
| 09 | NEEDS_REPRO | Post-publish sweep exists; whether an old delayed GDAL reader remains valid through its handle and how leases map must be measured, not assumed. | NOT_EXECUTED |
| 10 | IN_PROGRESS | #2059 propagation implementation is baseline. Need A→B→C delete/replace/restore and duplicate/failed-generation facts. | NOT_EXECUTED |
| 11 | CONFIRMED_RESIDUAL | `rs_training_data_extraction.cpp:159-174` explicitly says pixelOwner is already materialized and unbounded before maxCandidates. Need admission at collection growth. | NOT_EXECUTED |
| 12 | CONFIRMED_RESIDUAL | candidates, byClass copies, matchCount, keepRows, cappedRows and X×bands coexist without a complete byte admission; portable Fisher–Yates/cap-after-NoData must remain exact. | NOT_EXECUTED |
| 13 | CONFIRMED_RESIDUAL | R30 global CC/sieve is baseline. `rs_post_process.h:7-10` still declares a full-Mat path; `labelSameValueComponents` retains full records/frontier and only the INT_MAX component-count guard exists. Cancellation/no-partial-dst tests exist, but typed memory/disk admission and cross-tile equivalence are absent. | NOT_EXECUTED |
| 14 | IN_PROGRESS | Wide/stable evaluation baseline identified. Reuse existing numerical/e2e suites; add only newly exposed gaps. | NOT_EXECUTED |
| 15 | CONFIRMED_RESIDUAL | `ModelRuntimeRegistry::release` compares identity to text after final `|`, incompatible with `|load:` contract suffix and package fields; directed unload needs full structured matching. | NOT_EXECUTED |
| 16 | CONFIRMED_RESIDUAL | digest memo re-check can overwrite an existing entry's sample while returning its old digest; concurrent hash/sample snapshots can disagree. Need controlled oracle. | NOT_EXECUTED |
| 17 | CONFIRMED_RESIDUAL | `inference_plan.cpp:912-919` estimates input + one output plane; `tile_inference_engine.cpp:4546-4561` repeats a fixed approximation and does not scale with output.classes. Multi-task full-VRAM-ceiling behavior is explicitly disclosed at `:4518-4526`. Need output-plane/working-set oracle and real CPU equivalence. | NOT_EXECUTED |
| 18 | IN_PROGRESS | Per-invocation cancellation baseline preserved. Real HTTP/available CPU and optional ORT recovery matrix pending. | NOT_EXECUTED |
| 19 | IN_PROGRESS | ADR0172 production verifier authority is #2055 baseline; output/report and reconcile changes must retain truthful verification facts. #1601 is FIXED_OPEN for production docs, not a new migration task. | NOT_EXECUTED |
| 20 | IN_PROGRESS | Five-chain acceptance, independent R1/R2, final refresh/rebase and PR remain. | NOT_EXECUTED |
