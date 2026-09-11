// tests/test_harness9_contracts.cpp
//
// Harness 9.0 contract floors:
//   * #881 — every suggested action the harness can emit resolves to a real
//     surface: a registered SpatialTool id or a registered workbench command.
//     The workbench half is cross-checked against the authoritative command
//     definitions source (src/app/workbench/command_defs.cpp), not a copy.
//   * #867 — recipe gate truth semantics (numeric/list/object bindings open
//     gates; bool semantics unchanged), numeric substitution fidelity, and
//     transitive degradation: a fallback-less step on a dropped dependency is
//     dropped (never an orphan consuming an intermediate nobody produces).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/harness/harness_actions.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/context_ledger.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/harness/scientific_preflight.h"
#include "agent/mapspec/mapspec_conditions.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::agent::harness;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;

namespace {

bool writeTextFile( const QString &path, const std::string &content )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  return file.write( content.data(), static_cast<qint64>( content.size() ) ) >= 0;
}

/// The authoritative workbench command ids, parsed from the command
/// definition source (RS_CMD( d, "<id>", ...) rows). Reading the source keeps
/// this a cross-check — the app registry stays the only registry.
std::set<std::string> registeredWorkbenchCommands()
{
  std::set<std::string> ids;
  QFile source( QString::fromStdString(
    std::string( CMAKE_SOURCE_DIR ) + "/src/app/workbench/command_defs.cpp" ) );
  if ( !source.open( QIODevice::ReadOnly ) )
    return ids;
  QTextStream stream( &source );
  const QString text = stream.readAll();
  static const QRegularExpression pattern(
    R"rx(RS_CMD\(\s*\w+\s*,\s*"([^"]+)")rx" );
  QRegularExpressionMatchIterator it = pattern.globalMatch( text );
  while ( it.hasNext() )
  {
    const auto match = it.next();
    ids.insert( match.captured( 1 ).toStdString() );
  }
  return ids;
}

std::set<std::string> registeredToolIds()
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  std::set<std::string> ids;
  // registerBuiltinTools registers the platform surface; the harness recipe
  // of the registry is the authoritative lookup for one id.
  for ( const char *probe : { "spatial:understand", "spatial:raster_inspect",
                              "spatial:select_model", "spatial:search_capabilities",
                              "spatial:workspace_summary", "project:search",
                              "temporal:preflight_collection",
                              "harness:search_recipes", "harness:plan", "harness:preflight",
                              "harness:run_status" } )
  {
    if ( SpatialToolRegistry::instance().find( probe ).has_value() )
      ids.insert( probe );
  }
  return ids;
}

} // namespace

// ---------------------------------------------------------------------------
// #881 — closed suggested-action vocabulary resolves against live surfaces
// ---------------------------------------------------------------------------

TEST_CASE( "harness action table entries resolve to registered surfaces",
           "[harness9][actions][drift]" )
{
  const std::set<std::string> tools = registeredToolIds();
  const std::set<std::string> commands = registeredWorkbenchCommands();
  INFO( "workbench command ids parsed from command_defs.cpp: " << commands.size() );
  REQUIRE( !commands.empty() );
  REQUIRE( commands.count( "workbench.datasetExperiment" ) == 1 );

  const auto &table = harnessActionTable();
  REQUIRE( table.size() >= 12 );
  for ( const HarnessActionSpec &spec : table )
  {
    INFO( "action key: " << spec.key );
    REQUIRE( !spec.key.empty() );
    // Kind is a closed vocabulary; tool/workbench entries must reference
    // REAL registered surfaces (the #881 contract).
    REQUIRE( ( spec.kind == "tool" || spec.kind == "workbench" || spec.kind == "author" ) );
    const bool toolOk = spec.tool.empty() || tools.count( spec.tool ) == 1;
    const bool commandOk =
      spec.workbenchCommand.empty() || commands.count( spec.workbenchCommand ) == 1;
    INFO( "tool: " << spec.tool << " command: " << spec.workbenchCommand );
    REQUIRE( toolOk );
    REQUIRE( commandOk );
    // A non-author entry must carry at least one resolvable surface.
    if ( spec.kind != "author" )
      REQUIRE( ( !spec.tool.empty() || !spec.workbenchCommand.empty() ) );
    // Default arguments, when declared, must parse as a JSON object.
    if ( !spec.defaultArgumentsJson.empty() )
    {
      Json::Value parsed;
      Json::CharReaderBuilder builder;
      std::string errors;
      std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
      const bool ok = reader->parse( spec.defaultArgumentsJson.data(),
                                     spec.defaultArgumentsJson.data() +
                                       spec.defaultArgumentsJson.size(),
                                     &parsed, &errors );
      REQUIRE( ok );
      REQUIRE( parsed.isObject() );
    }
  }
}

