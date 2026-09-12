// src/agent/harness/agent_plan.cpp
#include "agent_plan.h"

#include "intent_vocabulary.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <QCryptographicHash>
#include <QString>

#include <json/writer.h>

#include <algorithm>
#include <set>
#include <string>

namespace sicnu::agent::harness {

namespace {

std::string stepIdOf( const Json::Value &step )
{
  if ( step.isObject() && step.isMember( "id" ) && step["id"].isString() )
    return step["id"].asString();
  return {};
}

std::string operatorIdOf( const Json::Value &step )
{
  for ( const char *key : { "operator_id", "operatorId", "operator" } )
  {
    if ( step.isObject() && step.isMember( key ) && step[key].isString() )
      return step[key].asString();
  }
  return {};
}

bool operatorExists( const std::string &operatorId )
{
  if ( sicnu::operators::RSOperatorRegistry::instance().create( operatorId ) )
    return true;
  return sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId ) != nullptr;
}

} // namespace

bool isKnownIntent( const std::string &intent )
{
  if ( intent.empty() )
    return true;
  // Harness 4.0 vocabulary + Platform 5.0 recipe families; declared in
  // intent_vocabulary.h — the single source every mirror keys off.
  for ( const char *candidate : kIntentVocabulary )
    if ( intent == candidate )
      return true;
  return false;
}

bool readAgentPlan( const Json::Value &doc, AgentPlan &plan, HarnessError &error )
{
  if ( !doc.isObject() )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, "Plan must be a JSON object" );
    return false;
  }

  const std::string version = doc.get( "schema_version", "1.0" ).asString();
  const std::string kind = doc.get( "kind", "execution_plan" ).asString();
  if ( kind != "execution_plan" )
  {
    error = HarnessError::make( error_codes::kInvalidPlan,
                                "Plan envelope kind must be 'execution_plan'",
                                Json::Value() );
    error.details["found"] = kind;
    return false;
  }
  if ( version != "2.0" && version != "1.0" )
  {
    Json::Value details( Json::objectValue );
    details["found"] = version;
    details["supported"] = "1.0, 2.0";
    error = HarnessError::make( error_codes::kInvalidPlan, "Unsupported plan schema version",
                                details );
    return false;
  }

  plan = AgentPlan{};
  plan.raw = doc;
  plan.planId = doc.get( "plan_id", "plan-agent" ).asString();
  plan.goal = doc.get( "goal", "" ).asString();
  plan.intent = doc.get( "intent", "" ).asString();
  if ( !isKnownIntent( plan.intent ) )
  {
    Json::Value details( Json::objectValue );
    details["intent"] = plan.intent;
    details["known"] = "ndvi|evi|savi|ndre|ndwi|mndwi|ndsi|nbr|dnbr|ndbi|bsi|water|flood|"
                       "sar_water|sar_flood|sar|ship|change|sar_change|temporal|terrain|"
                       "classify|accuracy|qa|preprocess|inference|phenology";
    error = HarnessError::make( error_codes::kInvalidPlan, "Unknown intent", details );
    return false;
  }
  if ( doc.isMember( "inputs" ) && doc["inputs"].isArray() )
    plan.inputs = doc["inputs"];
  if ( doc.isMember( "steps" ) && doc["steps"].isArray() )
    plan.steps = doc["steps"];
  if ( doc.isMember( "outputs" ) && doc["outputs"].isArray() )
    plan.outputs = doc["outputs"];
  if ( doc.isMember( "verification" ) && doc["verification"].isObject() )
    plan.verification = doc["verification"];
  if ( doc.isMember( "map_output" ) )
    plan.mapOutput = doc["map_output"];
  // Harness 8.0 (Area E): identity pins + cleanup policy. Shape/vocabulary
  // is validated structurally below; identity is checked at execute time.
  if ( doc.isMember( "pins" ) )
  {
    // A malformed pins block must not silently disable the identity gate
    // (adversarial review P1): reject it instead of dropping it.
    if ( !doc["pins"].isObject() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  "pins must be an object" );
      return false;
    }
    plan.pins = doc["pins"];
  }
  if ( doc.isMember( "cleanup" ) )
  {
    if ( !doc["cleanup"].isString() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  "cleanup must be a string (keep_all|keep_outputs)" );
      return false;
    }
    plan.cleanup = doc["cleanup"].asString();
  }

  // v1 steps used "operator_id" too (makeExecutionStep), so both versions
  // read through the same accessors.
  if ( plan.steps.empty() )
  {
    error = HarnessError::makeWithAction( error_codes::kInvalidPlan, "Plan has no steps",
                                          "harness:plan", Json::Value() );
    return false;
  }
  return true;
}

