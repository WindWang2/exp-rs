# REVIEW_R1 — R31-T01 (SELF-REVIEW; independent review NOT completed)

> **STATUS: this is a SELF-REVIEW by the implementing agent. It is NOT the
> independent R1 the track protocol requires.** The independent reviewer was
> blocked: every `Agent` launch failed with a provider quota error
> ("You've reached your monthly usage limit for this billing cycle"), so no
> reviewer with a clean context could be started. Retry at PR time; if still
> blocked, the PR body must state the review blocker explicitly.

Reviewed: `git diff 85ce917bea..ee6df01c84` (12 commits, 57 files) plus the
full file contents of every changed production file.

## Findings

| ID | Severity | Location | Finding | Disposition |
|---|---|---|---|---|
| S1 | P2 | `output_committer.cpp` reconcile (pre-fix) | The provenance-only repair went through the refresh registration, advancing the revision for a record whose content facts never moved — downstream convergence would chase a fabricated change. | **FIXED**: Ready + provenance-missing now attaches in place (no registration, no revision advance); test asserts the revision is unchanged. |
| S2 | P3 | `model_runtime.cpp` digest memo | The unsampled-middle residual (>192 KiB artifacts with a same-state rewrite confined to unsampled bytes) remains by design; the concurrent-interleaving oracle (fixed A/B barrier) is not deterministically reproducible in-process without a test seam. | **DISCLOSED**: the fix removes the mixed-entry class and the straddle class; the residual bound is documented in code. The concurrency oracle is NOT_EXECUTED (seam limitation), not claimed. |
| S3 | P3 | `tile_inference_engine.cpp` effectiveBatchSize | Heads that declare no output channels keep the historical 4× input bundle (disclosed in code); the plan's fallback keeps its 2× bundle, so the two estimates still disagree in that one case. | **DISCLOSED**: consistent with the pre-existing state; only the declared-class path is unified (which is what WP17 targets). |
| S4 | P3 | `chunked_run.cpp` manifest refresh | A refresh failure fails the run AFTER a successful commit (artifact + registration stand, no marker). The next run re-executes and re-commits; state converges, but the first run reports failure with a valid artifact on disk. | **ACCEPTED**: same shape as the marker-write failure contract; failing closed is preferred over leaving a lying manifest. |

## Verified correct (with the evidence that convinced me)

- **WP02 semantics**: `declaredFileOutputsForAlgorithm` returns `{"output"}` only for adapters with no descriptor or a throw (legacy convention); an explicit `outputs: null` synthesizes nothing (`atomic_algorithm_adapter.cpp` guard); `declaredOutputs` is written only when >1 file ports exist and every named identity is preserved from the result payload or the parameters; single-port tasks keep `outputLayerPath` (backward compatible). RED/GREEN: baseline commits value reports as assets (commitCalls 1 vs 0, fabricated assetId, autoLoad fired, 19 files written beside the input) → implementation: 4 cases / 99 assertions pass.
- **WP03**: `io:subdatasets` entries are an array port, `metadata`/`report` object ports, `verified` boolean — none can be mistaken for a file output; the real producer (`io:translate`) still commits, registers provenance and reopens with identical pixels.
- **WP04**: reconcile order (missing → unopenable → kind mismatch → state/provenance) is fail-closed at each step; the refresh path passes no execution fingerprint so the #726 same-execution escape cannot suppress a state repair; the dedup keeps the Asset ID stable; the manifest gate engages only when a sidecar exists (legacy artifacts keep the existence floor); the refresh keeps a carried manifest truthful across republish (loop-free).
- **WP05**: `absoluteFilePath()` is computed on the CALLING thread before the lambda captures it by value — a later CWD change cannot redirect the persist.
- **WP07**: the band-level `<NoDataValue>` still carries the fill for FillValue recipes (uncovered voids machine-readable); the source-band `<NODATA>` now comes only from the standing file's own declaration (a source's -9999 keeps its meaning); the warp sets targetNodata only; direct and warped VRTs share one contract; the valid-value-equals-fill aliasing is disclosed via `preflight.fill_value_alias` (emitted once per preflight — the early-return branches and the main path are mutually exclusive).
- **WP08**: reuse requires the sidecar, a full content re-hash, band count/dtype/NoData/mask flags and all six transform terms; a missing/corrupt/mismatched sidecar rebuilds through the same atomic publish; the sweep's live set includes the sidecar name.
- **WP11**: `rasterizeEach` streams to the sink (no QSet materialization); the budget check precedes insertion and duplicates never count; the cached `pixelIndices` path uses the same admission; the QSet-based `rasterize()` delegates to `rasterizeEach` with an inserting sink (identical union); BudgetExceeded is checked before Cancelled/RasterizeFailed.
- **WP15**: raw identity releases every device/load configuration; `|load:` releases exactly one; the cache key format (`framework|device|framework|digest[|load:...]`) makes the component search exact; live shared_ptrs keep working after release (verified by the test's post-release infers).
- **WP16**: the insert re-check replaces the entry wholesale and returns the caller's own digest; the before/after probe pair gate makes a straddling rewrite cost one uncached acquire.
- **WP17**: `declaredOutputChannels` matches `estimateExecution`'s accounting (classes × tensorNames + uncertainty); the undeclared-class fallback is byte-identical to the old formula (so `test_model_runtime.cpp`'s pinned batch values still hold unchanged).
- **Ownership**: no file owned by T02 (`python_environment.*`, `python_script_operator.*`, `python_environments.qrc`, `src/sdk/exprs/external_process.*`, `src/agent/runtime/**`) or T03 (app/GUI/cartography/mapspec/layout) appears in the diff; no #2061 active-exclude path is touched; `tests/CMakeLists.txt` changes are one append-only R31-T01 block.
- **Test quality**: the new tests assert numeric/identity/state facts (revision values, asset counts, digests, declared identities), not string presence; the two R30 test edits only exclude the metadata sidecar from a file-name glob and are documented.

## Not verified without running code

- The WP16 concurrent-interleaving oracle (see S2).
- ORT-dependent cancellation (`SICNU_WITH_ONNX_RUNTIME=OFF` in this build): `test_r30_t02_cancel_isolation`'s ORT case is tagged and skipped — ENV_LIMITED.
- The Windows path of every changed file (Linux-only host) — ENV_LIMITED.