TEST_CASE( "resolvedSuggestedAction issues the resolution contract",
           "[harness9][actions]" )
{
  const Json::Value known = resolvedSuggestedAction( "check_dataset", Json::Value() );
  REQUIRE( known["action"].asString() == "check_dataset" );
  REQUIRE( known["resolved"].asBool() );
  REQUIRE( known["tool"].asString() == "spatial:understand" );
  REQUIRE( known["workbench_command"].asString() == "workbench.datasetExperiment" );
  REQUIRE( known["arguments"].isObject() );

  // Preparation-class actions pre-fill the recipe-search arguments so the
  // agent can act without inventing a query.
  const Json::Value prep = resolvedSuggestedAction( "calibrate_consistently", Json::Value() );
  REQUIRE( prep["tool"].asString() == "harness:search_recipes" );
  REQUIRE( prep["arguments"]["query"].asString().find( "calibration" ) != std::string::npos );

  // Caller-provided arguments are never overwritten.
  Json::Value args;
  args["query"] = "custom";
  const Json::Value custom = resolvedSuggestedAction( "calibrate_consistently", args );
  REQUIRE( custom["arguments"]["query"].asString() == "custom" );

  // Unknown keys stay visible as unresolved drift — never silently invented.
  const Json::Value unknown = resolvedSuggestedAction( "teleport_dataset", Json::Value() );
  REQUIRE( unknown["resolved"].asBool() == false );
  REQUIRE( unknown["action"].asString() == "teleport_dataset" );

  // The canonical error helper routes through the same vocabulary.
  const HarnessError error = HarnessError::makeWithAction(
    error_codes::kModelNotReady, "model missing", "select_model", Json::Value() );
  REQUIRE( error.suggestedActions.isArray() );
  REQUIRE( error.suggestedActions[0]["resolved"].asBool() );
  REQUIRE( error.suggestedActions[0]["tool"].asString() == "spatial:select_model" );
}

TEST_CASE( "preflight blockers carry resolvable suggested actions (#881)",
           "[harness9][actions][preflight]" )
{
  // An unresolvable input trips the shared rule pack; its action key is the
  // historical dotted spelling and must resolve to the understand tool.
  Json::Value refs( Json::arrayValue );
  Json::Value entry( Json::objectValue );
  entry["name"] = "primary";
  entry["ref"] = "/definitely/not/present.tif";
  refs.append( entry );
  const PreflightOutcome outcome = preflightIntent( "ndvi", refs );
  REQUIRE( outcome.verdict == "blocked" );
  REQUIRE( outcome.issues.isArray() );
  REQUIRE( outcome.issues.size() >= 1 );
  bool sawResolvedAction = false;
  for ( const Json::Value &issue : outcome.issues )
  {
    if ( !issue.isMember( "suggested_action" ) )
      continue;
    const Json::Value &action = issue["suggested_action"];
    sawResolvedAction = true;
    INFO( "action: " << action["action"].asString() );
    REQUIRE( action["resolved"].asBool() );
    REQUIRE( !action["tool"].asString().empty() );
  }
  REQUIRE( sawResolvedAction );
}

// ---------------------------------------------------------------------------
// #867 — recipe gate truth semantics + substitution fidelity
// ---------------------------------------------------------------------------

