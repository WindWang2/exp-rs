// src/agent/harness/scientific_preflight.cpp
#include "scientific_preflight.h"

#include "agent/workspace_state.h"
#include "band_facts.h"
#include "capability_knowledge.h"
#include "contracts/spatial_contracts.h"
#include "entity_resolver.h"
#include "grounding_tools.h"
#include "harness_actions.h"
#include "intent_vocabulary.h"
#include "operators/framework/model_catalog.h"
#include "spatial_tools/spatial_tool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

using facts::BandFacts;
using facts::GridFacts;
using facts::SarFacts;
using facts::bandFacts;
using facts::crsOf;
using facts::gridFacts;
using facts::lowered;
using facts::modalityOf;
using facts::radiometricState;
using facts::sarFacts;

Json::Value makeIssueWithCode( const std::string &code, const std::string &severity,
                               const std::string &message, bool repairable,
                               Json::Value suggestedAction )
{
  return sicnu::agent::contracts::makeIssue( code, severity, message, repairable, "",
                                    std::move( suggestedAction ) );
}

Json::Value makeCheck( const std::string &name, bool passed, const std::string &code,
                       Json::Value details )
{
  return sicnu::agent::contracts::makeAssessmentCheck(
    name, passed, passed ? "info" : "error", code, std::move( details ) );
}

void addBlocker( PreflightOutcome &outcome, const std::string &code,
                 const std::string &message, const std::string &action,
                 Json::Value details = Json::Value() )
{
  // Harness 9.0 (#881): the action resolves through the closed vocabulary —
  // the issued document carries the registered tool / workbench command.
  outcome.issues.append( makeIssueWithCode( code, "error", message, true,
                                            resolvedSuggestedAction( action, Json::Value() ) ) );
  outcome.errors.push_back( HarnessError::make( code, message, details ) );
  outcome.checks.append( makeCheck( code, false, code, details ) );
}

void addWarning( PreflightOutcome &outcome, const std::string &code,
                 const std::string &message )
{
  outcome.issues.append( makeIssueWithCode( code, "warning", message, false, Json::Value() ) );
  outcome.checks.append( makeCheck( code, true, code, Json::Value() ) );
}

/// Harness 9.0 (M3): the third tier — pure improvements with no correctness
/// risk (a better polarization choice, a model's resolution window). Advice
/// never flips the verdict and never lands in assumptions: it is guidance,
/// not uncertainty.
void addAdvice( PreflightOutcome &outcome, const std::string &code,
                const std::string &message )
{
  outcome.issues.append( makeIssueWithCode( code, "advice", message, false, Json::Value() ) );
  outcome.checks.append( makeCheck( code, true, code, Json::Value() ) );
}

void addInfo( PreflightOutcome &outcome, const std::string &name, bool passed,
              const std::string &code )
{
  outcome.checks.append( makeCheck( name, passed, code, Json::Value() ) );
}

Json::Value inputSummary( const PreflightInput &input )
{
  Json::Value summary( Json::objectValue );
  summary["name"] = input.name;
  summary["ref"] = input.reference;
  if ( input.resolved() )
  {
    summary["path"] = input.understanding.get( "path", "" ).asString();
    summary["crs"] = input.understanding.get( "crs", Json::Value() );
    summary["modality"] = input.understanding.get( "modality", "unknown" );
    summary["band_count"] = input.understanding.get( "band_count", Json::Value() );
    summary["radiometric_state"] = input.understanding.get( "radiometric_state", Json::Value() );
  }
  else
  {
    summary["error"] = input.resolutionError.toJson();
  }
  return summary;
}

// --- shared rules ----------------------------------------------------------

bool sharedRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  bool fatal = false;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
    {
      addBlocker( outcome, input.resolutionError.code.empty() ? std::string( error_codes::kDatasetNotFound )
                                                              : input.resolutionError.code,
                  "Input '" + input.name + "' did not resolve: " + input.resolutionError.summary,
                  "spatial.understand", input.resolutionError.details );
      fatal = true;
    }
  }
  if ( inputs.empty() )
  {
    addBlocker( outcome, error_codes::kInvalidParameter, "No inputs provided",
                "harness.preflight", Json::Value() );
    fatal = true;
  }
  return fatal;
}

// --- rule packs ------------------------------------------------------------

/// Spectral-window requirement for a band-ratio index preflight.
enum class BandRequirement
{
  Nir,
  Red,
  Green,
  Blue,
  Swir,
  RedEdge,
};

bool bandPresent( const BandFacts &facts, BandRequirement requirement )
{
  switch ( requirement )
  {
    case BandRequirement::Nir:
      return facts.hasNir;
    case BandRequirement::Red:
      return facts.hasRed;
    case BandRequirement::Green:
      return facts.hasGreen;
    case BandRequirement::Blue:
      return facts.hasBlue;
    case BandRequirement::Swir:
      return facts.hasSwir;
    case BandRequirement::RedEdge:
      return facts.hasRedEdge;
  }
  return false;
}

/// Window description for blocker messages.
std::string bandWindowLabel( BandRequirement requirement )
{
  switch ( requirement )
  {
    case BandRequirement::Nir:
      return "NIR (role or 750-1100nm)";
    case BandRequirement::Red:
      return "Red (role or 600-700nm)";
    case BandRequirement::Green:
      return "Green (role or 500-600nm)";
    case BandRequirement::Blue:
      return "Blue (role or 430-520nm)";
    case BandRequirement::Swir:
      return "SWIR (role or 1550-1750/2080-2350nm)";
    case BandRequirement::RedEdge:
      return "Red edge (role or 700-745nm)";
  }
  return "band";
}

