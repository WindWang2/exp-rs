// src/operators/runtime/inference_plan.cpp — see inference_plan.h.
#include "operators/runtime/inference_plan.h"

#include "geospatial/common.h"

#include "operators/framework/model_readiness.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/eo_preflight.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <functional>

namespace sicnu::operators::runtime
{

namespace
{

const char *rasterOutputModeName( RasterOutputMode mode )
{
  switch ( mode )
  {
    case RasterOutputMode::Probability:
      return "probability";
    case RasterOutputMode::Labels:
      return "labels";
    case RasterOutputMode::Mask:
      return "mask";
    case RasterOutputMode::Confidence:
      return "confidence";
  }
  return "probability";
}

void addFinding( InferencePlan &plan, const std::string &code, const std::string &severity,
                 const std::string &message, const std::string &remediation = {} )
{
  InferencePlanFinding finding;
  finding.code = code;
  finding.severity = severity;
  finding.message = message;
  finding.remediation = remediation;
  plan.findings.push_back( finding );
}

bool hasError( const InferencePlan &plan )
{
  for ( const InferencePlanFinding &finding : plan.findings )
  {
    if ( finding.severity == "error" )
      return true;
  }
  return false;
}

/// One read-only input inspection; failure lands as an error finding.
bool inspectInputRaster( InferencePlan &plan, const ModelInfo &model, const std::string &path,
                         const std::string &name, const std::vector<int> &bandSelection,
                         int frames, InferencePlanInput &input )
{
  GdalDatasetWrapper ds;
  if ( !ds.open( QString::fromStdString( path ) ) )
  {
    addFinding( plan, "plan.input_unreadable", "error",
                "input raster cannot be opened: " + path,
                "verify the path and format before planning an inference" );
    return false;
  }
  input.path = path;
  input.name = name;
  input.width = ds.width();
  input.height = ds.height();
  input.bandCount = ds.bandCount();
  input.crsDeclared = !ds.projection().isEmpty();
  input.frames = frames;
  input.fedBands = bandSelection.empty() ? ds.bandCount()
                                         : static_cast<int>( bandSelection.size() );
  if ( input.bandCount <= 0 )
  {
    addFinding( plan, "plan.input_no_bands", "error",
                "input raster reports no bands: " + path );
    return false;
  }
  for ( int band : bandSelection )
  {
    if ( band < 1 || band > input.bandCount )
    {
      addFinding( plan, "plan.band_out_of_range", "error",
                  "band " + std::to_string( band ) + " out of range (1.."
                      + std::to_string( input.bandCount ) + ")",
                  "adjust the band selection to the raster's band count" );
      return false;
    }
  }
  if ( input.width <= 0 || input.height <= 0 )
  {
    addFinding( plan, "plan.input_empty_grid", "error",
                "input raster has a degenerate grid: " + path );
    return false;
  }
  // The engine's own dtype contract validator over the fed bands (#632/#705:
  // EVERY fed band, not band 1 alone). An empty selection means "all bands".
  std::vector<int> allBands;
  if ( bandSelection.empty() )
  {
    allBands.reserve( input.bandCount );
    for ( int b = 1; b <= input.bandCount; ++b )
      allBands.push_back( b );
  }
  const std::vector<int> &bands = bandSelection.empty() ? allBands : bandSelection;
  const std::string dtypeError = TileInferenceEngine::inputDTypeMismatch(
    model, bands, [ &ds ]( int band ) { return ds.bandDataType( band ); } );
  if ( !dtypeError.empty() )
    addFinding( plan, "plan.dtype_mismatch", "error", dtypeError,
                "align the manifest input.dtype with the raster's band types or "
                "convert the raster" );
  return true;
}

} // namespace

Json::Value InferencePlanFinding::toJson() const
{
  Json::Value json( Json::objectValue );
  json["code"] = code;
  json["severity"] = severity;
  json["message"] = message;
  if ( !remediation.empty() )
    json["remediation"] = remediation;
  return json;
}

Json::Value InferencePlanInput::toJson() const
{
  Json::Value json( Json::objectValue );
  json["path"] = path;
  if ( !name.empty() )
    json["name"] = name;
  json["width"] = width;
  json["height"] = height;
  json["band_count"] = bandCount;
  json["fed_bands"] = fedBands;
  json["crs_declared"] = crsDeclared;
  if ( frames > 1 )
    json["frames"] = frames;
  return json;
}

std::string InferencePlan::verdictString() const
{
  return verdict == Verdict::Ready ? "ready" : "refused";
}

const InferencePlanFinding *InferencePlan::findingCode( const std::string &code ) const
{
  for ( const InferencePlanFinding &finding : findings )
  {
    if ( finding.code == code )
      return &finding;
  }
  return nullptr;
}

Json::Value InferencePlan::toJson() const
{
  Json::Value json( Json::objectValue );
  json["schema"] = "sicnu.inference.plan/1";
  json["schema_version"] = kSchemaVersion;
  json["verdict"] = verdictString();

  Json::Value model( Json::objectValue );
  model["identity_tag"] = modelIdentityTag;
  model["stable_id"] = modelStableId;
  model["task"] = modelTask;
  model["canonical_task"] = canonicalTask;
  model["framework"] = framework;
  if ( !contentDigest.empty() )
    model["content_digest"] = contentDigest;
  if ( isEnsemble )
  {
    model["is_ensemble"] = true;
    model["ensemble_members"] = ensembleMembers;
  }
  json["model"] = model;

  Json::Value runtime( Json::objectValue );
  runtime["ready"] = runtimeReady;
  runtime["readiness"] = runtimeReadinessName;
  if ( !runtimeReason.empty() )
    runtime["reason"] = runtimeReason;
  if ( !requestedDevice.empty() )
    runtime["requested_device"] = requestedDevice;
  runtime["manifest_device"] = manifestDevice;
  json["runtime"] = runtime;

  Json::Value inputs( Json::arrayValue );
  for ( const InferencePlanInput &input : this->inputs )
    inputs.append( input.toJson() );
  json["inputs"] = inputs;

  Json::Value tiling( Json::objectValue );
  tiling["tile_size"] = tileSize;
  tiling["halo"] = halo;
  tiling["grid_x"] = gridX;
  tiling["grid_y"] = gridY;
  tiling["tiles_planned"] = tilesPlanned;
  tiling["batch_size"] = batchSize;
  tiling["batches_estimated"] = batchesEstimated;
  tiling["fed_channels"] = fedChannels;
  tiling["batch_working_set_mib"] = batchWorkingSetMiB;
  json["tiling"] = tiling;

  Json::Value output( Json::objectValue );
  output["path"] = outputPath;
  output["mode"] = rasterOutputModeName( outputMode );
  output["head_contract_ok"] = headContractOk;
  if ( !headContractNote.empty() )
    output["head_contract_note"] = headContractNote;
  output["detection_lane"] = detectionLane;
  output["scene_classification"] = sceneClassification;
  json["output"] = output;

  Json::Value eo( Json::objectValue );
  eo["applicable"] = eoApplicable;
  eo["enforced_ok"] = eoEnforcedOk;
  eo["advisories"] = eoAdvisoryCount;
  json["eo"] = eo;

  Json::Value findings( Json::arrayValue );
  for ( const InferencePlanFinding &finding : this->findings )
    findings.append( finding.toJson() );
  json["findings"] = findings;
  return json;
}

InferencePlan planModelInference( const ModelExecutionRequest &request )
{
  InferencePlan plan;
  plan.outputPath = request.outputPath;
  plan.requestedDevice = request.deviceToken;
  plan.detectionLane = request.asDetection;
  plan.sceneClassification = request.asSceneClassification;
  plan.outputMode = request.outputMode;

  // --- gate 1 (fast fail, run-seam ordering): input existence --------------
  if ( request.namedInputs.empty() && !params::fileExists( request.inputPath ) )
  {
    addFinding( plan, "plan.input_missing", "error",
                "Input raster not found: " + request.inputPath );
    plan.verdict = InferencePlan::Verdict::Refused;
    return plan;
  }

  // --- gate 2: model resolution + readiness --------------------------------
  std::string errorDetail;
  const ModelInfo model = resolveModelReference( request.modelReference, &errorDetail );
  if ( model.readiness != ModelReadiness::Ready )
  {
    plan.modelIdentityTag = model.identityTag();
    plan.modelStableId = model.stableId();
    plan.modelTask = model.task;
    plan.canonicalTask = sicnu::operators::canonicalEoTask( model.task );
    plan.framework = model.framework;
    plan.manifestDevice = model.runtime.device;
    addFinding( plan, "plan.model_not_ready", "error",
                errorDetail.empty() ? std::string( "model is not ready" ) : errorDetail );
    plan.runtimeReadinessName = modelReadinessName( model.readiness );
    plan.verdict = InferencePlan::Verdict::Refused;
    return plan;
  }
  return planModelInference( request, model );
}

InferencePlan planModelInference( const ModelExecutionRequest &request, const ModelInfo &model )
{
  InferencePlan plan;
  plan.outputPath = request.outputPath;
  plan.requestedDevice = request.deviceToken;
  plan.detectionLane = request.asDetection;
  plan.sceneClassification = request.asSceneClassification;
  plan.outputMode = request.outputMode;

  // Inputs were already existence-checked by the resolving overload; feeds
  // still need the per-path check when callers come in through this entry.
  for ( const NamedRasterFeed &feed : request.namedInputs )
  {
    if ( feed.paths.empty() )
      addFinding( plan, "plan.feed_empty", "error",
                  "named input '" + feed.name + "' provides no raster paths" );
    for ( const std::string &path : feed.paths )
    {
      if ( !params::fileExists( path ) )
        addFinding( plan, "plan.input_missing", "error",
                    "Input raster not found (feed '" + feed.name + "'): " + path );
    }
  }
  if ( request.namedInputs.empty() && !params::fileExists( request.inputPath ) )
    addFinding( plan, "plan.input_missing", "error",
                "Input raster not found: " + request.inputPath );

  // --- model identity ------------------------------------------------------
  plan.modelIdentityTag = model.identityTag();
  plan.modelStableId = model.stableId();
  plan.modelTask = model.task;
  plan.canonicalTask = sicnu::operators::canonicalEoTask( model.task );
  plan.framework = model.framework;
  plan.contentDigest = model.contentDigest;
  plan.manifestDevice = model.runtime.device;
  plan.isEnsemble = model.ensemble.declared;
  plan.ensembleMembers = static_cast<int>( model.ensemble.members.size() );

  // --- gate 3: task intent --------------------------------------------------
  if ( !request.requiredEoTask.empty() && plan.canonicalTask != request.requiredEoTask )
  {
    addFinding( plan, "plan.task_intent_mismatch", "error",
                "model '" + model.name + "' declares task '" + model.task
                  + "' which carries no '" + request.requiredEoTask
                  + "' contract — pick a model whose canonical task matches the operator",
                "fix the manifest task or pick the matching operator" );
  }

  // --- gate 4: runtime/device readiness (run-seam ORDER: ensembles route to
  // their own engine BEFORE this gate — mirror that order, per-member
  // readiness is the ensemble engine's concern, not this static check) ------
  if ( plan.isEnsemble )
  {
    plan.runtimeReady = true;
    plan.runtimeReadinessName = modelReadinessName( ModelReadiness::Ready );
    addFinding( plan, "plan.ensemble_route", "info",
                "ensemble model: " + std::to_string( plan.ensembleMembers )
                  + " members execute through the ensemble engine; per-member readiness "
                    "is evaluated there, the grid plan below is the combined skeleton" );
  }
  else
  {
    const ModelHardwareCapabilities hw = ModelRuntimeRegistry::instance().hardware();
    std::string runtimeReason;
    const ModelReadiness runtimeReadiness = evaluateRuntimeReadiness( model, hw, &runtimeReason );
    plan.runtimeReadinessName = modelReadinessName( runtimeReadiness );
    plan.runtimeReason = runtimeReason;
    plan.runtimeReady = runtimeReadiness == ModelReadiness::Ready;
    if ( !plan.runtimeReady )
      addFinding( plan, "plan.runtime_not_ready", "error",
                  "Model '" + model.name + "' cannot execute: " + runtimeReason );
  }

  // --- gate 5: lane incompatibilities --------------------------------------
  if ( !request.namedInputs.empty() && request.asDetection )
    addFinding( plan, "plan.detection_multi_input_unsupported", "error",
                "detection decode runs on the single-input path — multi-input "
                  "detection heads are not wired yet" );
  if ( request.asSceneClassification && ( request.asDetection || !request.namedInputs.empty() ) )
    addFinding( plan, "plan.scene_lane_conflict", "error",
                "scene classification runs the single-input path — it classifies "
                  "ONE scene in ONE forward pass" );

  // --- gate 6: contract wiring + feature cube (the shared predicates) ------
  try
  {
    rejectUnwiredContracts( model, request.namedInputs );
  }
  catch ( const RSOperatorError &error )
  {
    addFinding( plan, "plan.contract_unwired", "error", error.message(),
                "provide the named feeds the model contract demands" );
  }
  if ( request.namedInputs.empty() )
  {
    try
    {
      preflightFeatureCube( model, request.inputPath );
    }
    catch ( const RSOperatorError &error )
    {
      addFinding( plan, "plan.feature_cube_mismatch", "error", error.message() );
    }
  }

  // --- gate 7: device token parsability ------------------------------------
  RequestedDevice device;
  if ( !request.deviceToken.empty() && !RequestedDevice::parse( request.deviceToken, &device ) )
  {
    addFinding( plan, "plan.device_unparsable", "error",
                "device '" + request.deviceToken + "' is not parsable (supported: cpu, "
                  "cuda, cuda:N, auto)" );
  }

  // --- gate 7b: detection knob bounds (the run seam's own [0,1]/(0,1]) ------
  if ( request.asDetection && request.confOverride > 1.0 )
    addFinding( plan, "plan.conf_bounds", "error",
                "conf must be in [0, 1] (declared " + std::to_string( request.confOverride )
                  + ")" );
  if ( request.asDetection && request.nmsIouOverride >= 0.0
       && ( request.nmsIouOverride <= 0.0 || request.nmsIouOverride > 1.0 ) )
    addFinding( plan, "plan.nms_bounds", "error",
                "nms_iou must be in (0, 1] (declared "
                  + std::to_string( request.nmsIouOverride ) + ")" );

  // --- gate 8: input grids (read-only open; no pixels touched) -------------
  int primaryWidth = 0;
  int primaryHeight = 0;
  int fedChannels = 0;
  bool gridsOk = true;
  if ( !request.namedInputs.empty() )
  {
    // Bind feeds to input contracts the way the multi-input engine does:
    // by name first, then positional (model.inputs[feedContract[f]]).
    const auto contractFor = [ &model ]( std::size_t feedIndex,
                                         const std::string &feedName ) -> const ModelInputContract *
    {
      for ( const ModelInputContract &contract : model.inputs )
      {
        if ( !contract.name.empty() && contract.name == feedName )
          return &contract;
      }
      return feedIndex < model.inputs.size() ? &model.inputs[feedIndex] : nullptr;
    };
    for ( std::size_t f = 0; f < request.namedInputs.size(); ++f )
    {
      const NamedRasterFeed &feed = request.namedInputs[f];
      const ModelInputContract *contract = contractFor( f, feed.name );
      // Channels feed the batch budget EXACTLY as the engine accounts them:
      // bands × DECLARED temporal length (the contract axis, not the provided
      // frame count) — the engine budgets on the largest feed.
      const int declaredFrames =
        contract && contract->temporalLength > 0 ? contract->temporalLength : 1;
      const std::string &firstPath = feed.paths.empty() ? std::string() : feed.paths.front();
      InferencePlanInput input;
      if ( !firstPath.empty()
           && inspectInputRaster( plan, model, firstPath, feed.name, feed.bands,
                                  declaredFrames, input ) )
      {
        const std::int64_t channels64 =
          static_cast<std::int64_t>( input.fedBands ) * declaredFrames;
        if ( channels64 > CV_CN_MAX )
          addFinding( plan, "plan.channels_over_limit", "error",
                      "feed '" + feed.name + "': bands × temporal_length = "
                        + std::to_string( channels64 ) + " channels but cv::Mat supports at most "
                        + std::to_string( CV_CN_MAX ),
                      "declare input.bands to select a subset" );
        fedChannels = std::max( fedChannels, static_cast<int>( channels64 ) );
        if ( primaryWidth == 0 )
        {
          primaryWidth = input.width;
          primaryHeight = input.height;
        }
        else if ( input.width != primaryWidth || input.height != primaryHeight )
        {
          addFinding( plan, "plan.feed_grid_difference", "info",
                      "feed '" + feed.name + "' declares a different grid ("
                        + std::to_string( input.width ) + "×" + std::to_string( input.height )
                        + ") than the primary feed (" + std::to_string( primaryWidth ) + "×"
                        + std::to_string( primaryHeight )
                        + ") — the engine verifies co-registration (and pre-aligns when the "
                          "contract allows) at run time" );
        }
        plan.inputs.push_back( input );
      }
      else
      {
        gridsOk = false;
      }
    }
  }
  else if ( inspectInputRaster( plan, model, request.inputPath, {}, request.bands, 1,
                                plan.inputs.emplace_back() ) )
  {
    primaryWidth = plan.inputs.front().width;
    primaryHeight = plan.inputs.front().height;
    fedChannels = plan.inputs.front().fedBands;
    if ( fedChannels > CV_CN_MAX )
      addFinding( plan, "plan.channels_over_limit", "error",
                  "input selection carries " + std::to_string( fedChannels )
                    + " bands but cv::Mat supports at most " + std::to_string( CV_CN_MAX ),
                  "declare input.bands to select a subset" );
  }
  else
  {
    gridsOk = false;
  }

  // --- gate 9: band_roles arity + preprocess channel contract --------------
  if ( gridsOk )
  {
    if ( !model.input.bandRoles.empty()
         && model.input.bandRoles.size()
              != static_cast<std::size_t>( plan.inputs.front().fedBands ) )
      addFinding( plan, "plan.band_roles_arity", "error",
                  "model manifest declares " + std::to_string( model.input.bandRoles.size() )
                    + " band roles but " + std::to_string( plan.inputs.front().fedBands )
                    + " bands are fed",
                  "pass an explicit band list or fix the manifest" );
    const ModelPreprocessContract &pre = model.preprocess;
    if ( pre.normalize == "mean_std" && fedChannels > 0 )
    {
      if ( !pre.mean.empty() && pre.mean.size() != static_cast<std::size_t>( fedChannels ) )
        addFinding( plan, "plan.preprocess_channel_mismatch", "error",
                    "preprocess.mean declares " + std::to_string( pre.mean.size() )
                      + " channels but " + std::to_string( fedChannels )
                      + " bands are fed" );
      if ( !pre.stdv.empty() && pre.stdv.size() != static_cast<std::size_t>( fedChannels ) )
        addFinding( plan, "plan.preprocess_channel_mismatch", "error",
                    "preprocess.std declares " + std::to_string( pre.stdv.size() )
                      + " channels but " + std::to_string( fedChannels )
                      + " bands are fed" );
    }
  }

  // --- gate 10: EFFECTIVE output mode + head contract -----------------------
  // The engine resolves the effective mode from the manifest when the request
  // leaves the default Probability; every derived-mode conflict the run
  // raises is planned here (uncertainty, multi-head, mask_threshold, head
  // role/confidence semantics).
  if ( !request.asDetection && !request.asSceneClassification )
  {
    try
    {
      const RasterOutputMode effectiveMode =
        request.outputMode != RasterOutputMode::Probability
          ? request.outputMode
          : TileInferenceEngine::rasterOutputMode( model );
      plan.outputMode = effectiveMode;
      if ( effectiveMode != RasterOutputMode::Probability )
      {
        const std::string uncertainty = TileInferenceEngine::uncertaintyMethod( model );
        if ( !uncertainty.empty() )
          addFinding( plan, "plan.output_mode_conflict", "error",
                      "output.uncertainty requires the full probability stack — remove "
                        "output.format or output.uncertainty" );
        if ( model.output.tensorNames.size() > 1 )
          addFinding( plan, "plan.output_mode_conflict", "error",
                      "derived output.format applies to single-head models; this manifest "
                        "declares " + std::to_string( model.output.tensorNames.size() )
                        + " tensor heads" );
        if ( effectiveMode == RasterOutputMode::Labels && model.postprocess.maskThreshold >= 0.0 )
          addFinding( plan, "plan.output_mode_conflict", "error",
                      "postprocess.mask_threshold is meaningless with output.format=labels" );
        ensureDerivedModeMatchesHeadContract( model, effectiveMode );
      }
    }
    catch ( const RSOperatorError &error )
    {
      plan.headContractOk = false;
      plan.headContractNote = error.message();
      addFinding( plan, "plan.head_contract_violation", "error", error.message(),
                  "choose an output mode the model's head supports (manifest output.heads)" );
    }
  }

  // --- gate 11: EO domain contract (metadata only) --------------------------
  // Lane fidelity: the raster lane enforces per input, the multi-input lane
  // enforces per feed PATH with the bound contract, the scene lane enforces
  // on its scene window — the DETECTION lane never enforces EO.
  if ( model.eo.declared && !request.asDetection )
  {
    plan.eoApplicable = true;
    const auto enforceOne = [ & ]( const std::string &path, const std::vector<int> &bands,
                                   const ModelInputContract *contract )
    {
      try
      {
        const EoPreflightReport eo = enforceEoPreflight( model, path, bands, contract );
        plan.eoAdvisoryCount += static_cast<int>( eo.advisories.size() );
      }
      catch ( const RSOperatorError &error )
      {
        plan.eoEnforcedOk = false;
        addFinding( plan, "plan.eo_contract_violation", "error", error.message(),
                    "feed an input matching the manifest eo contract (calibration / "
                    "wavelength windows / grid family)" );
      }
      catch ( const geo::GeoError &error )
      {
        // The EO inspector's own metadata reads may fail before any contract
        // verdict — the run seam surfaces that as its typed refusal.
        plan.eoEnforcedOk = false;
        addFinding( plan, "plan.eo_contract_violation", "error",
                    std::string( "eo preflight could not inspect the input: " ) + error.what(),
                    "verify the input raster opens and carries the declared metadata" );
      }
    };
    if ( !request.namedInputs.empty() )
    {
      const auto contractFor = [ &model ]( std::size_t feedIndex,
                                           const std::string &feedName ) -> const ModelInputContract *
      {
        for ( const ModelInputContract &contract : model.inputs )
        {
          if ( !contract.name.empty() && contract.name == feedName )
            return &contract;
        }
        return feedIndex < model.inputs.size() ? &model.inputs[feedIndex] : nullptr;
      };
      for ( std::size_t f = 0; f < request.namedInputs.size(); ++f )
      {
        const ModelInputContract *contract = contractFor( f, request.namedInputs[f].name );
        for ( const std::string &path : request.namedInputs[f].paths )
          enforceOne( path, request.namedInputs[f].bands, contract );
      }
    }
    else
    {
      enforceOne( request.inputPath, request.bands, nullptr );
    }
    if ( plan.eoApplicable && plan.eoAdvisoryCount > 0 && plan.eoEnforcedOk )
      addFinding( plan, "plan.eo_advisories", "info",
                  std::to_string( plan.eoAdvisoryCount )
                    + " EO preflight advisories recorded (non-enforced observations)" );
  }

  // --- gate 12: blend contract ---------------------------------------------
  {
    TileBlend blend = request.blend;
    if ( blend == TileBlend::Unset )
      blend = model.tiling.blend == "feather" ? TileBlend::Feather : TileBlend::None;
    const int halo = std::max( 0, TileInferenceEngine::effectiveHalo( model ) );
    if ( blend == TileBlend::Feather && halo <= 0 )
      addFinding( plan, "plan.blend_without_halo", "error",
                  "tiling.blend=feather requires a halo (tiling.halo or tiling.overlap)" );
  }

  // --- grid / batch plan (the engine's own static planners) -----------------
  if ( request.asSceneClassification )
  {
    // The scene lane is ONE forward pass over the scene window — a tile grid
    // would be fiction, so the plan reports none.
    addFinding( plan, "plan.scene_single_pass", "info",
                "scene classification runs one forward pass — no tile grid applies" );
  }
  else if ( gridsOk && primaryWidth > 0 && primaryHeight > 0 )
  {
    const ModelHardwareCapabilities hw = ModelRuntimeRegistry::instance().hardware();
    const int tileSize =
      std::min( TileInferenceEngine::effectiveTileSize( model ),
                std::max( primaryWidth, primaryHeight ) );
    plan.tileSize = tileSize;
    plan.halo = std::max( 0, TileInferenceEngine::effectiveHalo( model ) );
    plan.gridX = ( primaryWidth + tileSize - 1 ) / tileSize;
    plan.gridY = ( primaryHeight + tileSize - 1 ) / tileSize;
    plan.tilesPlanned = plan.gridX * plan.gridY;
    plan.fedChannels = fedChannels;
    int batchSize = TileInferenceEngine::effectiveBatchSize( model, hw, tileSize, fedChannels );
    if ( request.batchSizeOverride > 0 )
      batchSize = std::min( batchSize, request.batchSizeOverride );
    plan.batchSize = std::max( 1, batchSize );
    plan.batchesEstimated = ( plan.tilesPlanned + plan.batchSize - 1 ) / plan.batchSize;
    // Working-set estimate: one batch of float32 tiles, input + output plane
    // (estimate only — providers may stage more; resize-to-input changes the
    // model-side geometry, not the raster I/O side).
    const double bytesPerTile = static_cast<double>( tileSize ) * tileSize * fedChannels * 4.0;
    plan.batchWorkingSetMiB = plan.batchSize * bytesPerTile * 2.0 / ( 1024.0 * 1024.0 );
  }

  plan.verdict = hasError( plan ) ? InferencePlan::Verdict::Refused : InferencePlan::Verdict::Ready;
  return plan;
}

// ---------------------------------------------------------------------------
Json::Value InferenceQualitySummary::toJson() const
{
  Json::Value json( Json::objectValue );
  json["schema"] = "sicnu.inference.quality/1";
  json["schema_version"] = kSchemaVersion;

  Json::Value coverage( Json::objectValue );
  coverage["tiles_planned"] = tilesPlanned;
  coverage["tiles_processed"] = tilesProcessed;
  coverage["tiles_skipped_nodata"] = tilesSkippedNoData;
  coverage["tile_coverage_ratio"] = tileCoverageRatio;
  coverage["grid_complete"] = gridComplete;
  coverage["batch_reductions"] = batchReductions;
  json["coverage"] = coverage;

  Json::Value product( Json::objectValue );
  product["out_bands"] = outBands;
  product["out_width"] = outWidth;
  product["out_height"] = outHeight;
  if ( isDetection )
  {
    product["raw_detections"] = rawDetections;
    product["detections_kept"] = detectionsKept;
    product["suppression_ratio"] = suppressionRatio;
  }
  json["product"] = product;

  if ( hasClassDistribution )
  {
    Json::Value distribution( Json::objectValue );
    Json::Value counts( Json::arrayValue );
    for ( long long count : classPixelCounts )
      counts.append( static_cast<Json::Int64>( count ) );
    distribution["class_pixel_counts"] = counts;
    distribution["classified_pixel_ratio"] = classifiedPixelRatio;
    distribution["dominant_class"] = dominantClass;
    json["distribution"] = distribution;
  }

  Json::Value eo( Json::objectValue );
  eo["preflight_present"] = eoPreflightPresent;
  eo["advisories"] = eoAdvisoryCount;
  json["eo"] = eo;

  Json::Value notes( Json::arrayValue );
  for ( const InferencePlanFinding &note : this->notes )
    notes.append( note.toJson() );
  json["notes"] = notes;
  return json;
}

InferenceQualitySummary summarizeInferenceQuality( const ModelExecutionResult &result )
{
  const bool rasterRun =
    result.rasterStats.tilesPlanned > 0 || result.rasterStats.tilesProcessed > 0;
  const bool detectionRun =
    result.detectionStats.tilesPlanned > 0 || result.detectionStats.tilesProcessed > 0;
  if ( !rasterRun && !detectionRun )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "quality summary needs raster or detection stats — this result "
                           "carries neither (was the run finished?)" );

