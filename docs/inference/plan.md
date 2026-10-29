# Inference plan and quality summary (`sicnu.inference.plan/1` + `sicnu.inference.quality/1`)

R25-B feature 2, `src/operators/runtime/inference_plan.h`.

## `planModelInference(request)` — the read-only twin of `runModelInference`

Answers "what will this run do, and would it be refused?" **without
executing anything**: no session is acquired, no output file is written,
no pixels are read (metadata opens only). It walks the run seam's gate
sequence — input existence → model resolution/readiness → task
intent → runtime/device readiness → lane incompatibilities → contract
wiring (`rejectUnwiredContracts`) → feature cube → device token →
preprocess channel contract → output mode × head contract
(`ensureDerivedModeMatchesHeadContract`) → EO preflight
(`enforceEoPreflight`, enforced facts) → blend contract — converting each typed
refusal into an `error` finding instead.

It then computes the execution skeleton with **the engine's own static
planners** (`TileInferenceEngine::effectiveTileSize/effectiveHalo/
effectiveBatchSize`), so the plan and the engine cannot drift:

- `tile_size`, `halo`, `grid_x` × `grid_y`, `tiles_planned`
- `batch_size` (manifest budget capped by the request override, never
  upgraded), `batches_estimated`
- `batch_working_set_mib` — one batch of float32 tiles, input + output
  plane; labeled estimate

A `ready` verdict means "every gate the plan checks passes and the grid is
computable" — the checked gate list is exactly the one enumerated below
(a static read-only pass, not a run rehearsal; provider-side graph
validation remains the run's own step). A `refused` verdict carries the same message the run would
throw. Gate coverage after reviews R1+R2: input dtype contract (the engine's
own `inputDTypeMismatch` over every fed band), band-roles arity,
bands×temporal-length vs `CV_CN_MAX` (multi-input admitted on the largest
feed, exactly like the engine), detection `conf`/`nms_iou` bounds, and the
manifest-derived effective output mode with its conflict family
(uncertainty, multi-head, mask_threshold). Lane fidelity: the EO contract
is enforced per input / per feed path with the bound contract on the
raster, multi-input and scene lanes — the detection lane never enforces
EO, and neither does the plan. Ensembles skip the static runtime-readiness
gate (the run routes them to the ensemble engine first) and report the
combined skeleton with an info finding; per-member planning is not part of
v1. The batch working-set number is an estimate: raster-side float32
in/out per batch; provider staging and `to_input` resize change real peak
(see `batch_working_set_mib` notes).

The resolved-model overload `planModelInference(request, model)` skips
reference resolution for callers that already hold a `ModelInfo`.

## `summarizeInferenceQuality(result)` — one typed summary of a finished run

Projects a `ModelExecutionResult` into:

- **coverage** — `tiles_planned/processed/skipped_nodata`,
  `tile_coverage_ratio`, `grid_complete` (`processed + skipped == planned`,
  the r24-B tail-batch invariant: false means tiles were *lost*, not
  skipped), `batch_reductions` (OOM ladder)
- **product** — output bands/size; detection lanes get
  `raw_detections`, `detections_kept`, `suppression_ratio`
- **distribution** — labels/mask products: per-class pixel counts,
  `classified_pixel_ratio`, `dominant_class`
- **eo** — preflight presence + advisory count
- **notes** — typed observations (`quality.grid_incomplete`,
  `quality.detection_empty`, `quality.oom_ladder_fired`)

Refuses (typed `InvalidInputData`) results with neither raster nor
detection stats rather than fabricating a zero summary.

Multi-input fidelity: feeds bind to input contracts by name then
positionally (duplicate and unmatched names are typed refusals, exactly as
`runMultiInput`); per-feed dtype / band-roles / effective-preprocess arity
are validated per bound contract; the frame axis follows the engine
(dynamic-T takes the provided count, otherwise the declared
`temporal_length` with the `missing_timestep` policy); the batch budget
accounts `max(bands × declared frames)` over feeds.

Tests: `tests/test_r25_b_inference_plan.cpp` — the plan's grid is checked
**metamorphically** against a real engine run (exact tile counts, batch
size, tail-tile geometry), session-acquisition and output-file side effects
are asserted zero, all shared refusal gates are exercised, and the quality
summary covers the loss/detection/distribution projections.