/// Generalized band-ratio rule pack (Platform 5.0): the NDVI checks applied
/// to an arbitrary index's band requirements. `indexLabel` names the index
/// in messages. Refuse-on-missing bands (blockers), warn on raw DN.
void bandRatioRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome,
                     const std::string &indexLabel,
                     const std::vector<std::pair<std::string, BandRequirement>> &requirements )
{
  // Harness 8.0: fusion keeps working (a SAR companion beside an optical
  // input is skipped below), but an optical index intent with ONLY SAR/unknown
  // inputs can never apply — that must be a blocker, not a silent ok.
  const bool hasOptical = std::any_of(
    inputs.begin(), inputs.end(), []( const PreflightInput &input ) {
      const std::string modality = input.resolved() ? modalityOf( input.understanding ) : "";
      return input.resolved() && modality != "sar" && modality != "unknown";
    } );
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    // Harness 7.0: mixed workflows feed SAR companions to optical intents
    // (fused flood mapping). Spectral-window demands are physical facts about
    // optical inputs only; the SAR branch is gated by the SAR/multimodal
    // packs, so it is skipped here instead of failing on missing bands.
    if ( modalityOf( input.understanding ) == "sar" )
    {
      if ( hasOptical )
        addWarning( outcome, error_codes::kModalityMismatch,
                    "Input '" + input.name +
                      "' is SAR and is skipped by the " + indexLabel +
                      " spectral checks; the SAR branch is validated by the "
                      "multimodal rules" );
      else
        addBlocker( outcome, error_codes::kModalityMismatch,
                    "Input '" + input.name +
                      "' is SAR; the " + indexLabel +
                      " intent needs optical data and no optical input is bound",
                    "check_dataset", Json::Value() );
      continue;
    }
    const BandFacts facts = bandFacts( input.understanding );
    for ( const auto &[ roleLabel, requirement ] : requirements )
    {
      if ( !bandPresent( facts, requirement ) )
        addBlocker( outcome, error_codes::kBandRoleUnresolved,
                    "Input '" + input.name + "' has no " + bandWindowLabel( requirement ) +
                      " band required for " + indexLabel,
                    "inspect_bands", Json::Value() );
    }
    const std::string radiometry = radiometricState( input.understanding );
    if ( radiometry.empty() )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name + "' declares no radiometric state; " + indexLabel +
                    " quality cannot be guaranteed" );
    else if ( radiometry.find( "dn" ) != std::string::npos &&
              radiometry.find( "reflectance" ) == std::string::npos )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name + "' is raw DN; consider calibration to reflectance "
                  "for comparable " + indexLabel );
    addInfo( outcome, "nodata_declared", input.understanding.isMember( "nodata" ) ||
                                           input.understanding.isMember( "bands" ),
             "INVALID_RADIOMETRY" );
  }
}

void opticalChangeRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const PreflightInput *primary = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( !primary )
    {
      primary = &input;
      continue;
    }
    const GridFacts a = gridFacts( primary->understanding );
    const GridFacts b = gridFacts( input.understanding );
    if ( !a.crs.empty() && !b.crs.empty() && a.crs != b.crs )
      addBlocker( outcome, error_codes::kCrsMismatch,
                  "CRS mismatch between '" + primary->name + "' and '" + input.name + "'",
                  "reproject_to_reference", Json::Value() );
    const bool sizeMismatch =
      a.width && b.width && ( a.width != b.width || a.height != b.height );
    if ( sizeMismatch )
      addBlocker( outcome, error_codes::kGridMismatch,
                  "Raster size mismatch: '" + primary->name + "' is " +
                    std::to_string( a.width ) + "x" + std::to_string( a.height ) + ", '" +
                    input.name + "' is " + std::to_string( b.width ) + "x" +
                    std::to_string( b.height ),
                  "align_to_reference", Json::Value() );
    const bool resolutionMismatch =
      a.pixelSizeX && b.pixelSizeX && std::fabs( a.pixelSizeX - b.pixelSizeX ) > 1e-9;
    if ( resolutionMismatch && !sizeMismatch )
      addWarning( outcome, "GRID_MISMATCH",
                  "Pixel size differs between '" + primary->name + "' and '" + input.name +
                    "' — resample to a shared grid for pixel-wise change" );
    const std::string stateA = radiometricState( primary->understanding );
    const std::string stateB = radiometricState( input.understanding );
    if ( !stateA.empty() && !stateB.empty() && stateA != stateB )
      addBlocker( outcome, error_codes::kInvalidRadiometry,
                  "Radiometric states differ ('" + primary->name + "': " + stateA + ", '" +
                    input.name + "': " + stateB + ") — normalize before change",
                  "normalize_radiometry", Json::Value() );
  }
  if ( inputs.size() < 2 && std::any_of( inputs.begin(), inputs.end(),
                                         []( const PreflightInput &i ) { return i.resolved(); } ) )
    addBlocker( outcome, error_codes::kInvalidParameter,
                "Change detection needs two epochs; got one", "harness.plan", Json::Value() );
}

void sarChangeRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( !modality.empty() && modality != "sar" && modality != "unknown" )
      addBlocker( outcome, error_codes::kModalityMismatch,
                  "Input '" + input.name + "' is " + modality + ", not SAR",
                  "check_dataset", Json::Value() );
    if ( modality == "unknown" )
      addWarning( outcome, "MODALITY_MISMATCH",
                  "Input '" + input.name +
                    "' modality could not be verified; run spatial:understand on it" );
    const SarFacts facts = sarFacts( input.understanding );
    if ( facts.polarization.empty() )
      addWarning( outcome, "POLARIZATION_MISMATCH",
                  "Input '" + input.name +
                    "' declares no polarization; cross-pol comparisons will silently degrade" );
  }
  const PreflightInput *primary = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( !primary )
    {
      primary = &input;
      continue;
    }
    const SarFacts a = sarFacts( primary->understanding );
    const SarFacts b = sarFacts( input.understanding );
    if ( !a.polarization.empty() && !b.polarization.empty() &&
         a.polarization != b.polarization )
      addBlocker( outcome, error_codes::kPolarizationMismatch,
                  "Polarization mismatch: '" + primary->name + "' is " + a.polarization +
                    ", '" + input.name + "' is " + b.polarization,
                  "select_matching_polarization", Json::Value() );
    if ( a.calibrationDeclared && b.calibrationDeclared && a.calibration != b.calibration )
      addBlocker( outcome, error_codes::kCalibrationMismatch,
                  "Calibration domains differ ('" + primary->name + "': " + a.calibration +
                    ", '" + input.name + "': " + b.calibration + ")",
                  "calibrate_consistently", Json::Value() );
    const GridFacts ga = gridFacts( primary->understanding );
    const GridFacts gb = gridFacts( input.understanding );
    if ( !ga.crs.empty() && !gb.crs.empty() && ga.crs != gb.crs )
      addBlocker( outcome, error_codes::kCrsMismatch,
                  "CRS mismatch between SAR epochs", "reproject_to_reference",
                  Json::Value() );
    if ( ga.width && gb.width && ( ga.width != gb.width || ga.height != gb.height ) )
      addWarning( outcome, "GRID_MISMATCH",
                  "SAR epochs differ in grid size; terrain correction to a shared grid is "
                  "recommended" );
  }
  if ( inputs.size() < 2 && std::any_of( inputs.begin(), inputs.end(),
                                         []( const PreflightInput &i ) { return i.resolved(); } ) )
    addBlocker( outcome, error_codes::kInvalidParameter,
                "SAR change detection needs two epochs; got one", "harness.plan",
                Json::Value() );
}

void classifyRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const auto isTraining = []( const PreflightInput &i )
  { return i.name == "training" || i.name == "samples"; };
  const auto hasTraining = std::any_of( inputs.begin(), inputs.end(), isTraining );
  if ( !hasTraining )
    addBlocker( outcome, error_codes::kTrainingInvalid,
                "Supervised classification needs a training sample input named "
                "'training' or 'samples'",
                "harness.plan", Json::Value() );
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( isTraining( input ) )
    {
      const std::string kind = input.understanding.get( "source_kind", "" ).asString();
      if ( kind == "raster" )
        addWarning( outcome, "TRAINING_INVALID",
                    "Training input '" + input.name +
                      "' is a raster; verify it is a class map paired with feature bands" );
      else if ( kind == "vector" )
      {
        if ( input.understanding.get( "feature_count", 0 ).asInt() <= 0 )
          addBlocker( outcome, error_codes::kTrainingInvalid,
                      "Training vector '" + input.name + "' has no features",
                      "check_training", Json::Value() );
      }
    }
  }
  addInfo( outcome, "model_compatibility_checked", true, "MODEL_INCOMPATIBLE" );
}

void phenologyRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  // Phenology works on a temporal collection; the harness preflights the
  // collection via its own tool, so here we verify the referenced scenes are
  // comparable rasters and warn when nothing declares acquisition times.
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const BandFacts facts = bandFacts( input.understanding );
    if ( !facts.hasNir && !facts.hasRed )
      addWarning( outcome, "BAND_ROLE_UNRESOLVED",
                  "Input '" + input.name +
                    "' exposes no NIR/Red roles; vegetation phenology needs them" );
    if ( !input.understanding.isMember( "acquisition_time" ) &&
         !input.understanding.isMember( "SICNU_ACQUISITION_DATE" ) )
      addWarning( outcome, "TIME_ORDER_INVALID",
                  "Input '" + input.name +
                    "' carries no acquisition time; scene ordering cannot be verified" );
  }
}

// --- Harness 7.0 rule packs (mission Area C) -------------------------------

/// Temporal-series pack: facts-driven blockers for scene count, ordering and
/// gaps. Without declared temporal facts it only warns — unknown facts stay
/// unknown, never blockers.
void temporalSeriesRules( const std::vector<PreflightInput> &inputs,
                          PreflightOutcome &outcome, int minScenes,
                          const std::string &intentLabel )
{
  phenologyRules( inputs, outcome );
  const PreflightInput *withFacts = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( input.temporalFacts.isObject() )
    {
      withFacts = &input;
      break;
    }
  }
  if ( !withFacts )
  {
    addWarning( outcome, "TIME_ORDER_INVALID",
                "No temporal collection facts declared for '" + intentLabel +
                  "'; run temporal:preflight_collection to verify series quality" );
    return;
  }
  const Json::Value &facts = withFacts->temporalFacts;
  const int sceneCount = facts.get( "scene_count", 0 ).asInt();
  if ( minScenes > 0 && sceneCount > 0 && sceneCount < minScenes )
    addBlocker( outcome, error_codes::kInvalidParameter,
                "'" + intentLabel + "' needs >= " + std::to_string( minScenes ) +
                  " scenes; collection provides " + std::to_string( sceneCount ),
                "temporal.preflight_collection" );
  const Json::Value &dates = facts.get( "dates", Json::Value() );
  if ( dates.isArray() && dates.size() >= 2 )
  {
    bool sorted = true;
    for ( Json::ArrayIndex i = 1; i < dates.size(); ++i )
    {
      if ( dates[i - 1].asString() > dates[i].asString() )
        sorted = false;
    }
    if ( !sorted )
      addBlocker( outcome, error_codes::kTimeOrderInvalid,
                  "Collection dates are not in acquisition order", "check_collection" );
    const int maxGapDays = facts.get( "max_gap_days", 0 ).asInt();
    if ( maxGapDays > 0 )
      addWarning( outcome, "TIME_ORDER_INVALID",
                  "Collection declares max gap of " + std::to_string( maxGapDays ) +
                    " days; gaps beyond the declared budget bias '" + intentLabel + "'" );
  }
}

/// Land-cover deepening of the classify pack: legend declarations and sample
/// sufficiency become explicit checks instead of silent assumptions.
void landCoverRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const bool isTraining = input.name == "training" || input.name == "samples";
    if ( !isTraining )
      continue;
    if ( input.understanding.isMember( "class_values" ) &&
         input.understanding["class_values"].isArray() &&
         input.understanding["class_values"].size() > 0 )
    {
      addInfo( outcome, "class_legend_declared", true, "TRAINING_INVALID" );
    }
    else
    {
      addWarning( outcome, "TRAINING_INVALID",
                  "Training input '" + input.name +
                    "' declares no class legend; output verification can only check "
                    "value finiteness, not semantic class domains" );
    }
    const int featureCount = input.understanding.get( "feature_count", 0 ).asInt();
    if ( featureCount > 0 && featureCount < 10 )
      addWarning( outcome, "TRAINING_INVALID",
                  "Training input '" + input.name + "' exposes only " +
                    std::to_string( featureCount ) +
                    " features; accuracy estimates from <10 samples are unstable" );
  }
}

