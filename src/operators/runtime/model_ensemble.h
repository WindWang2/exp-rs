// src/operators/runtime/model_ensemble.h — manifest-defined model ensembles.
//
// A manifest that declares `ensemble` IS an ensemble model: it carries no
// weights of its own; its members (catalog model references) each execute
// the SAME input through the ordinary engines — every member keeps its own
// preprocessing, session, provider fallback chain and OOM ladder — and the
// member products are then combined into ONE product. No member product ever
// reaches the caller's output path: raster members stage into same-directory
// temporaries and the final product is published atomically, so a failure
// anywhere leaves NO output.
//
// Combination semantics (manifest `ensemble.combination`):
//   weighted_mean — out_c = Σ w·p_c / Σ w over equal-channel probability
//                   stacks (regression C==1 included) with an appended mean
//                   per-class weighted variance band.
//   weighted_vote — per pixel weighted argmax votes, ties → LOWEST class
//                   index; publishes a label raster (Byte ≤255 classes, else
//                   UInt16) + agreement band (round(share*254), sentinel
//                   excluded, documented in payload/sidecar).
//   wbf           — detection ensembles: Weighted Boxes Fusion over the
//                   members' decoded boxes (detection_fusion.h, ADR 0171).
// Scene-classification ensembles use weighted_mean / weighted_vote over the
// members' per-class probability vectors (identical vocabulary required).
//
// Uncertainty (manifest `ensemble.uncertainty`, default "auto"):
//   variance  — weighted per-class variance around the combined mean,
//               appended as one float32 band (weighted_mean only).
//   agreement — the winning class' accumulated vote share, appended as one
//               band quantized to the label raster's dtype (weighted_vote
//               only).
//   Under "wbf" and under request-level derived output modes no band exists.
//
// NaN semantics: any member NaN poisons the combined (pixel, channel) — one
// member's skipped tile is the ensemble's skipped tile.
// Grid + spatial-reference consistency (size, bands, projection, geotransform
// ≤1e-6) — divergence is a typed refusal, never a silent mix.
// Atomic publish (staged GTiff + rename + backup rollback) and a provenance
// sidecar (exp-rs-prov/1 + additive `ensemble` members block: identity/digest/
// framework/weight/backend/device/execution stats + provider fallback trail;
// detection adds the `fusion` block with algorithm and thresholds).
// RAII staged cleanup (member stacks AND their sidecars): no partial output,
// no residue on failure, cancel, and the success path.
// Bounded parallel member execution: members run under a manifest budget
// (`ensemble.max_concurrent_members`, default auto = min(members, 4)) with
// fail-fast, deterministic member-order result assembly and lowest-index
// failure surfacing.
#pragma once

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/tile_inference_engine.h"

namespace sicnu::operators::runtime {

/// Executes an ensemble model end-to-end: resolve + gate every member,
/// acquire one session per member (each through its own provider fallback
/// chain; the request's explicit device token overrides every member), run
/// every member under the bounded admission budget, combine, publish
/// atomically and write the provenance sidecar (exp-rs-prov/1 with the
/// `ensemble` member block; detection adds the `fusion` block).
///
/// Typed refusals (RSOperatorError): multi-feed/temporal members, TTA, nested
/// ensembles, detection requests on non-wbf manifests (and vice versa), class
/// vocabulary disagreement across members, request-level derived output modes
/// on weighted_vote, members resolving to non-ready models, members producing
/// unequal output channel counts under weighted_mean, and any contract the
/// member engines refuse.
/// Throws RSOperatorError on any failure; never leaves a partial output.
ModelExecutionResult runEnsembleInference( const ModelInfo &ensembleModel,
                                           const ModelExecutionRequest &request,
                                           RSOperatorContext &context );

} // namespace sicnu::operators::runtime
