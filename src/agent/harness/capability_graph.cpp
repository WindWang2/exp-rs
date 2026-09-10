// src/agent/harness/capability_graph.cpp
#include "capability_graph.h"

#include "agent_plan.h"
#include "band_facts.h"
#include "capability_knowledge.h"
#include "contracts/spatial_contracts.h"
#include "entity_resolver.h"
#include "grounding_tools.h"
#include "harness_error.h"
#include "recipe_catalog.h"
#include "spatial_tools/spatial_tool.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include <QString>

namespace sicnu::agent::harness {

namespace {

using facts::bandFacts;
using facts::lowered;

/// Intent trigger table: fixed declaration order (ties break alphabetically
/// downstream). One entry per closed intent that free text can serve; the
/// qa/preprocess intents are matched only through their explicit terms.
struct IntentTriggers {
  std::string intent;
  std::vector<std::string> terms;
};

const std::vector<IntentTriggers> &triggerTable()
{
  static const std::vector<IntentTriggers> kTable = {
    // Compound / most specific first — resolved by scoring, not order, but
    // keeping multi-word terms here documents them next to their singles.
    { "sar_flood", { "sar flood", "sar 洪水", "雷达洪水", "sar inundation" } },
    { "sar_change", { "sar change", "sar 变化", "雷达变化" } },
    { "sar_water", { "sar water", "sar 水体", "雷达水体" } },
    { "ndvi", { "ndvi", "植被指数", "vegetation index" } },
    { "evi", { "evi" } },
    { "savi", { "savi" } },
    { "ndre", { "ndre" } },
    { "ndwi", { "ndwi" } },
    { "mndwi", { "mndwi" } },
    { "ndsi", { "ndsi", "snow index", "雪指数" } },
    { "nbr", { "nbr", "normalized burn", "燃烧指数" } },
    { "dnbr", { "dnbr", "dnbr", "delta nbr", "差分燃烧" } },
    { "ndbi", { "ndbi", "built-up index", "建筑指数" } },
    { "bsi", { "bsi", "bare soil", "裸土指数" } },
    { "water", { "water body", "water extraction", "水体提取", "水体", "水面", "water" } },
    { "flood", { "flood", "洪水", "淹没", "内涝", "inundation" } },
    { "sar", { "sar", "radar", "雷达", "合成孔径" } },
    { "ship", { "ship", "vessel", "船只", "船舶" } },
    { "change", { "change detection", "变化检测", "bitemporal", "bi-temporal", "双时相",
                  "变化", "change" } },
    { "classify", { "classification", "classify", "land cover", "land use", "土地覆盖",
                    "土地利用", "kmeans", "监督分类", "supervised", "segmentation", "分割",
                    "obia" } },
    { "phenology", { "phenology", "物候", "growing season", "生长季" } },
    { "temporal", { "time series", "temporal", "时间序列", "时序", "trend", "趋势" } },
    { "terrain", { "terrain", "slope", "aspect", "hillshade", "dem", "地形", "坡度",
                   "坡向", "山体阴影", "高程" } },
    { "accuracy", { "accuracy", "confusion matrix", "精度", "混淆矩阵", "验证样本" } },
    { "qa", { "cloud mask", "qa", "云掩膜", "云 mask", "质量波段" } },
    { "preprocess", { "preprocess", "preprocessing", "mosaic", "reproject", "clip",
                      "orthorectify", "atmospheric correction", "预处理", "镶嵌", "重投影",
                      "裁剪", "正射", "大气校正" } },
    { "inference", { "inference", "deep learning", "unet", "segformer", "推理", "深度学习",
                     "模型推理" } },
  };
  return kTable;
}

/// SAR co-occurrence boosts: SAR + a science term compounds into the SAR
/// intent family instead of leaving a tie between two plausible readings.
struct CompoundRule {
  const char *gate;      ///< term that must be present (SAR family marker)
  const char *baseIntent;///< science intent scored by the plain terms
  const char *compoundIntent;
};

const std::vector<CompoundRule> &compoundRules()
{
  static const std::vector<CompoundRule> kRules = {
    { "sar", "flood", "sar_flood" },
    { "sar", "change", "sar_change" },
    { "sar", "water", "sar_water" },
    { "雷达", "flood", "sar_flood" },
    { "雷达", "change", "sar_change" },
    { "雷达", "水体", "sar_water" },
  };
  return kRules;
}

bool contains( const std::string &text, const std::string &term )
{
  return text.find( term ) != std::string::npos;
}

} // namespace

Json::Value IntentResolution::ambiguityError() const
{
  Json::Value details( Json::objectValue );
  details["status"] = status;
  details["candidates"] = candidates;
  return HarnessError::make(
    error_codes::kIntentAmbiguous,
    status == "unresolved"
      ? "Goal text carries no recognizable scientific intent; inspect the "
        "dataset and rephrase with an explicit intent"
      : "Goal text matches several scientific intents equally; disambiguate "
        "instead of guessing",
    details, true,
    Json::Value( Json::arrayValue ) ).toJson();
}

IntentResolution resolveGoalIntent( const std::string &goalText )
{
  const std::string text = lowered( goalText );

  std::map<std::string, int> scores;
  std::map<std::string, std::vector<std::string>> evidence;

  for ( const IntentTriggers &entry : triggerTable() )
  {
    for ( const std::string &term : entry.terms )
    {
      if ( contains( text, lowered( term ) ) )
      {
        scores[ entry.intent ] += 1;
        evidence[ entry.intent ].push_back( term );
      }
    }
  }

  // Compound boosts: a SAR marker plus a science term reads as the SAR
  // variant of that science intent.
  for ( const CompoundRule &rule : compoundRules() )
  {
    if ( contains( text, rule.gate ) && scores.count( rule.baseIntent ) )
    {
      scores[ rule.compoundIntent ] += 2;
      evidence[ rule.compoundIntent ].push_back(
        std::string( rule.gate ) + "+" + rule.baseIntent );
    }
  }

  IntentResolution resolution;
  if ( scores.empty() )
  {
    resolution.status = "unresolved";
    return resolution;
  }

  std::vector<std::pair<std::string, int>> ordered( scores.begin(), scores.end() );
  std::stable_sort( ordered.begin(), ordered.end(),
                    []( const auto &a, const auto &b ) { return a.second > b.second; } );

  for ( const auto &[ intent, score ] : ordered )
  {
    Json::Value candidate( Json::objectValue );
    candidate["intent"] = intent;
    candidate["score"] = score;
    Json::Value matched( Json::arrayValue );
    for ( const std::string &term : evidence[ intent ] )
      matched.append( term );
    candidate["matched"] = matched;
    resolution.candidates.append( candidate );
  }

  const bool tied = ordered.size() > 1 && ordered[0].second == ordered[1].second;
  if ( tied )
  {
    resolution.status = "ambiguous";
    return resolution;
  }

  resolution.status = "resolved";
  resolution.intent = ordered[0].first;
  for ( const std::string &term : evidence[ resolution.intent ] )
    resolution.matchedTerms.append( term );
  return resolution;
}

Json::Value evaluateFeasibility( const Json::Value &capabilityEntry,
                                 const Json::Value &understanding )
{
  Json::Value result( Json::objectValue );
  result["capability_id"] = capabilityEntry.get( "id", "" ).asString();
  result["feasible"] = true;
  result["score"] = 1.0;
  result["why"] = Json::Value( Json::arrayValue );
  result["why_not"] = Json::Value( Json::arrayValue );

  const auto block = [ & ]( const std::string &code, const std::string &message ) {
    result["feasible"] = false;
    result["score"] = 0.0;
    Json::Value entry( Json::objectValue );
    entry["code"] = code;
    entry["message"] = message;
    result["why_not"].append( entry );
  };
  const auto warn = [ & ]( const std::string &code, const std::string &message ) {
    Json::Value entry( Json::objectValue );
    entry["code"] = code;
    entry["message"] = message;
    result["why_not"].append( entry );
    result["score"] = std::max( 0.0, result["score"].asDouble() - 0.25 );
  };
  const auto why = [ & ]( const std::string &message ) { result["why"].append( message ); };

  if ( capabilityEntry.isNull() )
  {
    block( error_codes::kNotSupported, "No capability knowledge for this operator" );
    return result;
  }
  if ( understanding.isNull() || !understanding.isObject() )
  {
    warn( error_codes::kDatasetNotFound,
          "No dataset understanding available; run spatial:understand first" );
    return result;
  }

  const std::string datasetModality = facts::modalityOf( understanding );
  bool modalityChecked = false;
  bool modalityMatched = false;
  for ( const Json::Value &modality : capabilityEntry.get( "modality", Json::Value( Json::arrayValue ) ) )
  {
    if ( !modality.isString() )
      continue;
    modalityChecked = true;
    if ( modality.asString() == datasetModality )
    {
      modalityMatched = true;
      why( "Dataset modality '" + datasetModality + "' matches capability demand" );
      break;
    }
  }
  if ( modalityChecked && !modalityMatched && !datasetModality.empty() &&
       datasetModality != "unknown" )
  {
    block( error_codes::kModalityMismatch,
           "Dataset modality '" + datasetModality +
             "' is not among the capability's input modalities" );
    return result;
  }

  // Band-role demands against the dataset's actual bands/roles.
  const facts::BandFacts datasetBands = bandFacts( understanding );
  for ( const std::string &role : capabilityEntry.get( "band_roles", Json::Value() ).getMemberNames() )
  {
    const int minimum = CapabilityKnowledge::bandRoleMinimum( capabilityEntry, role );
    if ( minimum <= 0 )
      continue;
    const bool present =
      ( role == "nir" && datasetBands.hasNir ) ||
      ( role == "red" && datasetBands.hasRed ) ||
      ( role == "green" && datasetBands.hasGreen ) ||
      ( role == "blue" && datasetBands.hasBlue ) ||
      ( role == "swir" && datasetBands.hasSwir ) ||
      ( role == "red_edge" && datasetBands.hasRedEdge ) ||
      ( role != "nir" && role != "red" && role != "green" && role != "blue" &&
        role != "swir" && role != "red_edge" &&
        std::count( datasetBands.roles.begin(), datasetBands.roles.end(), role ) >= minimum );
    if ( !present )
    {
      block( error_codes::kBandRoleUnresolved,
             "Dataset exposes no '" + role + "' band required by this capability" );
    }
    else
    {
      why( "Dataset provides the required '" + role + "' band" );
    }
  }

  // Radiometric preference.
  const std::string radiometry = facts::radiometricState( understanding );
  const Json::Value &radiometric = capabilityEntry.get( "radiometric", Json::Value() );
  if ( !radiometric.isNull() && radiometric.isObject() && !radiometry.empty() )
  {
    bool acceptable = false;
    bool warned = false;
    for ( const Json::Value &state : radiometric.get( "acceptable", Json::Value( Json::arrayValue ) ) )
      if ( state.isString() && radiometry.find( lowered( state.asString() ) ) != std::string::npos )
        acceptable = true;
    for ( const Json::Value &state : radiometric.get( "warn", Json::Value( Json::arrayValue ) ) )
      if ( state.isString() && radiometry.find( lowered( state.asString() ) ) != std::string::npos )
        warned = true;
    if ( !acceptable && warned )
      warn( error_codes::kInvalidRadiometry,
            "Dataset radiometric state '" + radiometry +
              "' is workable but not ideal for this capability" );
    else if ( !acceptable )
      warn( error_codes::kInvalidRadiometry,
            "Dataset radiometric state '" + radiometry +
              "' is undeclared or outside the capability's preferred states" );
    else
      why( "Radiometric state '" + radiometry + "' is acceptable" );
  }

  // SAR demands.
  const Json::Value &sar = capabilityEntry.get( "sar", Json::Value() );
  if ( !sar.isNull() && sar.isObject() && datasetModality == "sar" )
  {
    const facts::SarFacts datasetSar = facts::sarFacts( understanding );
    if ( datasetSar.calibrationDeclared )
      why( "SAR calibration domain declared (" + datasetSar.calibration + ")" );
    else
      warn( error_codes::kInvalidRadiometry,
            "SAR dataset declares no calibration domain" );
  }

  // Temporal demands.
  const Json::Value &temporal = capabilityEntry.get( "temporal", Json::Value() );
  if ( !temporal.isNull() && temporal.isObject() )
  {
    const int minScenes = temporal.get( "min_scenes", 0 ).asInt();
    const int sceneCount = understanding.get( "scene_count", understanding.get( "band_count", 0 ) ).asInt();
    if ( minScenes > 0 && sceneCount > 0 && sceneCount < minScenes )
      block( error_codes::kInvalidParameter,
             "Temporal capability needs >= " + std::to_string( minScenes ) +
               " scenes; dataset provides " + std::to_string( sceneCount ) );
    const bool requiresTime = temporal.get( "requires_acquisition_time", false ).asBool();
    if ( requiresTime && facts::acquisitionTime( understanding ).empty() )
      warn( error_codes::kTimeOrderInvalid,
            "Capability requires acquisition times; dataset declares none" );
  }

  return result;
}

Json::Value capabilityCandidates( const std::string &intent,
                                  const Json::Value &understanding )
{
  Json::Value document( Json::objectValue );
  document["intent"] = intent;
  document["candidates"] = Json::Value( Json::arrayValue );
  document["total"] = 0;

  if ( !isKnownIntent( intent ) )
  {
    Json::Value details( Json::objectValue );
    details["intent"] = intent;
    document["error"] = HarnessError::make( error_codes::kInvalidParameter,
                                            "Unknown intent: " + intent, details )
                          .toJson();
    return document;
  }

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  struct Scored {
    Json::Value candidate;
    double score = 0.0;
    bool feasible = false;
  };
  std::vector<Scored> scored;
  for ( const std::string &operatorId : knowledge.operatorsForIntent( intent ) )
  {
    const Json::Value entry = knowledge.entryForOperator( operatorId );
    Json::Value feasibility = evaluateFeasibility( entry, understanding );
    feasibility["capability_id"] = operatorId;

    Scored item;
    item.candidate = std::move( feasibility );
    item.score = item.candidate["score"].asDouble();
    item.feasible = item.candidate["feasible"].asBool();
    scored.push_back( std::move( item ) );
  }

  std::stable_sort( scored.begin(), scored.end(),
                    []( const Scored &a, const Scored &b ) {
                      if ( a.feasible != b.feasible )
                        return a.feasible;
                      return a.score > b.score;
                    } );

  for ( Scored &item : scored )
    document["candidates"].append( item.candidate );
  document["total"] = static_cast<Json::Int>( scored.size() );
  return document;
}

// ---------------------------------------------------------------------------
// Harness 8.0 (Area C): missing facts, preparation, and solution paths.
// ---------------------------------------------------------------------------

namespace {

/// True when the understanding document carries a usable slot value.
bool hasSlot( const Json::Value &understanding, const char *slot )
{
  if ( understanding.isNull() || !understanding.isObject() )
    return false;
  if ( !understanding.isMember( slot ) )
    return false;
  const Json::Value &value = understanding[ slot ];
  if ( value.isString() )
    return !value.asString().empty();
  if ( value.isArray() )
    return !value.empty();
  if ( value.isObject() )
    return !value.empty();
  return !value.isNull();
}

} // namespace

Json::Value missingFactsForIntent( const std::string &intent,
                                   const Json::Value &understanding )
{
  Json::Value document( Json::objectValue );
  document["intent"] = intent;
  document["missing_facts"] = Json::Value( Json::arrayValue );
  if ( !isKnownIntent( intent ) )
  {
    document["error"] = HarnessError::make( error_codes::kInvalidParameter,
                                            "Unknown intent: " + intent )
                          .toJson();
    return document;
  }

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  // Deduped fact slots, in a fixed inspection order.
  bool missingModality = false;
  bool missingBandRoles = false;
  bool missingRadiometry = false;
  bool missingAcquisitionTime = false;
  bool demandsAny = false;

  for ( const std::string &operatorId : knowledge.operatorsForIntent( intent ) )
  {
    const Json::Value entry = knowledge.entryForOperator( operatorId );
    if ( entry.get( "modality", Json::Value() ).isArray() &&
         !entry["modality"].empty() && !hasSlot( understanding, "modality" ) )
      missingModality = true;
    if ( entry.get( "band_roles", Json::Value() ).isObject() &&
         !entry["band_roles"].empty() && !hasSlot( understanding, "band_roles" ) )
      missingBandRoles = true;
    if ( entry.isMember( "radiometric" ) && !hasSlot( understanding, "radiometric_state" ) )
      missingRadiometry = true;
    const Json::Value &temporal = entry.get( "temporal", Json::Value() );
    if ( temporal.isObject() &&
         ( temporal.get( "min_scenes", 0 ).asInt() > 0 ||
           temporal.get( "requires_acquisition_time", false ).asBool() ) &&
         !hasSlot( understanding, "acquisition_time" ) &&
         !hasSlot( understanding, "scene_dates" ) )
      missingAcquisitionTime = true;
    demandsAny = true;
  }

  if ( !demandsAny )
    return document;

  const auto append = [ & ]( const char *fact, const char *whyNeeded ) {
    Json::Value entry( Json::objectValue );
    entry["fact"] = fact;
    entry["why_needed"] = whyNeeded;
    entry["how_to_obtain"] = "spatial:understand {asset: <dataset ref>}";
    document["missing_facts"].append( entry );
  };
  if ( missingModality )
    append( "modality", "feasibility cannot distinguish optical/sar/dem demands" );
  if ( missingBandRoles )
    append( "band_roles", "band-role requirements cannot be checked against the scene" );
  if ( missingRadiometry )
    append( "radiometric_state",
            "radiometric suitability (DN vs reflectance vs backscatter) is unknown" );
  if ( missingAcquisitionTime )
    append( "acquisition_time", "temporal ordering and scene-floor checks need dates" );

  // Bounded.
  while ( document["missing_facts"].size() > 8 )
    document["missing_facts"].removeIndex( document["missing_facts"].size() - 1, nullptr );
  return document;
}

Json::Value preparationForWhyNot( const Json::Value &whyNot )
{
  Json::Value document( Json::objectValue );
  document["preparations"] = Json::Value( Json::arrayValue );
  if ( !whyNot.isArray() )
    return document;

  const auto prepare = [ & ]( const char *code, const char *action, const char *tool,
                              const char *recipe ) {
    Json::Value entry( Json::objectValue );
    Json::Value preparations( Json::arrayValue );
    Json::Value step( Json::objectValue );
    step["action"] = action;
    if ( tool )
      step["tool"] = tool;
    if ( recipe )
      step["recipe"] = recipe;
    preparations.append( step );
    entry["code"] = code;
    entry["preparations"] = preparations;
    document["preparations"].append( entry );
  };

  // Static code→action table (H8 test pins the codes it covers). Only
  // structurally safe, deterministic preparation steps appear here; codes
  // whose "fix" would be a science decision carry no_safe_preparation.
  static const std::set<std::string> kNoSafePreparation = {
    error_codes::kModalityMismatch, error_codes::kPolarizationMismatch,
    error_codes::kNotSupported,     error_codes::kDatasetNotFound,
  };
  bool sawUnsafe = false;
  for ( const Json::Value &entry : whyNot )
  {
    if ( !entry.isObject() || !entry.isMember( "code" ) || !entry["code"].isString() )
      continue;
    const std::string code = entry["code"].asString();
    if ( kNoSafePreparation.count( code ) )
    {
      sawUnsafe = true;
      continue;
    }
    if ( code == error_codes::kBandRoleUnresolved )
      prepare( code.c_str(), "inspect band roles, then extract the required bands from a "
                             "richer source product",
               "rs:extract_bands", nullptr );
    else if ( code == error_codes::kInvalidRadiometry )
      prepare( code.c_str(), "bring the scene into the preferred radiometric domain",
               "rs:atmospheric_dos1", "harness.preprocess_dn_to_reflectance" );
    else if ( code == error_codes::kCrsMismatch || code == error_codes::kGridMismatch )
      prepare( code.c_str(), "reproject/align inputs onto the reference grid", "rs:align",
               nullptr );
    else if ( code == error_codes::kCalibrationMismatch )
      prepare( code.c_str(), "calibrate the SAR product into the demanded domain",
               "rs:sar_calibrate", nullptr );
    else if ( code == error_codes::kTimeOrderInvalid )
      prepare( code.c_str(), "inspect acquisition times and order the collection",
               "spatial:understand", nullptr );
    // Unknown codes intentionally yield no row — never guessed advice.
  }
  document["no_safe_preparation"] = sawUnsafe;
  return document;
}

Json::Value solutionPathsForIntent( const std::string &intent )
{
  Json::Value document( Json::objectValue );
  document["intent"] = intent;
  document["solution_paths"] = Json::Value( Json::arrayValue );

  RecipeCatalog &catalog = RecipeCatalog::instance();
  if ( !catalog.loaded() )
    catalog.reload();
  if ( !catalog.loaded() )
  {
    document["note"] = "recipe catalog unavailable in this process; "
                       "operator candidates remain authoritative";
    return document;
  }

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  std::set<std::string> servingOperators;
  for ( const std::string &operatorId : knowledge.operatorsForIntent( intent ) )
    servingOperators.insert( operatorId );

  // Deterministic order: recipe id sort, then bounded. Capabilities live on
  // the full documents, not the bounded summaries.
  std::map<std::string, Json::Value> paths;
  for ( const Json::Value &summary : catalog.listRecipes() )
  {
    const std::string recipeId = summary.get( "recipe_id", "" ).asString();
    const Json::Value doc = catalog.recipe( recipeId );
    const std::string recipeIntent = doc.get( "intent", "" ).asString();
    bool serves = recipeIntent == intent;
    if ( !serves )
    {
      for ( const Json::Value &capability :
            doc.get( "capabilities", Json::Value( Json::arrayValue ) ) )
        if ( capability.isString() && servingOperators.count( capability.asString() ) )
          serves = true;
    }
    if ( !serves )
      continue;
    Json::Value path( Json::objectValue );
    path["recipe_id"] = recipeId;
    if ( doc.isMember( "title" ) )
      path["title"] = doc["title"];
    if ( recipeIntent.size() )
      path["intent"] = recipeIntent;
    path["step_count"] = static_cast<Json::Int>(
      doc.get( "steps", Json::Value( Json::arrayValue ) ).size() );
    paths[ recipeId ] = std::move( path );
  }

  int appended = 0;
  for ( const auto &[ recipeId, path ] : paths )
  {
    document["solution_paths"].append( path );
    if ( ++appended >= 8 )
      break;
  }
  return document;
}

} // namespace sicnu::agent::harness