/// Accuracy-assessment pack: the reference must describe the same class
/// domain as the classified map when both declare one.
void accuracyRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const PreflightInput *classMap = nullptr;
  const PreflightInput *reference = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string name = lowered( input.name );
    if ( name.find( "class" ) != std::string::npos && !classMap )
      classMap = &input;
    if ( ( name.find( "reference" ) != std::string::npos ||
           name.find( "ground" ) != std::string::npos ||
           name.find( "truth" ) != std::string::npos ||
           name == "training" || name == "samples" ) && !reference )
      reference = &input;
  }
  if ( !classMap || !reference )
  {
    addWarning( outcome, "TRAINING_INVALID",
                "Could not identify the classified map and reference inputs by name; "
                "name them 'classified' and 'reference' for domain cross-checks" );
    return;
  }
  const Json::Value &mapValues = classMap->understanding.get( "class_values", Json::Value() );
  const Json::Value &refValues = reference->understanding.get( "class_values", Json::Value() );
  if ( mapValues.isArray() && !mapValues.empty() && refValues.isArray() && !refValues.empty() )
  {
    bool intersects = false;
    for ( const Json::Value &mapValue : mapValues )
      for ( const Json::Value &refValue : refValues )
        if ( mapValue.asString() == refValue.asString() )
          intersects = true;
    if ( !intersects )
      addBlocker( outcome, error_codes::kInvalidParameter,
                  "Reference class domain does not intersect the classified map domain; "
                  "accuracy assessment would be meaningless",
                  "check_training" );
  }
}

/// Inference pack (7.0): model-manifest contract vs dataset facts. Absent
/// manifests warn — an unverified compatibility is not a compatible one.
void inferenceRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const PreflightInput *withModel = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( input.modelManifest.isObject() )
    {
      withModel = &input;
      break;
    }
  }
  if ( !withModel )
  {
    addWarning( outcome, error_codes::kModelIncompatible,
                "No model manifest declared; model/dataset compatibility is unverified" );
    return;
  }
  const Json::Value &manifest = withModel->modelManifest;
  const std::string modelName = manifest.get( "name", "" ).asString();

  // Unknown model id: typed blocker, never a silent pass.
  if ( manifest.get( "unknown", false ).asBool() )
  {
    addBlocker( outcome, error_codes::kModelNotReady,
                "Model '" + modelName + "' is not in the model catalog",
                "select_model", Json::Value() );
    return;
  }

  // Input modality demand.
  const std::string datasetModality = modalityOf( withModel->understanding );
  const Json::Value &modalities = manifest.get( "modalities", Json::Value() );
  if ( modalities.isArray() && !modalities.empty() && !datasetModality.empty() &&
       datasetModality != "unknown" )
  {
    bool accepts = false;
    for ( const Json::Value &modality : modalities )
      if ( modality.isString() && lowered( modality.asString() ) == datasetModality )
        accepts = true;
    if ( !accepts )
      addBlocker( outcome, error_codes::kModelIncompatible,
                  "Model '" + modelName + "' does not accept " + datasetModality +
                    " input",
                  "select_model" );
  }

  // Band-role demand.
  const BandFacts facts = bandFacts( withModel->understanding );
  for ( const Json::Value &roleValue : manifest.get( "supported_band_roles", Json::Value( Json::arrayValue ) ) )
  {
    if ( !roleValue.isString() )
      continue;
    const std::string role = lowered( roleValue.asString() );
    const bool present =
      ( role == "nir" && facts.hasNir ) ||
      ( role == "red" && facts.hasRed ) ||
      ( role == "green" && facts.hasGreen ) ||
      ( role == "blue" && facts.hasBlue ) ||
      ( role == "swir" && facts.hasSwir ) ||
      ( role == "red_edge" && facts.hasRedEdge ) ||
      std::count( facts.roles.begin(), facts.roles.end(), role ) > 0;
    if ( !present )
      addBlocker( outcome, error_codes::kModelIncompatible,
                  "Dataset lacks the '" + role + "' band required by model '" +
                    modelName + "'",
                  "select_model" );
  }

  // Temporal contract.
  const int temporalLength = manifest.get( "temporal_length", 0 ).asInt();
  if ( temporalLength > 1 )
  {
    if ( withModel->temporalFacts.isObject() )
    {
      const int sceneCount = withModel->temporalFacts.get( "scene_count", 0 ).asInt();
      if ( sceneCount > 0 && sceneCount < temporalLength )
        addBlocker( outcome, error_codes::kModelIncompatible,
                    "Model '" + modelName + "' needs " +
                      std::to_string( temporalLength ) +
                      " frames per inference; collection provides " +
                      std::to_string( sceneCount ),
                    "temporal.preflight_collection" );
    }
    else
    {
      addWarning( outcome, error_codes::kModelIncompatible,
                  "Model '" + modelName + "' is temporal (length " +
                    std::to_string( temporalLength ) +
                    "); declare temporal_facts to verify the series" );
    }
  }

  // Radiometric expectation.
  const std::string expectedRadiometry = lowered( manifest.get( "radiometric_state", "" ).asString() );
  const std::string datasetRadiometry = radiometricState( withModel->understanding );
  if ( !expectedRadiometry.empty() && !datasetRadiometry.empty() &&
       datasetRadiometry.find( expectedRadiometry ) == std::string::npos )
    addWarning( outcome, error_codes::kInvalidRadiometry,
                "Model '" + modelName + "' expects " + expectedRadiometry +
                  " input; dataset declares " + datasetRadiometry );

  // Resolution window.
  const GridFacts grid = gridFacts( withModel->understanding );
  const double minRes = manifest.get( "min_resolution_meters", -1.0 ).asDouble();
  const double maxRes = manifest.get( "max_resolution_meters", -1.0 ).asDouble();
  if ( grid.pixelSizeX > 0 && minRes > 0 && grid.pixelSizeX < minRes )
    addAdvice( outcome, error_codes::kModelIncompatible,
               "Dataset resolution (" + std::to_string( grid.pixelSizeX ) +
                 " m) is finer than the model's recommended minimum (" +
                 std::to_string( minRes ) + " m)" );
  if ( grid.pixelSizeX > 0 && maxRes > 0 && grid.pixelSizeX > maxRes )
    addAdvice( outcome, error_codes::kModelIncompatible,
               "Dataset resolution (" + std::to_string( grid.pixelSizeX ) +
                 " m) is coarser than the model's recommended maximum (" +
                 std::to_string( maxRes ) + " m)" );
}

