# FINAL_REPORT — Model Runtime & Multimodal EO Inference 9.0

Branch `feat/model-runtime-multimodal-9` (worktree exp-rs-model-runtime-9),
based on origin/master @ `132da5e998`, merged with origin/master
`f316dfdbb4` (#848–#882 fixes; one predicted conflict in rs_inference_operator
resolved — both #872 fixes kept ours with the richer description).
**Online CI/CD was NOT waited on; completion is local reproducible evidence.**

## Delivered (per milestone)

- **M0 contract truth**: rs:infer schema declares `device` (#872, still-valid
  on origin/master — fixed here independently of the parallel WIP) and
  `blend`; mechanical schema⊇parsed-params regression for all model
  operators; Timeout kind appended to the taxonomy.
- **M1 provider matrix**: CUDA EP executed FOR REAL (first in the platform's
  history) on the RTX 3080 Laptop via ORT 1.30 GPU + cuDNN (user-local pip
  stack); typed DeviceUnavailable refusals everywhere else; the CUDA-less
  refusal test now honestly SKIPS with a capability WARN on GPU builds
  (its Ort::Env-first probe is a real ORT ≥1.30 finding); worker device
  pinning via CUDA_VISIBLE_DEVICES; timeout vs crash taxonomy.
- **M2 planner**: NVML-backed real device inventory (names, total+free VRAM
  per card, refreshed per acquire); dual CUDA gates (cv::dnn backend vs real
  driver keyed on provider traits); admission free-VRAM = min(ledger,
  driver-reported); capacity = min(total, budget).
- **M3 feed graph**: inputs[].preprocess per-feed overrides (closed
  vocabulary, per-feed arity, effective contract recorded); feed
  fingerprints (structure + bounded content digest); per-input geometry
  knobs refused (grid-global by design).
- **M4 temporal**: chunking DEFERRED with rationale (rank-4 raster output
  contract leaves nothing to reassemble); dynamic-T NCTHW truth, per-frame
  provenance and rank-5-output refusal regression-tested.
- **M5 tile engine**: feather blending with corrected cosine ramp, bounded
  sliding-row accumulator, NoData-wins invalidity plane, probabilities
  blend before the derived collapse, per-(head,tile) grid-preserving
  refusal; embedding product semantics verified as existing.
- **M6 products**: per-product-class pixel tallies in payload + sidecar.
- **M7 packaging**: package.aux_files digest-verified at resolve; package
  digest extends session keys; duplicate/version conflicts per catalog
  contract.
- **M8 provenance**: consumer-side verifyProductProvenance with 8 typed
  verdicts (never throws); sidecar/payload execution identity (EP +
  runtime version, package digest).
- **M9 performance**: honest CPU + CUDA numbers with environment
  (benchmarks/model-runtime-9-cuda.json NEW; CPU baseline re-measured
  uncontaminated).

## Verification

14 suites, 114,661 assertions, all green (post-merge, frozen sources;
TEST_MATRIX.md). help_coverage 8,159 green. test_capability_drift: 2
failures pre-existing on master (harness track). Capability-gated items
are MARKED: CUDA-refusal test skips on GPU builds; CUDA bench FAILs
loudly on CPU-only ORT (never a fabricated number).

## Adversarial review

Reviewer A + B (read-only) + self-review: 1 P0 (inverted feather ramp —
the midpoint test couldn't discriminate; fixed + doc/test corrected),
3 P1 (mask threshold sentinel; verifier never-throws; NoData-wins
invalidity plane), 12 P2 + 8 P3 — all fixed or dispositioned with
reasons in REVIEW_LOG.md.

## Known limitations (honest)

- CUDA EP per-forward benchmark is launch/copy-bound for tiny tensors
  (correctness anchor, not a throughput claim); CUDA_VISIBLE_DEVICES
  physical/visible reconciliation on multi-GPU cluster hosts is a
  follow-up; NVML probe not cached (per-acquire cost accepted).
- Feather blending: single-input only; multi-input refuses typed;
  accumulator memory scales with W×slots (bound documented).
- Fingerprints cover the first frame of temporal feeds (documented).
- MLOps recorder wiring of verifyProductProvenance is the MLOps track's
  follow-up (the function is the seam).
- The GDAL 3.13 / run_bridge cross-track fixes are guarded only by the
  host toolchain (version-shim follow-ups flagged for their tracks).
