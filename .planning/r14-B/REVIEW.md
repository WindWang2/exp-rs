# R14-B Independent Review Record

## Round 1 (BLOCK on first submission)

Reviewer: general-purpose subagent with no authorship of the diff; wrote
its own risk predictions before reading the implementation conclusions.
Method: full diff read + surrounding-source context; independently
re-derived the strided-head halo-crop math; BUILT AND RAN all three new
test binaries (279 / 20695 / 15 assertions) plus test_detection_nms_10,
test_eo_platform_10, test_ensemble_detection, test_provider_http.

Predictions vs findings: 5 of the reviewer's 12 predictions confirmed
clean by independent derivation (halo math, kernel-parity guard vs catalog
validation, failMorphology lifetime, maxDetections non-regression, TRT
API usage); 2 hit (seam edge, NaN coverage); 1 partially hit (test
pinning); 2 NOT predicted but found: the python-worker timeout desync
(P1) and the square-auto runtime refusal (P2).

### Findings and dispositions

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| 1 | P1 | python worker timeout left the worker alive; no request correlation → next request consumes the stale answer (wrong outputs attributed to the next tile). The malformed/oversized branches stop the worker fail-closed for exactly this reason | **FIXED**: `stopWorker()` before throwing; recovery via the existing bounded restart budget |
| 2 | P2 | NaN *confidence* (not just geometry) still breaks the NMS sort's strict weak ordering / dedup's lround | **FIXED**: `!std::isfinite(b.confidence)` added to both predicates |
| 3 | P2 | square head + default `tensor_layout:"auto"` now fails at RUNTIME (mid-product) where master's heuristic silently decoded — should surface at manifest validation | **DELIBERATE + disclosed (R9)**: fail-closed beats silently transposed values; the registration-time fix lives in src/operators/framework/model_catalog.cpp (outside B ownership) and is called out in the PR for the framework owner |
| 4 | P3 | `.morph~` strandable on non-RSOperatorError exceptions (bad_alloc from row-band allocations bypasses the pass's own cleanup) | **FIXED**: run-level catch also removes `stagePath + ".morph~"` |
| 5 | P3 | half-open seam could orphan a center exactly on the raster's last core edge | **NOT FIXED (documented)**: unreachable via decode (boxes clamp to [0, rasterW] and w<=0 drops; only denormal-width float rounding could reach it — measure-zero). One-sided edge exception noted for the engine owner |
| 6 | P3 | stale contract comment in detection_postprocess.h re maxDetections | **FIXED**: comment updated to the raw-ceiling + post-dedup product-cap description |
| 7 | P3 | inferMulti success path skipped health accounting (lastForwardMs/forwards/clearError) | **FIXED**: success bookkeeping added, matching infer() |
| 8 | P3 | test used a 2-D blob for the CV_32F refusal (passed on master behind the pre-existing dims!=4 gate) | **FIXED**: 4-D CV_8U blob pins the dtype gate |
| 9 | P3 | HTTP watchdog detached thread referenced case locals | **FIXED**: heap-shared state (shared_ptr) |
| 10 | P3 | HTTP guard message implied routing that doesn't exist | **FIXED**: message now says "route the call onto a thread with an event loop" |

Round-1 verdict on the core hardening: "correct, well-tested and within
track scope" — halo-crop math, morphology re-tally, maxDetections
re-scoping (provably non-regressive), NaN gates, inferMulti detachment,
HTTP dispatcher refusal, TensorBlob contracts all verified correct by
independent derivation; scope check clean.

### Post-fix re-verification

* Rebuilt; GREEN x3 (279/15/20695 assertions).
* 21 regression binaries rerun green (test_model_runtime 100394
  assertions incl. the #1455 corpse-session ledger case; stress; OOM
  ladder; publish fences; tile semantic equivalence; chunk 11s).
* `git diff --check` clean.

## Round 2 (PASS — 0 P0, 0 P1)

Reviewer: second independent subagent (no authorship). Method: re-derived
each round-1 disposition from the committed state, then a fresh pass over
the full diff plus a first sweep of src/runtime/** (scratch/chunk/exec/
worker/gpu — no concrete defects found; streamFnv1a, bounded_chunk_queue,
worker_lease, execution_governor and the gpu_plane pool invariants all
verified clean).

### Round-1 disposition verification

All ten dispositions verified resolved except round-1 item 8 (the dtype
test), which round 1 had mis-verified: the 4-D CV_8U blob does not pin
the gate on master (master's setInput throws cv::Exception, wrapped as
"forward pass failed: ... incompatible type" — which still satisfies
REQUIRE_THROWS_AS(std::runtime_error)).

### New findings from round 2 and dispositions

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| NEW-1 | P2 | python timeout stopped the worker but did NOT mark the session permanently dead (the oversized/malformed branches do): the next request fails as "not loaded" forever and the registry only recycles on permanentlyUnavailable(); the code comment claimed a recovery that did not exist | **FIXED**: m_permanentlyDead.store(true) beside stopWorker(); comment rewritten to the actual semantics |
| NEW-2 | P3 | dtype-gate test unpinned (see above) | **FIXED**: asserts the gate's own "must be float32" message; verified RED on master's message path |
| NEW-3 | P3 | inferMulti detachment case unpinned on this fixture (OpenCV 5 forward returns fresh Mats for the identity graph) | **FIXED (retitled)**: case renamed to "results survive a second forward" with a comment documenting that it pins the CONTRACT (the race needs a multi-layer net whose forward returns internal headers) |
| NEW-4 | P3 | inferMulti acquired m_inferMutex without re-checking m_cancelRequested afterwards (infer() does) | **FIXED**: post-lock cancel re-check |
| NEW-5 | P3 | model_catalog.h maxDetections comment ("bounded accumulation guard across tiles") drifted from the new semantics | **NOT FIXED here** — src/operators/framework/model_catalog.h is outside B ownership; disclosed in the PR for the framework owner |

Round-2 verdict: PASS. Fixes verified in code (not taken on trust); no new
bug introduced by the round-1 fixes; post-fix rerun: test_r14_B_*
(279/15/20695) + test_provider_python (115 assertions) green.
Final P0/P1 count for this track: **0**.