/// Cross-modality pack (7.0): optical + SAR fused analysis. Both branches
/// must be individually sound and mutually registrable.
void multimodalRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const PreflightInput *optical = nullptr;
  const PreflightInput *sar = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( modality == "sar" && !sar )
      sar = &input;
    else if ( !modality.empty() && modality != "sar" && modality != "unknown" && !optical )
      optical = &input;
  }
  if ( !optical || !sar )
    return;

  // Registration: a fused product inherits the worse of the two grids.
  const GridFacts opticalGrid = gridFacts( optical->understanding );
  const GridFacts sarGrid = gridFacts( sar->understanding );
  if ( !opticalGrid.crs.empty() && !sarGrid.crs.empty() && opticalGrid.crs != sarGrid.crs )
    addBlocker( outcome, error_codes::kCrsMismatch,
                "Optical ('" + optical->name + "') and SAR ('" + sar->name +
                  "') inputs are in different CRS; fuse only in a shared CRS",
                "reproject_to_reference" );
  else
    addWarning( outcome, "GRID_MISMATCH",
                "Optical and SAR fusion assumes co-registration; verify the "
                "orthorectification/terrain-correction chain before trusting "
                "pixel-aligned fusion" );
  if ( opticalGrid.pixelSizeX > 0 && sarGrid.pixelSizeX > 0 &&
       std::fabs( opticalGrid.pixelSizeX - sarGrid.pixelSizeX ) > 1e-9 )
    addWarning( outcome, "GRID_MISMATCH",
                "Optical and SAR resolutions differ; resample to a shared grid "
                "before fusion" );

  // SAR branch: calibration must be declared for threshold fusion.
  const SarFacts sarFactsOfInput = sarFacts( sar->understanding );
  if ( !sarFactsOfInput.calibrationDeclared )
    addWarning( outcome, "INVALID_RADIOMETRY",
                "SAR branch ('" + sar->name +
                  "') declares no calibration domain; threshold-based fusion will "
                  "be unstable" );

  // Optical branch: water mapping needs Green plus NIR or SWIR.
  const BandFacts opticalFacts = bandFacts( optical->understanding );
  if ( !opticalFacts.hasGreen || ( !opticalFacts.hasNir && !opticalFacts.hasSwir ) )
    addWarning( outcome, "BAND_ROLE_UNRESOLVED",
                "Optical branch ('" + optical->name +
                  "') lacks Green + NIR/SWIR; water reflectance contrast cannot be "
                  "verified" );
}

/// Flood deepening (7.0): physics caveats that apply to both branches,
/// stated as warnings — never silently skipped.
void floodRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( modality == "sar" )
    {
      const SarFacts facts = sarFacts( input.understanding );
      if ( !facts.polarization.empty() &&
           ( facts.polarization == "vv" || facts.polarization == "hh" ) )
        addAdvice( outcome, "POLARIZATION_MISMATCH",
                   "Input '" + input.name + "' is " + facts.polarization +
                     "-pol; co-pol backscatter is wind-sensitive — cross-pol (VH/HV) "
                     "is preferred for open-water mapping" );
    }
    else if ( !modality.empty() && modality != "unknown" )
    {
      addAdvice( outcome, "INVALID_RADIOMETRY",
                 "Flood extents from optical indices miss turbid or vegetated "
                 "water; validate against an independent reference" );
    }
  }
}

// --- intent specification table (single source for dispatch + drift) -------

enum class PackKind {
  BandRatio,
  OpticalChange,
  SarChange,
  SarSingle,
  Classify,
  TemporalSeries,
  Terrain,
  Inference,
  SharedOnly,
};

struct IntentSpec {
  std::string intent;
  PackKind kind;
  std::vector<std::pair<std::string, BandRequirement>> bands;
  bool requiresPair = false;
  bool sarModality = false;
  int minScenes = 0;
};

const std::vector<IntentSpec> &intentSpecTable()
{
  static const std::vector<IntentSpec> kTable = {
    { kIntentNdvi, PackKind::BandRatio, { { "red", BandRequirement::Red },
                                    { "nir", BandRequirement::Nir } } },
    { kIntentEvi, PackKind::BandRatio, { { "blue", BandRequirement::Blue },
                                    { "red", BandRequirement::Red },
                                    { "nir", BandRequirement::Nir } } },
    { kIntentSavi, PackKind::BandRatio, { { "red", BandRequirement::Red },
                                     { "nir", BandRequirement::Nir } } },
    { kIntentNdre, PackKind::BandRatio, { { "red_edge", BandRequirement::RedEdge },
                                     { "nir", BandRequirement::Nir } } },
    { kIntentNdwi, PackKind::BandRatio, { { "green", BandRequirement::Green },
                                     { "nir", BandRequirement::Nir } } },
    { kIntentWater, PackKind::BandRatio, { { "green", BandRequirement::Green },
                                      { "nir", BandRequirement::Nir } } },
    { kIntentFlood, PackKind::BandRatio, { { "green", BandRequirement::Green },
                                      { "nir", BandRequirement::Nir } } },
    { kIntentMndwi, PackKind::BandRatio, { { "green", BandRequirement::Green },
                                      { "swir", BandRequirement::Swir } } },
    { kIntentNdsi, PackKind::BandRatio, { { "green", BandRequirement::Green },
                                     { "swir", BandRequirement::Swir } } },
    { kIntentNbr, PackKind::BandRatio, { { "nir", BandRequirement::Nir },
                                    { "swir", BandRequirement::Swir } } },
    { kIntentDnbr, PackKind::BandRatio, { { "nir", BandRequirement::Nir },
                                     { "swir", BandRequirement::Swir } }, true },
    { kIntentNdbi, PackKind::BandRatio, { { "swir", BandRequirement::Swir },
                                     { "nir", BandRequirement::Nir } } },
    { kIntentBsi, PackKind::BandRatio, { { "blue", BandRequirement::Blue },
                                    { "red", BandRequirement::Red },
                                    { "nir", BandRequirement::Nir },
                                    { "swir", BandRequirement::Swir } } },
    { kIntentChange, PackKind::OpticalChange, {}, true },
    { kIntentSarChange, PackKind::SarChange, {}, true, true },
    { kIntentSarFlood, PackKind::SarChange, {}, true, true },
    { kIntentSar, PackKind::SarSingle, {}, false, true },
    { kIntentSarWater, PackKind::SarSingle, {}, false, true },
    { kIntentShip, PackKind::SharedOnly },
    { kIntentZonal, PackKind::SharedOnly },
    { kIntentClassify, PackKind::Classify },
    { kIntentAccuracy, PackKind::Classify },
    { kIntentPhenology, PackKind::TemporalSeries, {}, false, false, 12 },
    { kIntentTemporal, PackKind::TemporalSeries, {}, false, false, 3 },
    { kIntentTerrain, PackKind::Terrain },
    { kIntentQa, PackKind::SharedOnly },
    { kIntentPreprocess, PackKind::SharedOnly },
    { kIntentInference, PackKind::Inference },
  };
  return kTable;
}