std::vector<AgentPlanIssue> validateAgentPlan( const AgentPlan &plan )
{
  std::vector<AgentPlanIssue> issues;
  const auto addIssue = [ &issues ]( const std::string &code, const std::string &summary,
                                     const std::string &stepId, bool repairable,
                                     const std::string &action )
  {
    AgentPlanIssue issue;
    issue.stepId = stepId;
    issue.repairable = repairable;
    Json::Value details( Json::objectValue );
    if ( !stepId.empty() )
      details["step_id"] = stepId;
    issue.error = action.empty()
                    ? HarnessError::make( code, summary )
                    : HarnessError::makeWithAction( code, summary, action, details );
    issues.push_back( std::move( issue ) );
  };

  // Harness 8.0 (adversarial review P1): duplicate input slot names make
  // identity pins ambiguous — a pinned slot must match exactly one input.
  std::set<std::string> inputNames;
  for ( const Json::Value &input : plan.inputs )
  {
    if ( !input.isObject() )
      continue;
    const std::string name = input.get( "name", "" ).asString();
    if ( !name.empty() && !inputNames.insert( name ).second )
      addIssue( error_codes::kInvalidPlan,
                "Duplicate input slot name: " + name + " — pins cannot gate it",
                "", true, "rename_input" );
  }

  std::set<std::string> ids;
  for ( const Json::Value &step : plan.steps )
  {
    const std::string id = stepIdOf( step );
    if ( id.empty() )
    {
      addIssue( error_codes::kInvalidPlan, "Every step needs a string 'id'", "", false, "" );
      continue;
    }
    if ( !ids.insert( id ).second )
      addIssue( error_codes::kInvalidPlan, "Duplicate step id: " + id, id, true,
                "rename_step" );

    const std::string operatorId = operatorIdOf( step );
    if ( operatorId.empty() )
      addIssue( error_codes::kInvalidPlan, "Step is missing 'operator_id'", id, true,
                "set_operator" );
    else if ( !operatorExists( operatorId ) )
      addIssue( error_codes::kInvalidPlan, "Unknown operator id: " + operatorId, id, true,
                "search_capabilities" );

    for ( const Json::Value &input : step.get( "inputs", Json::Value( Json::arrayValue ) ) )
    {
      const std::string upstream = input.get( "step", "" ).asString();
      if ( upstream.empty() || !std::any_of(
             plan.steps.begin(), plan.steps.end(),
             [ &upstream ]( const Json::Value &s ) { return stepIdOf( s ) == upstream; } ) )
        addIssue( error_codes::kInvalidPlan,
                  "Step input references unknown upstream step: " + upstream, id, true,
                  "fix_wiring" );
    }

    const std::string verification =
      step.get( "verification", "" ).asString();
    if ( !verification.empty() && verification != "raster" && verification != "vector" &&
         verification != "skip" )
      addIssue( error_codes::kInvalidPlan,
                "verification must be raster|vector|skip in step " + id, id, true,
                "set_verification" );
  }

  for ( const Json::Value &output : plan.outputs )
  {
    const std::string from = output.get( "from_step", "" ).asString();
    if ( !from.empty() && !std::any_of(
           plan.steps.begin(), plan.steps.end(),
           [ &from ]( const Json::Value &s ) { return stepIdOf( s ) == from; } ) )
      addIssue( error_codes::kInvalidPlan,
                "Declared output references unknown step: " + from, from, true,
                "fix_outputs" );
  }

  // Harness 8.0 (Area E): closed step-role vocabulary (explainability only —
  // roles never change execution semantics).
  for ( const Json::Value &step : plan.steps )
  {
    const std::string role = step.get( "role", "" ).asString();
    if ( role.empty() )
      continue;
    static const char *const kRoles[] = { "preparation", "analysis", "postprocess",
                                          "verification" };
    if ( !std::any_of( std::begin( kRoles ), std::end( kRoles ),
                       [ &role ]( const char *candidate ) { return role == candidate; } ) )
      addIssue( error_codes::kInvalidPlan,
                "step role must be preparation|analysis|postprocess|verification: " + role,
                stepIdOf( step ), true, "set_role" );
  }

  // Harness 8.0 (Area E): pins shape + slot membership. Identity against the
  // resolved dataset is checked at execute time (needs entity resolution).
  if ( plan.pins.isObject() && plan.pins.isMember( "datasets" ) )
  {
    const Json::Value &datasets = plan.pins["datasets"];
    if ( !datasets.isObject() )
    {
      addIssue( error_codes::kInvalidPlan, "pins.datasets must be an object", "", false, "" );
    }
    else
    {
      for ( const std::string &slot : datasets.getMemberNames() )
      {
        const Json::Value &pin = datasets[ slot ];
        const bool slotDeclared = std::any_of(
          plan.inputs.begin(), plan.inputs.end(),
          [ &slot ]( const Json::Value &input ) {
            return input.isObject() && input.get( "name", "" ).asString() == slot;
          } );
        if ( !slotDeclared )
        {
          addIssue( error_codes::kInvalidPlan,
                    "pins.datasets names undeclared input slot: " + slot, "", true,
                    "fix_pins" );
          continue;
        }
        if ( !pin.isObject() ||
             !( pin.isMember( "asset_entity_id" ) || pin.isMember( "asset_id" ) ||
                pin.isMember( "path" ) ) )
        {
          addIssue( error_codes::kInvalidPlan,
                    "pin for slot '" + slot + "' needs asset_entity_id, asset_id, or path",
                    "", true, "fix_pins" );
        }
      }
    }
  }
  if ( plan.pins.isObject() && plan.pins.isMember( "model" ) &&
       !plan.pins["model"].isString() )
    addIssue( error_codes::kInvalidPlan, "pins.model must be a string ('<id>' or '<id>@<version>')",
              "", true, "fix_pins" );

  // Cleanup policy vocabulary.
  if ( !plan.cleanup.empty() && plan.cleanup != "keep_all" && plan.cleanup != "keep_outputs" )
    addIssue( error_codes::kInvalidPlan, "cleanup must be keep_all|keep_outputs", "", true,
              "set_cleanup" );

  return issues;
}