namespace {

/// Loads an inline recipe and instantiates it with the given bindings.
Json::Value instantiate( const QTemporaryDir &dir, const std::string &recipeJson,
                         const Json::Value &bindings, HarnessError &error )
{
  writeTextFile( dir.filePath( QStringLiteral( "recipe_under_test.json" ) ), recipeJson );
  RecipeCatalog::instance().setDirectory( dir.path().toStdString() );
  RecipeCatalog::instance().reload();
  return RecipeCatalog::instance().instantiateRecipe( "test.nine", bindings, error );
}

Json::Value stepsById( const Json::Value &plan )
{
  Json::Value byId( Json::objectValue );
  for ( const auto &step : plan["steps"] )
    byId[step["id"].asString()] = step;
  return byId;
}

void restoreCatalog()
{
  RecipeCatalog::instance().setDirectory(
    std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" );
  RecipeCatalog::instance().reload();
}

} // namespace

TEST_CASE( "numeric parameter bindings open when_param gates (#867)",
           "[harness9][recipe][gate]" )
{
  QTemporaryDir dir;
  QTemporaryDir dataDir;
  REQUIRE( writeTextFile( dataDir.filePath( QStringLiteral( "input.tif" ) ), "x" ) );

  const std::string recipeJson = R"JSON({
    "schema_version": "2.0",
    "kind": "harness_recipe",
    "recipe_id": "test.nine",
    "title": "gate truth test",
    "intent": "test",
    "slots": [ { "name": "primary", "required": true } ],
    "outputs": [ { "name": "out", "from_step": "threshold" } ],
    "steps": [
      { "id": "threshold", "operator_id": "op.threshold",
        "params": { "input": "$primary.path", "mode": "manual",
                    "threshold": "$params.threshold" },
        "params_when_skipped": { "input": "$primary.path", "mode": "statistical" },
        "when_param": "threshold" }
    ]
  })JSON";

  // A bound numeric threshold opens the gate AND substitutes faithfully —
  // the pre-9.0 behavior silently degraded to the statistical fallback and,
  // when substituted, leaked a 17-digit binary expansion.
  Json::Value bindings( Json::objectValue );
  bindings["slots"]["primary"] = dataDir.filePath( QStringLiteral( "input.tif" ) ).toStdString();
  bindings["params"]["threshold"] = 0.4;
  bindings["output_dir"] = dir.path().toStdString();
  HarnessError error;
  const Json::Value plan = instantiate( dir, recipeJson, bindings, error );
  REQUIRE( error.code.empty() );
  const Json::Value byId = stepsById( plan );
  REQUIRE( byId.isMember( "threshold" ) );
  CHECK( byId["threshold"]["params"]["mode"].asString() == "manual" );
  CHECK( byId["threshold"]["params"]["threshold"].asString() == "0.4" );
  CHECK( !plan.isMember( "degradations" ) );

  // A bound zero threshold is a bound VALUE (manual thresholding at 0), not
  // an unbound parameter.
  Json::Value zeroBindings = bindings;
  zeroBindings["params"]["threshold"] = 0;
  HarnessError zeroError;
  const Json::Value zeroPlan = instantiate( dir, recipeJson, zeroBindings, zeroError );
  REQUIRE( zeroError.code.empty() );
  const Json::Value zeroById = stepsById( zeroPlan );
  REQUIRE( zeroById.isMember( "threshold" ) );
  CHECK( zeroById["threshold"]["params"]["threshold"].asString() == "0" );

  // Bool semantics are unchanged: explicit false closes the gate into the
  // fallback template; true keeps the normal one.
  Json::Value boolBindings = bindings;
  boolBindings["params"]["threshold"] = false;
  HarnessError boolError;
  const Json::Value boolPlan = instantiate( dir, recipeJson, boolBindings, boolError );
  REQUIRE( boolError.code.empty() );
  const Json::Value boolById = stepsById( boolPlan );
  REQUIRE( boolById.isMember( "threshold" ) );
  CHECK( boolById["threshold"]["params"]["mode"].asString() == "statistical" );

  // Unbound: fallback template + an honest degradation record.
  Json::Value bare = bindings;
  bare.removeMember( "params" );
  HarnessError bareError;
  const Json::Value barePlan = instantiate( dir, recipeJson, bare, bareError );
  REQUIRE( bareError.code.empty() );
  const Json::Value bareById = stepsById( barePlan );
  REQUIRE( bareById.isMember( "threshold" ) );
  CHECK( bareById["threshold"]["params"]["mode"].asString() == "statistical" );
  REQUIRE( barePlan["degradations"].isArray() );
  REQUIRE( barePlan["degradations"].size() == 1 );
  CHECK( barePlan["degradations"][0]["step"].asString() == "threshold" );
  CHECK( barePlan["degradations"][0]["mode"].asString() == "skipped" );
  CHECK( barePlan["degradations"][0]["reason"].asString() == "gate_closed" );

  restoreCatalog();
}