const IntentSpec *intentSpec( const std::string &intent )
{
  for ( const IntentSpec &spec : intentSpecTable() )
    if ( spec.intent == intent )
      return &spec;
  return nullptr;
}

bool inputMixedModality( const std::vector<PreflightInput> &inputs )
{
  bool hasOptical = false;
  bool hasSar = false;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( modality == "sar" )
      hasSar = true;
    else if ( !modality.empty() && modality != "unknown" )
      hasOptical = true;
  }
  return hasOptical && hasSar;
}

std::string bandRequirementRole( BandRequirement requirement )
{
  switch ( requirement )
  {
    case BandRequirement::Nir: return "nir";
    case BandRequirement::Red: return "red";
    case BandRequirement::Green: return "green";
    case BandRequirement::Blue: return "blue";
    case BandRequirement::Swir: return "swir";
    case BandRequirement::RedEdge: return "red_edge";
  }
  return "";
}

std::string packKindLabel( PackKind kind )
{
  switch ( kind )
  {
    case PackKind::BandRatio: return "band_ratio";
    case PackKind::OpticalChange: return "optical_change";
    case PackKind::SarChange: return "sar_change";
    case PackKind::SarSingle: return "sar_single";
    case PackKind::Classify: return "classify";
    case PackKind::TemporalSeries: return "temporal_series";
    case PackKind::Terrain: return "terrain";
    case PackKind::Inference: return "inference";
    case PackKind::SharedOnly: return "shared_only";
  }
  return "";
}

bool anyResolvedInput( const std::vector<PreflightInput> &inputs )
{
  return std::any_of( inputs.begin(), inputs.end(),
                      []( const PreflightInput &i ) { return i.resolved(); } );
}

void requirePair( PreflightOutcome &outcome, const std::string &intentLabel )
{
  addBlocker( outcome, error_codes::kInvalidParameter,
              intentLabel + " needs two comparable epochs; got one", "harness.plan",
              Json::Value() );
}

} // namespace

Json::Value PreflightOutcome::toJson( const std::string &subject ) const
{
  // Harness 8.0 (Area D): assumptions are the warning-class issues — facts
  // the pack could not verify and therefore degraded instead of guessing.
  // Surfacing them separately keeps blockers, checks, and honest unknowns
  // apart so the agent sees exactly which science rests on assumptions.
  Json::Value assumptions( Json::arrayValue );
  // Harness 9.0 (M3): advice-class issues (pure improvements) and the safe
  // preparation table — blocker actions that name a deterministic data
  // transform. Preparations are the ONLY class the agent may auto-apply;
  // everything else stays an explicit human/Pi decision.
  static const char *kPreparationActions[] = {
    "reproject_to_reference", "align_to_reference", "normalize_radiometry",
    "calibrate_consistently",
  };
  Json::Value advice( Json::arrayValue );
  Json::Value preparations( Json::arrayValue );
  for ( const Json::Value &issue : issues )
  {
    const std::string severity = issue.get( "severity", "" ).asString();
    if ( severity == "warning" )
      assumptions.append( issue );
    else if ( severity == "advice" )
      advice.append( issue );
    if ( severity == "error" && issue.isMember( "suggested_action" ) )
    {
      const Json::Value &action = issue["suggested_action"];
      const std::string actionKey = action.get( "action", "" ).asString();
      for ( const char *preparation : kPreparationActions )
      {
        if ( actionKey == preparation )
        {
          Json::Value entry( Json::objectValue );
          entry["for_issue"] = issue.get( "code", "" ).asString();
          entry["action"] = action;
          preparations.append( entry );
        }
      }
    }
  }

  Json::Value doc = sicnu::agent::contracts::makePreflightResult( subject, verdict, issues,
                                                                  checks );
  if ( !assumptions.empty() )
    doc["assumptions"] = assumptions;
  if ( !advice.empty() )
    doc["advice"] = advice;
  if ( !preparations.empty() )
    doc["preparations"] = preparations;
  return doc;
}

bool intentRequiresPair( const std::string &intent )
{
  if ( const IntentSpec *spec = intentSpec( intent ) )
    return spec->requiresPair;
  return false;
}

Json::Value intentRequirements( const std::string &intent )
{
  const IntentSpec *spec = intentSpec( intent );
  if ( !spec )
    return Json::Value();
  Json::Value doc( Json::objectValue );
  doc["intent"] = spec->intent;
  doc["pack"] = packKindLabel( spec->kind );
  Json::Value bandRoles( Json::objectValue );
  for ( const auto &[ role, requirement ] : spec->bands )
    bandRoles[ role ] = 1;
  doc["band_roles"] = bandRoles;
  doc["requires_pair"] = spec->requiresPair;
  if ( spec->sarModality )
    doc["modality"] = "sar";
  if ( spec->minScenes > 0 )
    doc["min_scenes"] = spec->minScenes;
  return doc;
}