  InferenceQualitySummary summary;
  if ( detectionRun )
  {
    const DetectionTileStats &stats = result.detectionStats;
    summary.isDetection = true;
    summary.tilesPlanned = stats.tilesPlanned;
    summary.tilesProcessed = stats.tilesProcessed;
    summary.batchReductions = stats.batchReductions;
    summary.outWidth = stats.rasterWidth;
    summary.outHeight = stats.rasterHeight;
    summary.rawDetections = stats.rawDetections;
    summary.detectionsKept = stats.detectionsKept;
    summary.suppressionRatio =
      stats.rawDetections > 0
        ? 1.0 - static_cast<double>( stats.detectionsKept ) / stats.rawDetections
        : 0.0;
    if ( stats.rawDetections == 0 && stats.tilesProcessed > 0 )
    {
      InferencePlanFinding note;
      note.code = "quality.detection_empty";
      note.severity = "info";
      note.message = "the model produced no detections on any processed tile";
      note.remediation = "verify the confidence threshold against the model contract";
      summary.notes.push_back( note );
    }
  }
  else
  {
    const TileInferenceStats &stats = result.rasterStats;
    summary.tilesPlanned = stats.tilesPlanned;
    summary.tilesProcessed = stats.tilesProcessed;
    summary.tilesSkippedNoData = stats.tilesSkippedNoData;
    summary.batchReductions = stats.batchReductions;
    summary.outBands = stats.outBands;
    summary.outWidth = stats.outWidth;
    summary.outHeight = stats.outHeight;
    if ( !stats.classPixelCounts.empty() )
    {
      summary.hasClassDistribution = true;
      summary.classPixelCounts = stats.classPixelCounts;
      long long total = 0;
      long long best = 0;
      for ( std::size_t i = 0; i < stats.classPixelCounts.size(); ++i )
      {
        total += stats.classPixelCounts[i];
        if ( stats.classPixelCounts[i] > best )
        {
          best = stats.classPixelCounts[i];
          summary.dominantClass = static_cast<int>( i );
        }
      }
      const double pixels =
        static_cast<double>( stats.outWidth ) * static_cast<double>( stats.outHeight );
      summary.classifiedPixelRatio = pixels > 0 ? total / pixels : 0.0;
    }
    // EO presence: single-input lanes record an OBJECT report, multi-input
    // lanes an ARRAY of per-feed reports — anything non-null is presence.
    if ( !stats.eoPreflight.isNull() )
    {
      summary.eoPreflightPresent = true;
      if ( stats.eoPreflight.isObject() )
      {
        const Json::Value advisories = stats.eoPreflight.get( "advisories", Json::Value() );
        summary.eoAdvisoryCount =
          advisories.isArray() ? static_cast<int>( advisories.size() ) : 0;
      }
      else if ( stats.eoPreflight.isArray() )
      {
        for ( const Json::Value &entry : stats.eoPreflight )
        {
          const Json::Value advisories =
            entry.get( "report", Json::Value() ).get( "advisories", Json::Value() );
          if ( advisories.isArray() )
            summary.eoAdvisoryCount += static_cast<int>( advisories.size() );
        }
      }
    }
  }