std::string compilePlanToWorkflowJson( const AgentPlan &plan, HarnessError &error )
{
  const std::vector<AgentPlanIssue> issues = validateAgentPlan( plan );
  if ( !issues.empty() )
  {
    Json::Value details( Json::objectValue );
    Json::Value codes( Json::arrayValue );
    for ( const AgentPlanIssue &issue : issues )
      codes.append( issue.error.code );
    details["issues"] = codes;
    error = HarnessError::make( error_codes::kInvalidPlan,
                                "Plan failed structural validation", details );
    return {};
  }

  Json::Value def( Json::objectValue );
  def["id"] = plan.planId.empty() ? "agent_plan" : plan.planId;
  def["title"] = plan.goal.empty() ? "Agent Plan" : plan.goal;
  def["workspaceKind"] = "agent";

  Json::Value steps( Json::arrayValue );
  for ( const Json::Value &step : plan.steps )
  {
    Json::Value out( Json::objectValue );
    out["id"] = stepIdOf( step );
    out["title"] = step.get( "title", operatorIdOf( step ) ).asString();
    out["operatorId"] = operatorIdOf( step );
    out["params"] = step.isMember( "params" ) && step["params"].isObject()
                      ? step["params"]
                      : Json::Value( Json::objectValue );
    if ( step.isMember( "verification" ) && step["verification"].isString() )
      out["verificationPolicy"] = step["verification"];

    Json::Value inputs( Json::arrayValue );
    for ( const Json::Value &input : step.get( "inputs", Json::Value( Json::arrayValue ) ) )
    {
      Json::Value conn( Json::objectValue );
      conn["fromStepId"] = input.get( "step", "" ).asString();
      conn["fromPort"] = input.get( "port", "output" ).asString();
      conn["toPort"] = input.get( "to_port", "input" ).asString();
      inputs.append( conn );
      // Keep params and wiring consistent: the placeholder makes the data
      // flow explicit for downstream parameter resolution.
      const std::string toPort = conn["toPort"].asString();
      if ( !out["params"].isMember( toPort ) )
        out["params"][toPort] = "$" + conn["fromStepId"].asString() + "." +
                                conn["fromPort"].asString();
    }
    if ( !inputs.empty() )
      out["inputs"] = inputs;

    steps.append( out );
  }
  def["steps"] = steps;

  // Harness 8.0 (Area E): run policies ride as workflow metadata. The engine
  // parser ignores unknown root keys today (verified), so this is additive;
  // execution-plane consumption is a recorded cross-track follow-up.
  Json::Value metadata( Json::objectValue );
  metadata["plan_fingerprint"] = planFingerprint( plan );
  if ( !plan.intent.empty() )
    metadata["intent"] = plan.intent;
  metadata["cleanup"] = plan.cleanup.empty() ? "keep_all" : plan.cleanup;
  if ( plan.pins.isObject() && !plan.pins.empty() )
    metadata["pins"] = plan.pins;
  def["metadata"] = metadata;

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString( builder, def );
}