TEST_CASE( "fallback-less steps on degraded dependencies drop, never orphan (#867)",
           "[harness9][recipe][degradation]" )
{
  QTemporaryDir dir;
  QTemporaryDir dataDir;
  REQUIRE( writeTextFile( dataDir.filePath( QStringLiteral( "input.tif" ) ), "x" ) );

  // gated_step (when_param "enhance", no fallback) produces an intermediate;
  // nofallback_consumer consumes it in its NORMAL params without a fallback;
  // fallback_consumer consumes it with one; skipped_path_consumer consumes it
  // only in its degraded template (its normal path never touches it).
  const std::string recipeJson = R"JSON({
    "schema_version": "2.0",
    "kind": "harness_recipe",
    "recipe_id": "test.nine",
    "title": "orphan test",
    "intent": "test",
    "slots": [ { "name": "primary", "required": true } ],
    "outputs": [],
    "steps": [
      { "id": "gated_step", "operator_id": "op.enhance", "when_param": "enhance",
        "params": { "input": "$primary.path", "output": "$outputs.enhanced" } },
      { "id": "nofallback_consumer", "operator_id": "op.analyze",
        "inputs": [ { "step": "gated_step" } ],
        "params": { "input": "$outputs.enhanced", "output": "$outputs.analysis" } },
      { "id": "fallback_consumer", "operator_id": "op.analyze",
        "inputs": [ { "step": "gated_step" } ],
        "params": { "input": "$outputs.enhanced", "output": "$outputs.alt" },
        "params_when_skipped": { "input": "$primary.path", "output": "$outputs.alt" } },
      { "id": "skipped_path_consumer", "operator_id": "op.report",
        "params": { "input": "$primary.path", "output": "$outputs.report" },
        "params_when_skipped": { "input": "$outputs.enhanced", "output": "$outputs.report" } }
    ]
  })JSON";

  Json::Value bindings( Json::objectValue );
  bindings["slots"]["primary"] = dataDir.filePath( QStringLiteral( "input.tif" ) ).toStdString();
  bindings["output_dir"] = dir.path().toStdString();
  // "enhance" stays unbound -> gated_step drops.
  HarnessError error;
  const Json::Value plan = instantiate( dir, recipeJson, bindings, error );
  REQUIRE( error.code.empty() );
  const Json::Value byId = stepsById( plan );

  // The gated producer drops.
  CHECK( !byId.isMember( "gated_step" ) );
  // Pre-9.0 this step SURVIVED with stripped wiring, consuming an
  // intermediate no surviving step would ever produce — an orphan.
  CHECK( !byId.isMember( "nofallback_consumer" ) );
  // A consumer WITH a fallback runs the degraded template.
  REQUIRE( byId.isMember( "fallback_consumer" ) );
  CHECK( byId["fallback_consumer"]["params"]["input"].asString()
           .find( "input.tif" ) != std::string::npos );
  // A consumer whose NORMAL path never touches the dropped producer keeps
  // running its normal template (branch independence, #784 principle).
  REQUIRE( byId.isMember( "skipped_path_consumer" ) );
  CHECK( byId["skipped_path_consumer"]["params"]["input"].asString()
           .find( "input.tif" ) != std::string::npos );

  // No emitted step references a dropped one, and no output survives from a
  // dropped producer.
  for ( const auto &step : plan["steps"] )
  {
    if ( !step.isMember( "inputs" ) )
      continue;
    for ( const auto &conn : step["inputs"] )
      CHECK( conn["step"].asString() != "gated_step" );
  }

  // Degradation record explains every non-normal step exactly once.
  REQUIRE( plan["degradations"].isArray() );
  std::map<std::string, std::string> degradationById;
  for ( const auto &degradation : plan["degradations"] )
    degradationById[degradation["step"].asString()] = degradation["mode"].asString();
  REQUIRE( degradationById.size() == 3 );
  CHECK( degradationById["gated_step"] == "dropped" );
  CHECK( degradationById["nofallback_consumer"] == "dropped" );
  CHECK( degradationById["fallback_consumer"] == "skipped" );
  CHECK( degradationById.count( "skipped_path_consumer" ) == 0 );

  // With the gate bound, nothing degrades and no record is emitted.
  bindings["params"]["enhance"] = true;
  HarnessError openError;
  const Json::Value openPlan = instantiate( dir, recipeJson, bindings, openError );
  REQUIRE( openError.code.empty() );
  CHECK( stepsById( openPlan ).size() == 4 );
  CHECK( !openPlan.isMember( "degradations" ) );

  restoreCatalog();
}