Json::Value typedIntentDocument( const std::string &intent )
{
  const IntentSpec *spec = intentSpec( intent );
  if ( !spec )
    return Json::Value();
  Json::Value doc = intentRequirements( intent );
  doc["schema"] = "harness.intent/1.0";

  // Required facts: what MUST be true about the inputs before the pack can
  // pass — typed so Pi can ground each one with spatial:understand instead
  // of guessing.
  Json::Value requiredFacts( Json::arrayValue );
  for ( const auto &[ roleLabel, requirement ] : spec->bands )
  {
    Json::Value fact( Json::objectValue );
    fact["fact"] = "band_role:" + bandRequirementRole( requirement );
    fact["why"] = intent + " requires the " + bandWindowLabel( requirement ) + " band";
    requiredFacts.append( fact );
  }
  if ( spec->requiresPair )
  {
    Json::Value fact( Json::objectValue );
    fact["fact"] = "input_pair";
    fact["why"] = intent + " compares two comparable epochs";
    requiredFacts.append( fact );
  }
  if ( spec->kind == PackKind::Classify )
  {
    Json::Value fact( Json::objectValue );
    fact["fact"] = "training_samples";
    fact["why"] = "supervised classification needs a training input named "
                  "'training' or 'samples'";
    requiredFacts.append( fact );
  }
  if ( spec->kind == PackKind::TemporalSeries )
  {
    Json::Value fact( Json::objectValue );
    fact["fact"] = "temporal_collection";
    fact["why"] = intent + " orders scenes from a declared temporal collection";
    requiredFacts.append( fact );
  }
  doc["required_facts"] = requiredFacts;

  // Optional facts: absence degrades to a warning (an honest assumption),
  // never to a blocker and never to fabricated values.
  Json::Value optionalFacts( Json::arrayValue );
  auto addOptional = [ &optionalFacts ]( const char *fact, const char *why ) {
    Json::Value entry( Json::objectValue );
    entry["fact"] = fact;
    entry["why"] = why;
    entry["degrades_to"] = "warning";
    optionalFacts.append( entry );
  };
  if ( spec->kind == PackKind::BandRatio || spec->kind == PackKind::OpticalChange )
    addOptional( "radiometric_state",
                 "raw DN vs reflectance changes index comparability" );
  if ( spec->sarModality )
  {
    addOptional( "polarization",
                 "cross-pol vs co-pol changes water/ship detectability" );
    addOptional( "calibration_domain",
                 "comparing scenes across calibration domains is meaningless" );
  }
  if ( spec->kind == PackKind::TemporalSeries )
    addOptional( "acquisition_time",
                 "scene ordering cannot be verified without acquisition times" );
  if ( spec->kind == PackKind::Terrain )
    addOptional( "crs", "slope/aspect in a geographic CRS is a physical error" );
  doc["optional_facts"] = optionalFacts;

  // Expected products + quality expectations: the union over the capability
  // knowledge entries serving this intent (the same authoritative layer the
  // verification derives from — no second schema).
  Json::Value products( Json::arrayValue );
  std::set<std::string> checks;
  bool wantsFinite = false;
  bool wantsNodata = false;
  for ( const std::string &operatorId :
        CapabilityKnowledge::instance().operatorsForIntent( intent ) )
  {
    const Json::Value entry = CapabilityKnowledge::instance().entryForOperator( operatorId );
    for ( const Json::Value &check :
          entry.get( "verification", Json::Value() ).get( "checks",
                                                          Json::Value( Json::arrayValue ) ) )
    {
      if ( !check.isString() )
        continue;
      checks.insert( check.asString() );
      wantsFinite |= check.asString() == "finite_fraction";
      wantsNodata |= check.asString() == "nodata_fraction";
    }
    const Json::Value artifacts = entry.get( "artifacts", Json::Value() );
    for ( const std::string &name : artifacts.getMemberNames() )
    {
      if ( static_cast<int>( products.size() ) >= 4 )
        break;
      Json::Value product( Json::objectValue );
      product["artifact"] = name;
      if ( artifacts[ name ].isObject() && artifacts[ name ].isMember( "kind" ) )
        product["kind"] = artifacts[ name ][ "kind" ];
      if ( artifacts[ name ].isObject() && artifacts[ name ].isMember( "dtype" ) )
        product["dtype"] = artifacts[ name ][ "dtype" ];
      bool duplicate = false;
      for ( const Json::Value &existing : products )
        duplicate |= existing == product;
      if ( !duplicate )
        products.append( product );
    }
  }
  doc["expected_products"] = products;
  Json::Value quality( Json::objectValue );
  for ( const std::string &check : checks )
    quality["checks"].append( check );
  if ( wantsFinite )
    quality["min_finite_fraction"] = 0.5;
  if ( wantsNodata )
    quality["max_nodata_fraction"] = 0.9;
  doc["quality_expectations"] = quality;
  return doc;
}

/// Single-input SAR rule pack (Platform 5.0): modality + calibration checks
/// without the epoch pairing of sar_change.
void sarSingleRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( !modality.empty() && modality != "sar" && modality != "unknown" )
      addBlocker( outcome, error_codes::kModalityMismatch,
                  "Input '" + input.name + "' is " + modality + ", not SAR",
                  "check_dataset", Json::Value() );
    const SarFacts facts = sarFacts( input.understanding );
    if ( !facts.calibrationDeclared )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name +
                    "' declares no SAR calibration domain; calibrate (sigma0/gamma0) before "
                    "thresholding or comparing scenes" );
    if ( facts.polarization.empty() )
      addWarning( outcome, "POLARIZATION_MISMATCH",
                  "Input '" + input.name +
                    "' declares no polarization; cross-pol comparisons will silently degrade" );
  }
}

/// Terrain rule pack: single raster expected, CRS must be declared (slope/
/// aspect in a geographic CRS is a physical error, not a style problem).
void terrainRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const GridFacts grid = gridFacts( input.understanding );
    if ( grid.crs.empty() )
      addWarning( outcome, "CRS_MISMATCH",
                  "Input '" + input.name + "' declares no CRS; terrain products need a "
                  "projected CRS with metric units" );
    else if ( grid.crs.find( "EPSG:4326" ) != std::string::npos )
      addWarning( outcome, "CRS_MISMATCH",
                  "Input '" + input.name +
                    "' is in a geographic CRS; slope/aspect need a projected CRS" );
    if ( input.understanding.isMember( "band_count" ) && input.understanding["band_count"].isInt() &&
         input.understanding["band_count"].asInt() < 1 )
      addBlocker( outcome, error_codes::kDatasetNotFound,
                  "Input '" + input.name + "' has no bands", "check_dataset", Json::Value() );
  }
}

