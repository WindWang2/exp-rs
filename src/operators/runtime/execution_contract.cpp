#include "operators/runtime/execution_contract.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sicnu::operators::runtime
{

namespace
{

ContractViolation makeViolation( const char *code, ErrorCode error, const std::string &message,
                                 std::string remediation = {} )
{
  ContractViolation v;
  v.code = code;
  v.error = error;
  v.message = message;
  v.remediation = std::move( remediation );
  return v;
}

/// The engine's own resolution (request override wins; the manifest carries
/// the default) — non-throwing variant of TileInferenceEngine::rasterOutputMode.
RasterOutputMode resolveOutputMode( const ModelInfo &model, RasterOutputMode requestMode,
                                    bool *unsupported )
{
  *unsupported = false;
  if ( requestMode != RasterOutputMode::Probability )
    return requestMode;
  const std::string &format = model.output.format;
  if ( format.empty() || format == "probability" )
    return RasterOutputMode::Probability;
  if ( format == "labels" )
    return RasterOutputMode::Labels;
  if ( format == "mask" )
    return RasterOutputMode::Mask;
  if ( format == "confidence" )
    return RasterOutputMode::Confidence;
  *unsupported = true;
  return RasterOutputMode::Probability;
}

TileBlend resolveBlend( const ModelInfo &model, TileBlend requestBlend )
{
  if ( requestBlend != TileBlend::Unset )
    return requestBlend;
  return model.tiling.blend == "feather" ? TileBlend::Feather : TileBlend::None;
}

/// The derived-mode head-contract rules live in ONE place — the engine's own
/// ensureDerivedModeMatchesHeadContract (tile_inference_engine.cpp). The
/// resolver calls it and converts its typed throw into a violation so plan
/// and run read the same implementation.
void derivedHeadContractViolations( const ModelInfo &model, RasterOutputMode mode,
                                    std::vector<ContractViolation> &out )
{
  try
  {
    ensureDerivedModeMatchesHeadContract( model, mode );
  }
  catch ( const RSOperatorError &error )
  {
    out.push_back(
      makeViolation( "plan.head_contract_violation", error.code(), error.message() ) );
  }
}

bool anyFeedPads( const ModelInfo &model )
{
  if ( model.preprocess.pad > 0 )
    return true;
  for ( const ModelInputContract &input : model.inputs )
    if ( input.preprocessDeclared && input.preprocess.pad > 0 )
      return true;
  return false;
}

} // namespace

ResolvedExecutionContract resolveExecutionContract( const ModelInfo &model, InferenceLane lane,
                                                    RasterOutputMode requestMode,
                                                    TileBlend requestBlend )
{
  ResolvedExecutionContract resolved;
  bool unsupportedFormat = false;
  resolved.outputMode = resolveOutputMode( model, requestMode, &unsupportedFormat );
  resolved.blend = resolveBlend( model, requestBlend );
  std::vector<ContractViolation> &violations = resolved.violations;
  const bool multi = lane == InferenceLane::MultiInputRaster;
  const bool singleRaster = lane == InferenceLane::SingleInputRaster;
  const RasterOutputMode mode = resolved.outputMode;
  const bool derived = mode != RasterOutputMode::Probability;

  if ( unsupportedFormat )
  {
    violations.push_back( makeViolation(
      "plan.output_mode_unsupported", ErrorCode::InvalidInputData,
      "unsupported output.format '" + model.output.format
        + "' (supported: probability, labels, mask, confidence)",
      "fix the manifest output.format vocabulary (see the model catalog docs)" ) );
    // Every later rule would reason about an unresolved mode; stop here.
    resolved.blend = resolveBlend( model, requestBlend );
    return resolved;
  }

  // --- preprocess lane support -------------------------------------------
  // inputs[0].preprocess overrides and the global pad/clamp extensions are
  // executed by the MULTI-INPUT engine only (run 1113/1122, scene 2669,
  // detection contract: same refusal, lane-specific text).
  if ( !multi && !model.inputs.empty() && model.inputs[0].preprocessDeclared )
    violations.push_back( makeViolation(
      "plan.preprocess_lane_conflict", ErrorCode::InvalidParameter,
      "inputs[0].preprocess override is only executed by the multi-input engine "
      "(named_inputs); a single-input model must declare its preprocessing in the global "
      "preprocess section" ) );
  if ( !multi && ( model.preprocess.pad > 0 || !std::isnan( model.preprocess.clampMin )
                   || !std::isnan( model.preprocess.clampMax ) ) )
  {
    if ( lane == InferenceLane::Detection )
      violations.push_back( makeViolation(
        "plan.preprocess_lane_conflict", ErrorCode::InvalidParameter,
        "preprocess.pad / clamp_min / clamp_max are executed by the multi-input engine; "
        "this detection model must drop them from the manifest" ) );
    else if ( lane == InferenceLane::SceneClassification )
      violations.push_back( makeViolation(
        "plan.preprocess_lane_conflict", ErrorCode::InvalidInputData,
        "preprocess.pad / clamp_min / clamp_max are executed by the "
          "multi-input engine; this scene-classification model must drop "
          "them from the manifest" ) );
    else
      violations.push_back( makeViolation(
        "plan.preprocess_lane_conflict", ErrorCode::InvalidInputData,
        "preprocess.pad / clamp_min / clamp_max are executed by the multi-input engine "
        "(runMultiInput); this single-input model must drop them from the manifest to run "
        "here" ) );
  }

  // --- derived-mode conflicts (single-input raster lane) -------------------
  if ( singleRaster && derived )
  {
    const std::string uncertainty = TileInferenceEngine::uncertaintyMethod( model );
    if ( !uncertainty.empty() )
      violations.push_back( makeViolation(
        "plan.output_mode_conflict", ErrorCode::InvalidInputData,
        "output.uncertainty requires the full probability stack — remove output.format or "
        "output.uncertainty" ) );
    if ( model.output.tensorNames.size() > 1 )
      violations.push_back( makeViolation(
        "plan.output_mode_conflict", ErrorCode::InvalidInputData,
        "derived output.format applies to single-head models; this manifest declares "
          + std::to_string( model.output.tensorNames.size() ) + " tensor heads" ) );
    derivedHeadContractViolations( model, mode, violations );
    if ( mode == RasterOutputMode::Labels && model.postprocess.maskThreshold >= 0.0 )
      violations.push_back( makeViolation(
        "plan.output_mode_conflict", ErrorCode::InvalidInputData,
        "postprocess.mask_threshold is meaningless with output.format=labels (argmax never "
        "thresholds) — remove one of the two" ) );
    if ( mode == RasterOutputMode::Labels )
    {
      int domainSize = static_cast<int>( model.output.classes.size() );
      if ( !model.postprocess.classMapping.empty() )
        domainSize = 1 + *std::max_element( model.postprocess.classMapping.begin(),
                                            model.postprocess.classMapping.end() );
      if ( domainSize > 65535 )
        violations.push_back( makeViolation(
          "plan.labels_domain_overflow", ErrorCode::InvalidInputData,
          "labels output declares a class domain of " + std::to_string( domainSize )
            + " product classes; the maximum encodable domain is 65535 (NoData sentinel "
              "excluded)" ) );
    }
  }

  // --- multi-input lane restrictions --------------------------------------
  if ( multi )
  {
    if ( derived )
      violations.push_back( makeViolation(
        "plan.multi_input_contract", ErrorCode::InvalidInputData,
        "multi-input models write the probability stack only — derived output.format "
        "(labels/mask/confidence) is a single-head single-input convenience; derive from "
        "the stack afterwards or run the single-input engine" ) );
    if ( model.preprocess.resize == "to_input" )
      violations.push_back( makeViolation(
        "plan.multi_input_contract", ErrorCode::InvalidInputData,
        "preprocess.resize=to_input is not supported for multi-input models — "
        "declare aligned grids per input (alignment=reference) instead" ) );
  }

  // --- blend contract ------------------------------------------------------
  if ( ( singleRaster || multi ) && resolved.blend == TileBlend::Feather )
  {
    const int halo = std::max( 0, TileInferenceEngine::effectiveHalo( model ) );
    if ( halo <= 0 )
      violations.push_back( makeViolation(
        "plan.blend_without_halo", ErrorCode::InvalidParameter,
        "tiling.blend=feather requires a halo (tiling.halo or tiling.overlap) — overlapping "
        "windows are what gets averaged; refusing instead of blending nothing" ) );
    else if ( multi && anyFeedPads( model ) )
      violations.push_back( makeViolation(
        "plan.blend_pad_conflict", ErrorCode::InvalidParameter,
        "tiling.blend=feather is not supported with preprocess.pad on the multi-input "
        "lane — the pad margin is a zero-filled frame with no prediction geometry to "
        "blend; drop the pad or drop the blend contract" ) );
  }

  // --- postprocess: executes exactly where the engines execute it ---------
  // Morphology runs on the STAGED single-input Labels/Mask product only
  // (engine applyMorphologyPass). A declared morphology anywhere else would
  // be a declared knob that never executes — refuse it instead of publishing
  // a product whose provenance claims work that did not happen.
  resolved.morphologyExecutes = singleRaster && ( mode == RasterOutputMode::Labels
                                                  || mode == RasterOutputMode::Mask );
  if ( !model.postprocess.morphology.empty() && !resolved.morphologyExecutes )
  {
    if ( singleRaster )
      violations.push_back( makeViolation(
        "plan.postprocess_contract_conflict", ErrorCode::InvalidInputData,
        "postprocess.morphology executes on labels/mask products — declare an "
        "output.format or drop the morphology contract" ) );
    else
      violations.push_back( makeViolation(
        "plan.postprocess_contract_conflict", ErrorCode::InvalidInputData,
        "postprocess.morphology executes on labels/mask products of the single-input "
        "engine — it can never execute on the "
          + std::string( lane == InferenceLane::MultiInputRaster
                          ? "multi-input (probability-stack)"
                          : lane == InferenceLane::Detection ? "detection" : "scene" )
          + " lane; drop the morphology contract" ) );
  }
  // Calibration temperature-scales the STITCHED derived collapse (engine
  // flushBatch). Feather blending refuses it (uncalibrated blend would lie);
  // the probability stack and every other lane never apply it.
  resolved.calibrationExecutes = !std::isnan( model.postprocess.calibrationTemperature )
                                 && singleRaster && derived
                                 && resolved.blend != TileBlend::Feather;
  if ( !std::isnan( model.postprocess.calibrationTemperature ) )
  {
    if ( singleRaster && !derived )
      violations.push_back( makeViolation(
        "plan.postprocess_contract_conflict", ErrorCode::InvalidInputData,
        "postprocess.calibration_temperature executes on labels/mask/confidence products — "
        "declare an output.format or drop the calibration" ) );
    else if ( singleRaster && resolved.blend == TileBlend::Feather )
      violations.push_back( makeViolation(
        "plan.postprocess_contract_conflict", ErrorCode::InvalidInputData,
        "postprocess.calibration_temperature is not executed on the feather-blended derived "
        "path — drop the blend contract or the calibration (an uncalibrated product must "
        "not claim one)" ) );
    else if ( !singleRaster )
      violations.push_back( makeViolation(
        "plan.postprocess_contract_conflict", ErrorCode::InvalidInputData,
        "postprocess.calibration_temperature is not executed on the "
          + std::string( lane == InferenceLane::MultiInputRaster ? "multi-input"
                          : lane == InferenceLane::Detection ? "detection" : "scene" )
          + " lane — drop the calibration contract" ) );
  }

  return resolved;
}

ResolvedExecutionContract enforceExecutionContract( const ModelInfo &model, InferenceLane lane,
                                                    RasterOutputMode requestMode,
                                                    TileBlend requestBlend )
{
  ResolvedExecutionContract resolved =
    resolveExecutionContract( model, lane, requestMode, requestBlend );
  if ( !resolved.violations.empty() )
  {
    // The thrown text is the engine's historical message verbatim (the plan
    // carries the remediation hint as its own finding field).
    const ContractViolation &first = resolved.violations.front();
    throw RSOperatorError( first.error, first.message );
  }
  return resolved;
}

} // namespace sicnu::operators::runtime