// ---------------------------------------------------------------------------
// #866 / #877 — MapSpec condition semantics (presence guards + NaN)
// ---------------------------------------------------------------------------

TEST_CASE( "mapspec conditions: has(x) is a first-class operand (#866)",
           "[harness9][mapspec][conditions]" )
{
  using sicnu::agent::mapspec::evaluateCondition;
  Json::Value context;
  context["present"] = "yes";

  // The guard idiom: comparing a MISSING path's presence to false must
  // evaluate true — pre-fix it errored with "unknown context path".
  bool value = false;
  std::string error;
  const bool ok = evaluateCondition( "has(missing) == false", context, &value, &error );
  REQUIRE( ok );
  INFO( "error: " << error );
  CHECK( value );

  value = false;
  error.clear();
  REQUIRE( evaluateCondition( "has(present) == false", context, &value, &error ) );
  CHECK( !value );

  value = false;
  REQUIRE( evaluateCondition( "has(present) == true", context, &value, &error ) );
  CHECK( value );

  // The presence-guard idiom from #804 keeps working alongside.
  value = false;
  REQUIRE( evaluateCondition( "has(missing) or has(present)", context, &value, &error ) );
  CHECK( value );
}

TEST_CASE( "mapspec conditions: NaN is unordered, never equal (#877)",
           "[harness9][mapspec][conditions]" )
{
  using sicnu::agent::mapspec::evaluateCondition;
  Json::Value context;
  context["value"] = std::numeric_limits<double>::quiet_NaN();

  // Pre-fix, the NaN double fell into the ordering fallthrough and compared
  // EQUAL to everything: `value == 0` gated content onto corrupt data.
  bool value = true;
  std::string error;
  REQUIRE( evaluateCondition( "value == 0", context, &value, &error ) );
  INFO( "error: " << error );
  CHECK( !value );

  value = false;
  REQUIRE( evaluateCondition( "value != 0", context, &value, &error ) );
  CHECK( value );

  value = true;
  REQUIRE( evaluateCondition( "value < 1", context, &value, &error ) );
  CHECK( !value );

  // Pre-fix `value >= 0` was TRUE for NaN (ordering 0 satisfied >=).
  value = true;
  REQUIRE( evaluateCondition( "value >= 0", context, &value, &error ) );
  CHECK( !value );

  // Real (non-NaN) comparisons keep their semantics.
  Json::Value sane;
  sane["value"] = 0.5;
  value = false;
  REQUIRE( evaluateCondition( "value >= 0.5", sane, &value, &error ) );
  CHECK( value );
  value = true;
  REQUIRE( evaluateCondition( "value == 0.5", sane, &value, &error ) );
  CHECK( value );
}

TEST_CASE( "diagnose_run fails typed on unknown runs and never invents repairs",
           "[harness9][diagnose]" )
{
  auto tool = SpatialToolRegistry::instance().find( "harness:diagnose_run" );
  REQUIRE( tool.has_value() );

  Json::Value input;
  input["run_id"] = "no-such-run";
  const SpatialToolResult result = ( *tool )->execute( input );
  CHECK( !result.success );
  CHECK( result.errorCode == "WORKFLOW_NOT_FOUND" );

  Json::Value missing;
  const SpatialToolResult invalid = ( *tool )->execute( missing );
  CHECK( !invalid.success );
  CHECK( invalid.errorCode == "INVALID_PARAMETER" );
}

