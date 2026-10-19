# R18-B Known Findings (verified, recorded, NOT fixed in this PR)

Audit-verified defects beyond this PR's P1/P2 work packages. Each carries the
mechanism from the line-level audit (subagent + lead re-verification where
marked). Recorded for follow-up tracks; none blocks the current fixes.

| ID | Sev | Where | Finding |
|----|-----|-------|---------|
| K1 | P2 | onnxruntime_provider.cpp:333-357 | No wall-clock budget on `m_session->Run`; a pathological graph blocks the shared session's `m_mutex` forever and the Timeout taxonomy is unreachable from this lane. Needs a watchdog design decision (ORT has no per-run timeout API). |
| K2 | P2 | model_runtime.cpp evictions (LRU sweep, evictExpired/evictDevice, releaseAll/release/setMaxCachedSessions) | Sessions destroyed while holding `m_mutex`; a python-worker kill (`waitForFinished(5000)`) stalls every acquire up to seconds; the corpse-recycle path already does unlock-destroy-relock — the other sites should follow. |
| K3 | P3 | detection_tile_engine.cpp:198-223 | Vector writer's ring mixes T_x(x+w,y+h) with T_y(x,y) — skewed footprints for rotated/sheeared geotransforms (exact for north-up). |
| K4 | P3 | model_ensemble.cpp:1418+ | Combine's NoData gate is `isnan` only; ±Inf member values poison the mean/variance and elect the Inf channel in argmax/vote. |
| K5 | P3 | model_ensemble.cpp:434+ | Member child context drops the progress callback; combine pass reports nothing; no lane ever reports 1.0 (Job layer masks it). |
| K6 | P3 | tile_inference_engine.cpp:1191/2465/3575, detection_tile_engine.cpp:355 | Unknown interpolation tokens silently run bilinear ("declared knob does nothing" class). |
| K7 | P3 | model_publish.cpp DetectionPublishGuard | Lacks ProductPublishGuard's Window-B adoption rung (crash between group publish and sidecar can destroy the last verified pair instead of adopting it back). |
| K8 | P3 | eo_preflight.cpp:143-148 / :204-213 / report plumbing | wavelengths without band_roles silently unchecked; CRS-family mismatch advisory-only; eo preflight report absent from raster/detection sidecars (promise gap in eo_preflight.h). |
| K9 | P3 | gpu_plane.cpp:263-268 / ~ModelSessionPool | `devices()` bypasses the mutex; pool destruction frees VRAM accounting for sessions with live external `shared_ptr` holders; dropped `shared_ptr` without `releaseSession` can never be evicted. Currently test-only surface. |
| K10 | P3 | detection_tile_engine.cpp (whole file) | The detection lane never runs the EO preflight — enforced calibration is fail-open for rs:detect. |
| K11 | P3 | resumable_tile_run.cpp:102+ | `wipeState` while the journal `ifstream` is still open fails silently on Windows → identity-drift wipe never happens; foreign header persists (POSIX unaffected). |
| K12 | P3 | model_ensemble.cpp:383-388 | Ensemble member acquisition ignores the cancellation predicate (single-model path honors it); a cancelled run still serially loads every member. |
| K13 | P3 | chunk_graph.cpp sink progress | Sink divides by the moving emitted count; a slow source + fast sink clamps progress to 1.0 after the first tile. |
| K14 | P3 | openvino/tensorrt providers | `mArtifact` never set (artifactPath placeholder); `infer` throws RSOperatorError (std::exception family) though the IModelRuntime contract documents std::runtime_error; TRT load() deserializes the engine before `cudaSetDevice(m_cudaIndex)`. Compile-only lanes — disclosed. |
| K15 | P3 | eo_preflight scene lane (fixed here as W5) — keep: single-input run() vs runMultiInput vs scene now consistent | (resolved) |
| K16 | P3 | nvml_inventory.cpp:114-118 | Failed middle-index NVML handles compact the device list; real device indexes ≥ devices.size() are dropped with skewed slots (honest-unknown degradation preferred). |