  if ( summary.tilesPlanned > 0 )
    summary.tileCoverageRatio =
      static_cast<double>( summary.tilesProcessed ) / summary.tilesPlanned;
  summary.gridComplete =
    summary.tilesProcessed + summary.tilesSkippedNoData == summary.tilesPlanned;
  if ( !summary.gridComplete )
  {
    InferencePlanFinding note;
    note.code = "quality.grid_incomplete";
    note.severity = "warning";
    note.message = "processed (" + std::to_string( summary.tilesProcessed ) + ") + skipped ("
                 + std::to_string( summary.tilesSkippedNoData ) + ") != planned ("
                 + std::to_string( summary.tilesPlanned ) + ") — tiles were lost, not skipped";
    note.remediation = "re-run and compare; report the run id if the loss reproduces";
    summary.notes.push_back( note );
  }
  if ( summary.batchReductions > 0 )
  {
    InferencePlanFinding note;
    note.code = "quality.oom_ladder_fired";
    note.severity = "info";
    note.message = std::to_string( summary.batchReductions )
                 + " batch reduction(s) fired during the run (OOM ladder)";
    note.remediation = "lower the batch cap or free device memory to avoid retry latency";
    summary.notes.push_back( note );
  }
  return summary;
}

} // namespace sicnu::operators::runtime