std::string planFingerprint( const AgentPlan &plan )
{
  // Canonical scientific content only: identity/plan_id/timestamps and
  // estimates are excluded so identical science yields identical bytes.
  // Json::Value object members iterate in sorted key order, so the compact
  // serialization is canonical without extra work.
  Json::Value content( Json::objectValue );
  content["intent"] = plan.intent;
  content["inputs"] = plan.inputs;
  Json::Value steps( Json::arrayValue );
  for ( const Json::Value &step : plan.steps )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = stepIdOf( step );
    entry["operator_id"] = operatorIdOf( step );
    entry["params"] = step.get( "params", Json::Value( Json::objectValue ) );
    entry["inputs"] = step.get( "inputs", Json::Value( Json::arrayValue ) );
    if ( step.isMember( "verification" ) )
      entry["verification"] = step["verification"];
    if ( step.isMember( "role" ) )
      entry["role"] = step["role"];
    steps.append( entry );
  }
  content["steps"] = steps;
  content["outputs"] = plan.outputs;
  content["verification"] = plan.verification;
  // Identity/policy content (adversarial review P3): two plans that differ
  // only in their pins or cleanup policy are different commitments.
  if ( plan.pins.isObject() && !plan.pins.empty() )
    content["pins"] = plan.pins;
  if ( !plan.cleanup.empty() && plan.cleanup != "keep_all" )
    content["cleanup"] = plan.cleanup;

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  const std::string serialized = Json::writeString( builder, content );
  const QByteArray digest = QCryptographicHash::hash(
    QByteArray::fromStdString( serialized ), QCryptographicHash::Sha256 );
  return QString::fromLatin1( digest.left( 16 ).toHex() ).toStdString();
}

Json::Value estimatePlanResources( const AgentPlan &plan )
{
  Json::Value perStep( Json::objectValue );
  long long totalRamMb = 0;
  for ( const Json::Value &step : plan.steps )
  {
    const std::string id = stepIdOf( step );
    const std::string operatorId = operatorIdOf( step );
    long long ramMb = 0;
    if ( auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId ) )
    {
      // Input-dependent estimate first (Phase 8), static declaration second.
      Json::Value estimate = op->estimateExecution(
        step.isMember( "params" ) ? step["params"] : Json::Value( Json::objectValue ) );
      if ( !estimate.isObject() || !estimate.isMember( "estimatedRamBytes" ) )
        estimate = op->executionEstimate();
      if ( estimate.isObject() && estimate.isMember( "estimatedRamBytes" ) &&
           estimate["estimatedRamBytes"].isNumeric() )
        ramMb = estimate["estimatedRamBytes"].asInt64() / ( 1024 * 1024 );
    }
    perStep[id] = static_cast<Json::Int64>( ramMb );
    totalRamMb += ramMb;
  }
  Json::Value estimates( Json::objectValue );
  estimates["total_ram_mb"] = static_cast<Json::Int64>( totalRamMb );
  estimates["per_step"] = perStep;
  estimates["declared"] = totalRamMb > 0;
  return estimates;
}

Json::Value planSummary( const AgentPlan &plan )
{
  Json::Value summary( Json::objectValue );
  summary["plan_id"] = plan.planId;
  if ( !plan.goal.empty() )
    summary["goal"] = plan.goal;
  if ( !plan.intent.empty() )
    summary["intent"] = plan.intent;
  summary["plan_fingerprint"] = planFingerprint( plan );
  summary["cleanup"] = plan.cleanup.empty() ? "keep_all" : plan.cleanup;
  Json::Value steps( Json::arrayValue );
  for ( const Json::Value &step : plan.steps )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = stepIdOf( step );
    entry["operator_id"] = operatorIdOf( step );
    if ( step.isMember( "role" ) && step["role"].isString() )
      entry["role"] = step["role"];
    steps.append( entry );
  }
  summary["steps"] = steps;
  summary["outputs"] = plan.outputs;
  return summary;
}

} // namespace sicnu::agent::harness
