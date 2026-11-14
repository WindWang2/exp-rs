#pragma once

/*******************************************************************************
 * R30-T02 (WP01): the ONE resolver for manifest+request execution contracts.
 *
 * Catalog parse, the inference PLAN and the tile ENGINES must never disagree
 * about what a (model, request) pair actually executes: output mode, blend,
 * postprocess (morphology / calibration), preprocess lane support and the
 * multi-input restrictions. Before this seam the plan carried its own copies
 * of the engine gates and they drifted in both directions (the morphology
 * gate was inverted in the plan, the feather+pad refusal was engine-only,
 * the single-input pad refusal never reached the plan at all).
 *
 * The resolver is a PURE function of (ModelInfo, lane, request knobs). The
 * plan maps violations onto findings; the engines throw the first violation
 * as a typed RSOperatorError. Message text is the engine's historical text
 * verbatim so existing consumers and tests keep matching.
 */

#include <string>
#include <vector>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/tile_inference_engine.h"

namespace sicnu::operators::runtime
{

/// Which engine lane executes this request (the service routes before the
/// engines run; the resolver needs the same routing fact).
enum class InferenceLane
{
  SingleInputRaster,
  MultiInputRaster,
  Detection,
  SceneClassification,
};

/// One contract violation: a stable finding code (the plan's vocabulary), the
/// user-facing message and an optional remediation hint.
struct ContractViolation
{
  std::string code;
  sicnu::operators::ErrorCode error = ErrorCode::InvalidInputData;
  std::string message;
  std::string remediation;
};

/// The resolved, executed-as-declared contract for one request.
struct ResolvedExecutionContract
{
  RasterOutputMode outputMode = RasterOutputMode::Probability;
  TileBlend blend = TileBlend::None;
  /// True only when the postprocess ACTUALLY executes on this lane — success
  /// products and provenance may claim exactly these.
  bool morphologyExecutes = false;
  bool calibrationExecutes = false;
  std::vector<ContractViolation> violations;
};

/// Resolve the execution contract. Never reads the filesystem, never throws
/// for contract reasons (a manifest output.format outside the vocabulary is
/// reported as a violation instead of thrown).
ResolvedExecutionContract resolveExecutionContract( const ModelInfo &model, InferenceLane lane,
                                                    RasterOutputMode requestMode,
                                                    TileBlend requestBlend );

/// Engine helper: throw the FIRST violation as its typed RSOperatorError, or
/// return the resolved contract. The engines call exactly this so plan and
/// run can never diverge again.
ResolvedExecutionContract enforceExecutionContract( const ModelInfo &model, InferenceLane lane,
                                                    RasterOutputMode requestMode,
                                                    TileBlend requestBlend );

} // namespace sicnu::operators::runtime
