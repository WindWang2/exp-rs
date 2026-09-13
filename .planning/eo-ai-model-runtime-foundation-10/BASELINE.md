# BASELINE — eo-ai-model-runtime-foundation-10

- **origin/master SHA at track start:** `7d78059d1a` ("Merge pull request #958 from WindWang2/zcode/prompt-command-hygiene-review")
- **Worktree:** `../exp-rs-eo-ai-model-runtime-foundation-10`
- **Branch:** `zcode/eo-ai-model-runtime-foundation-10` (off `origin/master` @ `7d78059d1a`)
- **Verified:** `git fetch --all --prune` → clean; `git pull --ff-only` → "已经是最新的"; branch created from `origin/master` at the SHA above.

## Concurrent 10.0 tracks (open PRs at start)

| PR | Branch | Ownership | Shared-file overlap with this track |
|---|---|---|---|
| #973 | zcode/temporal-eo-phenology-change-10 | temporal calendars/phenology/change intelligence | `.gitignore`, `CHANGELOG.md`, `src/operators/CMakeLists.txt`, `src/operators/rs/rs_operators_init.cpp` (append-only seams) |
| #974 | zcode/cloud-data-fabric-datacube-10 | data fabric / cube / chunk plans | `.gitignore`, `CHANGELOG.md`, `src/geospatial/convert/raster_convert.*` |
| #975 | zcode/scientific-contract-verification-10 | scientific contracts / verification | `.gitignore`, `CHANGELOG.md`, `docs/verification/*`, `pi/*` |
| #972 | zcode/r2-deep-review | review dossier (docs only) | none (`.planning`, `review`, `.gitignore`) |

Overlap policy: this track's `.gitignore` / `CHANGELOG.md` edits are append-only blocks
with a unique track slug; `src/operators/CMakeLists.txt` edits are append-only target
blocks; no edits to `src/operators/rs/rs_operators_init.cpp` registration lines other
than appended registrations (narrow integration commit if needed).

## 9.0 legacy already in master (must NOT be redone)

From `.planning/model-runtime-multimodal-9/FINAL_REPORT.md` + code verification
(`src/operators/runtime/`): NVML device inventory (`nvml_inventory.*`), real CUDA EP
execution (`onnxruntime_provider.*`), providerDetails, python worker CUDA pin,
timeout taxonomy (`classifyInferenceError`), per-feed preprocessing
(`ModelInputContract::preprocess`, model_catalog.h:93-103), feed fingerprints,
feather blending (`ModelTilingContract::blend`), class pixel counts, package
aux-file digest (`ModelAuxFileContract`), consumer-side product provenance
verification (`provenance_verify.*`).

## Known in-scope defects from the merged whole-repo line review (review/findings/operators.md)

- **F-OPS-5 (P2)**: whole-raster NMS/dedup O(n²), outside cancellation checkpoints —
  `detection_postprocess.cpp:150-187`, call site `detection_tile_engine.cpp:424`. → FIX in this track.
- **F-OPS-1 (P2)**: `postprocess.class_mapping` product-class values not bounded by the
  output raster encoding (Byte clamp → 255 = NoData sentinel silently corrupts labels) —
  `tile_inference_engine.cpp:852-874,1595-1613`, `model_catalog.cpp:829-845`. → FIX in this track.
- **F-OPS-2 (P3)**: `TensorBlob::fromMat` ND non-continuous fallback copies zero rows
  (`tensor_blob.cpp:163-173`). → FIX in this track.
- F-OPS-3 (qa_mask fail-open) / F-OPS-4 (io:reproject dead param): NOT this track's
  ownership (preprocess/io operators); left to their owning tracks.

## Test/build baseline facts

- Test fixtures: deterministic fake `IModelRuntime` providers registered into
  `ModelRuntimeRegistry` (see `tests/test_model_tasks.cpp:56-120`); no real ONNX weights
  in repo; manifests are templates with empty `artifact.path`.
- Existing model-runtime suites: test_model_runtime.cpp, _v2, _8, _9, test_model_tasks,
  test_multimodal_inference, test_model_failure_matrix, test_model_manifest7,
  test_model_library_manifests, test_model_runtime_bench, test_onnxruntime_provider.
- Build: `cmake --preset dev-default` (Debug, ENABLE_TESTS=ON) → `build-dev/` in the
  worktree; targeted `cmake --build build-dev --target <test targets> -j2`; sibling
  10.0 track evidence: pybind11 FetchContent needs offline source override
  `-DFETCHCONTENT_SOURCE_DIR_PYBIND11=<main>/build-dev/_deps/pybind11-src`; one ICE at
  host load ~16 → rerun `-j1`.
- Host: 16 cores / 62 GB; policy `-j2`, drop `-j1` when RSS > 70% or load > 1.5× cores.