// ---------------------------------------------------------------------------
// Harness 9.0 M1/M2/M6 — typed context facts, intent documents, run summaries
// ---------------------------------------------------------------------------

TEST_CASE( "typed intent documents expose the planning contract (M2)",
           "[harness9][intent]" )
{
  const Json::Value ndvi = typedIntentDocument( "ndvi" );
  REQUIRE( ndvi.isObject() );
  CHECK( ndvi["schema"].asString() == "harness.intent/1.0" );
  CHECK( ndvi["intent"].asString() == "ndvi" );
  CHECK( ndvi["pack"].asString() == "band_ratio" );

  // Required facts name the band roles the rule pack enforces.
  bool sawRed = false;
  bool sawNir = false;
  for ( const Json::Value &fact : ndvi["required_facts"] )
  {
    sawRed = sawRed || fact["fact"].asString() == "band_role:red";
    sawNir = sawNir || fact["fact"].asString() == "band_role:nir";
  }
  CHECK( sawRed );
  CHECK( sawNir );

  // Quality expectations derive from the serving capability knowledge —
  // band-ratio intents declare finite/nodata expectations.
  REQUIRE( ndvi["quality_expectations"].isObject() );
  bool finiteCheck = false;
  for ( const Json::Value &check : ndvi["quality_expectations"]["checks"] )
    finiteCheck = finiteCheck || check.asString() == "finite_fraction";
  CHECK( finiteCheck );
  CHECK( ndvi["quality_expectations"]["min_finite_fraction"].asDouble() > 0.0 );

  // Pair intents declare the input_pair requirement.
  const Json::Value change = typedIntentDocument( "change" );
  REQUIRE( change.isObject() );
  bool sawPair = false;
  for ( const Json::Value &fact : change["required_facts"] )
    sawPair = sawPair || fact["fact"].asString() == "input_pair";
  CHECK( sawPair );
  CHECK( change["requires_pair"].asBool() );

  // Unknown intents stay null — the vocabulary is closed.
  CHECK( typedIntentDocument( "warp_speed" ).isNull() );
}

TEST_CASE( "run summaries are bounded by count and token budget (M6)",
           "[harness9][context][ledger]" )
{
  ContextLedger &ledger = ContextLedger::instance();
  const int before = ledger.runSummaries().size();

  Json::Value summary;
  summary["verdict"] = "PASS";
  summary["intent"] = "ndvi";
  ledger.recordRunSummary( "run-summary-test-a", summary, 10 );
  ledger.recordRunSummary( "run-summary-test-b", summary, 10 );
  ledger.recordRunSummary( "run-summary-test-a", summary, 10 ); // replace in place

  const Json::Value summaries = ledger.runSummaries();
  REQUIRE( summaries.size() == static_cast<Json::ArrayIndex>( before + 2 ) );
  // Newest first: re-recording "a" made it the most recent entry.
  CHECK( summaries[0]["run_id"].asString() == "run-summary-test-a" );
  CHECK( summaries[1]["run_id"].asString() == "run-summary-test-b" );
  // The token meter reflects the store.
  CHECK( ledger.runSummaryTokens() >= 20 );

  // A huge summary is backstopped by the count bound, never dropped silently
  // below one entry; re-recording keeps one row per run id.
  Json::Value big = summary;
  for ( int i = 0; i < 64; ++i )
    big["filler"] = std::string( 512, 'x' );
  ledger.recordRunSummary( "run-summary-test-big", big, 4000 );

  const Json::Value bounded = ledger.runSummaries();
  CHECK( static_cast<int>( bounded.size() ) <= ContextLedger::kMaxRunSummaries );
  CHECK( ledger.runSummaryTokens() <=
         ContextLedger::kRunSummaryTokenBudget + 4000 ); // backstop allowance
}