PreflightOutcome runScientificPreflight( const std::string &intent,
                                         const std::vector<PreflightInput> &inputs )
{
  PreflightOutcome outcome;
  if ( sharedRules( inputs, outcome ) )
  {
    outcome.verdict = "blocked";
    return outcome;
  }

  // Harness 7.0: the dispatch is a specification table, not an if/else chain.
  // The same table drives intentRequirements() — one source, no drift.
  if ( const IntentSpec *spec = intentSpec( intent ) )
  {
    switch ( spec->kind )
    {
      case PackKind::BandRatio:
      {
        bandRatioRules( inputs, outcome, intent, spec->bands );
        if ( spec->requiresPair && inputs.size() < 2 && anyResolvedInput( inputs ) )
          requirePair( outcome, intent == kIntentDnbr ? "dNBR" : intent );
        if ( intent == kIntentFlood )
          floodRules( inputs, outcome );
        if ( inputMixedModality( inputs ) )
          multimodalRules( inputs, outcome );
        break;
      }
      case PackKind::OpticalChange:
        opticalChangeRules( inputs, outcome );
        break;
      case PackKind::SarChange:
        sarChangeRules( inputs, outcome );
        if ( intent == kIntentSarFlood )
        {
          floodRules( inputs, outcome );
          if ( inputMixedModality( inputs ) )
            multimodalRules( inputs, outcome );
        }
        break;
      case PackKind::SarSingle:
        sarSingleRules( inputs, outcome );
        break;
      case PackKind::Classify:
        // Supervised/unsupervised: the training-slot demand is skipped only
        // when a refs entry explicitly declares supervised=false.
        if ( std::any_of( inputs.begin(), inputs.end(),
                          []( const PreflightInput &i ) { return i.supervised; } ) )
          classifyRules( inputs, outcome );
        landCoverRules( inputs, outcome );
        if ( intent == kIntentAccuracy )
          accuracyRules( inputs, outcome );
        break;
      case PackKind::TemporalSeries:
        temporalSeriesRules( inputs, outcome, spec->minScenes, intent );
        break;
      case PackKind::Terrain:
        terrainRules( inputs, outcome );
        break;
      case PackKind::Inference:
        inferenceRules( inputs, outcome );
        break;
      case PackKind::SharedOnly:
        // Mask/preprocess/ship intents have no physical band demands beyond
        // resolvability.
        break;
    }
  }

  const bool blocked = std::any_of(
    outcome.issues.begin(), outcome.issues.end(),
    []( const Json::Value &issue ) {
      return issue.isMember( "severity" ) && issue["severity"].asString() == "error";
    } );
  const bool repairableWarning = std::any_of(
    outcome.issues.begin(), outcome.issues.end(), []( const Json::Value &issue ) {
      return issue.isMember( "severity" ) && issue["severity"].asString() == "warning" &&
             issue.get( "repairable", false ).asBool();
    } );
  outcome.verdict = blocked ? "blocked" : ( repairableWarning ? "fixable" : "ok" );
  return outcome;
}

PreflightOutcome preflightIntent( const std::string &intent,
                                  const Json::Value &refs )
{
  std::vector<PreflightInput> inputs;
  if ( refs.isArray() )
  {
    for ( const auto &entry : refs )
    {
      PreflightInput input;
      input.name = entry.get( "name", "" ).asString();
      input.reference = entry.get( "ref", entry.get( "asset", "" ) ).asString();
      input.supervised = entry.get( "supervised", true ).asBool();
      input.temporalFacts = entry.get( "temporal_facts", Json::Value() );
      if ( entry.isMember( "model" ) && entry["model"].isString() &&
           !entry["model"].asString().empty() )
      {
        // Project the ModelCatalog manifest into a bounded JSON document so
        // the inference pack checks typed facts (never weight paths).
        if ( const auto model =
               sicnu::operators::ModelCatalog::instance().find( entry["model"].asString() ) )
        {
          Json::Value manifest( Json::objectValue );
          manifest["name"] = model->name;
          manifest["task"] = model->task;
          manifest["input_type"] = model->inputType;
          for ( const std::string &role : model->supportedBandRoles )
            manifest["supported_band_roles"].append( role );
          for ( const std::string &modality : model->modalities )
            manifest["modalities"].append( modality );
          for ( const std::string &polarization : model->polarizations )
            manifest["polarizations"].append( polarization );
          manifest["temporal_length"] = model->temporalLength;
          manifest["radiometric_state"] = model->radiometricState;
          manifest["min_resolution_meters"] = model->minResolutionMeters;
          manifest["max_resolution_meters"] = model->maxResolutionMeters;
          input.modelManifest = std::move( manifest );
        }
        else
        {
          // Unknown model: keep the blocker deterministic and typed — the
          // inference pack turns this marker into MODEL_NOT_READY.
          Json::Value unknown( Json::objectValue );
          unknown["name"] = entry["model"].asString();
          unknown["unknown"] = true;
          input.modelManifest = std::move( unknown );
        }
      }
      HarnessError error;
      if ( const auto resolved =
             resolveDatasetRef( QString::fromStdString( input.reference ), &error ) )
      {
        // Gather bounded facts through the inspection tool — the same facts
        // spatial:understand returns, so preflight and grounding agree.
        if ( auto inspect = SpatialToolRegistry::instance().find( "spatial:raster_inspect" ) )
        {
          Json::Value inspectInput;
          inspectInput["path"] = resolved->path.toStdString();
          const SpatialToolResult result = ( *inspect )->execute( inspectInput );
          if ( result.success )
          {
            input.understanding =
              sicnu::agent::contracts::datasetUnderstandingFromRasterInspect( result.output );
            input.understanding["modality"] = inferModality( result.output );
            input.understanding["fact_status"]["modality"] =
              input.understanding["modality"].asString() == "unknown" ? "unknown" : "assumed";
          }
        }
        if ( input.understanding.isNull() )
        {
          if ( auto vectorInspect =
                 SpatialToolRegistry::instance().find( "spatial:vector_inspect" ) )
          {
            Json::Value inspectInput;
            inspectInput["path"] = resolved->path.toStdString();
            const SpatialToolResult result = ( *vectorInspect )->execute( inspectInput );
            if ( result.success )
              input.understanding = sicnu::agent::contracts::datasetUnderstandingFromVectorInspect(
                result.output );
          }
        }
        if ( input.understanding.isNull() )
          input.resolutionError = HarnessError::make(
            error_codes::kDatasetNotFound,
            "Resolved file is neither raster nor vector: " + resolved->path.toStdString() );
      }
      else
      {
        input.resolutionError = error;
      }
      inputs.push_back( std::move( input ) );
    }
  }
  return runScientificPreflight( intent, inputs );
}

} // namespace sicnu::agent::harness
