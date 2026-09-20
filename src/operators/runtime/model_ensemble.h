// src/operators/runtime/model_ensemble.h — manifest-defined model ensembles.
//
// A manifest that declares `ensemble` IS an ensemble model: it carries no
// weights of its own; its members (catalog model references) each execute
// the SAME input through the ordinary TileInferenceEngine — every member
// keeps its own preprocessing, session, provider fallback chain and OOM
// ladder — and the member probability stacks are then combined into ONE
// product by a streaming pass. No member product ever reaches the caller's
// output path: members stage into same-directory temporaries and the final
// product is published atomically, so a failure anywhere leaves NO output.
//
// Combination semantics (manifest `ensemble.combination`):
//   weighted_mean — out_c = Σ w·p_c / Σ w over equal-channel probability
//                   stacks (regression C==1 included).
//   weighted_vote — per pixel, each member votes its argmax class with its
//                   weight; the winner is the class with the highest
//                   accumulated vote, ties resolve to the LOWEST class index.
//                   Publishes a label raster, not probabilities.
// Uncertainty (manifest `ensemble.uncertainty`, default "auto"):
//   variance  — weighted per-class variance around the combined mean,
//               appended as one float32 band (weighted_mean only).
//   agreement — the winning class' accumulated vote share, appended as one
//               band quantized to the label raster's dtype (weighted_vote
//               only).
#pragma once

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/tile_inference_engine.h"

namespace sicnu::operators::runtime {

/// Executes an ensemble model end-to-end: resolve + gate every member,
/// acquire one session per member (each through its own provider fallback
/// chain; the request's explicit device token overrides every member),
/// run every member to a staged probability stack, combine, publish
/// atomically and write the provenance sidecar (exp-rs-prov/1 with the
/// `ensemble` member block).
///
/// Typed refusals (RSOperatorError): detection/scene-classification/multi-feed
/// requests, derived output modes (labels/mask/confidence) requested at the
/// request level, members resolving to non-ready models, members producing
/// unequal output channel counts under weighted_mean, and any contract the
/// member engines refuse.
/// Throws RSOperatorError on any failure; never leaves a partial output.
ModelExecutionResult runEnsembleInference( const ModelInfo &ensembleModel,
                                           const ModelExecutionRequest &request,
                                           RSOperatorContext &context );

} // namespace sicnu::operators::runtime