// ---------------------------------------------------------------------------
// harness:resolve_intent tool
// ---------------------------------------------------------------------------

namespace sicnu::agent::harness {

namespace {

using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;

/// Optional understanding gathering: `understanding` may be a pre-built
/// DatasetUnderstanding object, or a dataset reference string resolved and
/// inspected exactly like the preflight does (same facts, same sources).
Json::Value gatherUnderstanding( const Json::Value &input )
{
  if ( !input.isMember( "understanding" ) )
    return Json::Value();
  const Json::Value &supplied = input["understanding"];
  if ( supplied.isObject() )
    return supplied;
  if ( !supplied.isString() || supplied.asString().empty() )
    return Json::Value();

  HarnessError error;
  const std::optional<ResolvedDataset> resolved =
    resolveDatasetRef( QString::fromStdString( supplied.asString() ), &error );
  if ( !resolved )
    return Json::Value();

  if ( auto inspect = SpatialToolRegistry::instance().find( "spatial:raster_inspect" ) )
  {
    Json::Value inspectInput;
    inspectInput["path"] = resolved->path.toStdString();
    const SpatialToolResult result = ( *inspect )->execute( inspectInput );
    if ( result.success )
    {
      Json::Value understanding =
        sicnu::agent::contracts::datasetUnderstandingFromRasterInspect( result.output );
      understanding["modality"] = inferModality( result.output );
      return understanding;
    }
  }
  return Json::Value();
}

class ResolveIntentTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:resolve_intent"; }
    std::string displayName() const override { return "Resolve scientific intent"; }
    std::string description() const override
    {
      return "Deterministic goal classification: free-text goal -> closed scientific "
             "intent -> feasibility-ranked capability candidates against a dataset "
             "understanding. Input: {goal, understanding?} where understanding is a "
             "DatasetUnderstanding document or a dataset reference. Ambiguous or "
             "unresolvable goals return status ambiguous/unresolved with typed "
             "candidates — never a guessed intent.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "intent", "capability", "graph", "planning", "grounding" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["required"] = Json::Value( Json::arrayValue );
      schema["required"].append( "goal" );
      Json::Value props( Json::objectValue );
      Json::Value goal( Json::objectValue );
      goal["type"] = "string";
      goal["description"] = "Free-text scientific goal, e.g. 'Sentinel-2 洪水制图' or "
                            "'NDVI time series trend over the active scene'";
      props["goal"] = goal;
      Json::Value understanding( Json::objectValue );
      understanding["description"] = "DatasetUnderstanding document or a dataset "
                                     "reference (entity id / path / display name)";
      props["understanding"] = understanding;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["status"] = Json::Value( Json::objectValue );
      schema["properties"]["intent"] = Json::Value( Json::objectValue );
      schema["properties"]["candidates"] = Json::Value( Json::objectValue );
      schema["properties"]["capabilities"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string goal = input.isMember( "goal" ) && input["goal"].isString()
        ? input["goal"].asString()
        : std::string();
      if ( goal.empty() )
        return SpatialToolResult::failure( "Provide the goal text", "INVALID_PARAMETER",
                                           "validation" );

      const Json::Value understanding = gatherUnderstanding( input );
      const IntentResolution resolution = resolveGoalIntent( goal );

      Json::Value document( Json::objectValue );
      document["status"] = resolution.status;
      if ( resolution.status == "resolved" )
      {
        document["intent"] = resolution.intent;
        document["matched_terms"] = resolution.matchedTerms;
        document["capabilities"] = capabilityCandidates( resolution.intent, understanding );
        // Harness 8.0 (Area C): explainable planning surface — what facts are
        // missing, what safe preparation exists for the blockers, and which
        // recipe-level solution paths serve the intent.
        document["missing_facts"] =
          missingFactsForIntent( resolution.intent, understanding )["missing_facts"];
        document["solution_paths"] =
          solutionPathsForIntent( resolution.intent )["solution_paths"];
        Json::Value preparations( Json::arrayValue );
        for ( const Json::Value &candidate :
              document["capabilities"].get( "candidates", Json::Value( Json::arrayValue ) ) )
        {
          if ( candidate.get( "feasible", true ).asBool() )
            continue;
          for ( const Json::Value &preparation :
                preparationForWhyNot( candidate["why_not"] )["preparations"] )
            preparations.append( preparation );
        }
        document["preparations"] = preparations;
      }
      else
      {
        document["candidates"] = resolution.candidates;
        document["ambiguity"] = resolution.ambiguityError();
      }
      return SpatialToolResult::ok( document );
    }
};

} // namespace

void registerCapabilityGraphTools()
{
  static std::shared_ptr<SpatialTool> tool = std::make_shared<ResolveIntentTool>();
  SpatialToolRegistry::instance().registerTool( tool );
}

} // namespace sicnu::agent::harness
