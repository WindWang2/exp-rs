# EO AI Model Runtime / Foundation Model Platform 10.0

Baseline: origin/master @ `7d78059d1a` · branch `zcode/eo-ai-model-runtime-foundation-10`
(worktree `../exp-rs-eo-ai-model-runtime-foundation-10`). **Local evidence only;
no online CI dependency** (policy `ci=none`). PR not merged by the track.

## Baseline & dedupe

- 9.0 legacy audited and NOT redone (NVML device truth, real CUDA EP, per-feed
  preprocess, fingerprints, feather blending, aux-file digests, consumer-side
  provenance verification) — `.planning/<slug>/BASELINE.md`.
- Concurrent 10.0 tracks audited (#972-#975): no ownership overlap; shared-file
  policy (append-only `.gitignore` / `CHANGELOG` / CMake / registration) recorded
  in OWNERSHIP.md.
- Whole-repo line-review findings F-OPS-5/1/2 fixed (in-ownership); F-OPS-3/4
  left to their owning families.

## Architecture & major deliverables

- **EO manifest truth (manifest_version 6)**: additive `eo` section —
  `wavelengths_nm` per-band-role windows, `calibration.state|enforced`
  (SICNU_RADIOMETRIC_STATE vocabulary, fail-closed preflight via the canonical
  metadata layer BEFORE inference), `grid.crs_family`. Checks/advisories are
  recorded in run stats and provenance sidecars; absent section = bit-identical
  legacy behavior.
- **Canonical EO task vocabulary** (`canonicalEoTask`, aliases accepted, legacy
  free-form strings tolerated) + **task-intent gates** on the single execution
  seam; adapters **rs:classify** (one bounded scene window, typed
  `exp-rs-classification/1` JSON artifact), **rs:change** (two co-registered
  feeds), **rs:regress** (continuous raster).
- **Manifest-driven pre/post**: `preprocess.offset` (after scale; refused with
  normalize none), `postprocess.calibration_temperature` (probability-space
  temperature scaling pre-collapse; argmax invariant), `postprocess.morphology`
  (sentinel-aware streaming cleanup on published labels/mask, bounded
  O(W·rows) memory, seam-exact halos, executed on the stage pre-publication).
- **Optional provider matrix**: TensorRT (prebuilt engines) and OpenVINO
  (IR/ONNX) adapters behind `SICNU_ENABLE_TENSORRT` / `SICNU_ENABLE_OPENVINO`;
  absent SDKs degrade to typed `UnsupportedRuntime` (default build unchanged).
  `registerProvider` stays the plugin seam.
- **MLOps seam**: `verifyProductAgainstModel(outputPath, modelReference)`.
- **Defect fixes**: F-OPS-5 (grid-bounded cancellable NMS, kept set proven
  element-wise identical to dense), F-OPS-1 (Labels encoding from the PRODUCT
  class domain; class_mapping < 65535), F-OPS-2 (TensorBlob ND ROI copy).

## Compatibility

Old manifests parse and run bit-identically; every new field is optional and
additive; the round-trip projection (`toJson`) is accepted on re-registration
(`task_canonical` joined the derived-projection whitelist).

## Tests & performance (local)

- New: `test_detection_nms_10` (12 assertions / 4 cases),
  `test_eo_platform_10` (570 / 21).
- Regressions green: test_model_runtime (100394), _8 (1770), _9 (190),
  test_model_tasks (1249), test_multimodal_inference (8324),
  test_model_failure_matrix (126), test_model_catalog_v2 (383),
  test_model_manifest7 (62), test_model_library_manifests (237),
  test_algorithm_meta_drift (2631), test_capability_knowledge (1052),
  test_toolbox_coverage (108), test_spatial_tools (139),
  test_agent_tool_catalog (4169), test_harness_catalog (109).
- `test_capability_drift` 19/21 — the 2 failures are byte-identical to master
  @ `7d78059d1a` (harness-track, pre-existing; OUT_OF_SCOPE).
- NMS boundedness evidence: 100k disjoint-box dedup 304 ms (dense shape ≈ 5e9
  IoU compares); 50k dense-cluster NMS 105 ms. 1e5-extent scale + mid-run
  typed cancellation asserted in-test. See `.planning/<slug>/PERFORMANCE.md`.

## Review findings

Two independent read-only reviews (architecture/EO-correctness; concurrency/
lifecycle/test-credibility/security). **1 P0 + 6 P1 fixed before push**:
NMS empty-input crash (untested normal path — now typed + tested); morphology
open/close seam exactness (both ops over the full halo window); rs:change
positional binding (adapter no longer names feeds; test drives the shipped
operator); wavelength preflight now verifies the FED bands (effective band
selection + bound feed contract); rs:classify refuses clamp/pad (no silent
no-op); calibration refused on the feather path (no false provenance);
drift-guard assertion restored from a comment merge. High-value P2s fixed:
calibration refusal on probability/logit heads, morphology metadata/creation
options/counts/kernel-cap/cancellation, TRT/OV memory-safety + version
guards, scene sample bound, artifact backup on publish, test credibility
(three vacuous/order-dependent tests now pin real behavior). Full
finding-by-finding disposition: `.planning/<slug>/REVIEW_LOG.md`.
Final local state: 13 suites green (117k+ assertions); the only non-green
tests in scope are the 2 `test_capability_drift` failures byte-identical to
master (pre-existing, harness track).

## Known limitations

- TensorRT/OpenVINO execution lanes are compiled only on deployment hosts; on
  this host they are typed refusals (capability-gated tests SKIP honestly).
- Tiled-inference RESUME seam descoped (DECISIONS D8): reusing a partial stage
  requires an attach-existing contract on GdalStreamingOutput (#647 hygiene,
  geospatial-io ownership). 1e5-extent scale + cancellation evidence shipped.
- super_resolution / vision-language task tokens are reserved in the vocabulary
  without adapters (no in-repo model class); rs:change v1 publishes the
  probability stack (derived-mask collapse remains a single-input convenience).
- The 2 pre-existing `test_capability_drift` failures (harness track).

## Follow-ups

- GdalStreamingOutput attach-existing contract -> true tiled resume (D8).
- Wire verifyProductAgainstModel into the experiment replay recorder (mlops).
- SAM-like geo-prompt adapter when an in-repo promptable graph lands.
