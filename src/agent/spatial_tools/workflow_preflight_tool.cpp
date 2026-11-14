// src/agent/spatial_tools/workflow_preflight_tool.cpp
#include "workflow_preflight_tool.h"

#include "../contracts/spatial_contracts.h"
#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/task_center.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_types.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {

using namespace sicnu::agent::contracts;

namespace {

// asInt64() throws outside int64, including the double -2^63. In-range
// fractions still truncate toward zero.
bool jsonInt64IfFits( const Json::Value &value, Json::Int64 &out )
{
  if ( value.isInt64() )
  {
    out = value.asInt64();
    return true;
  }
  if ( !value.isNumeric() )
    return false;
  const double number = value.asDouble();
  // double(INT64_MAX) rounds to 2^63, which is not an int64.
  if ( !std::isfinite( number ) ||
       number <= static_cast<double>( std::numeric_limits<Json::Int64>::min() ) ||
       number >= static_cast<double>( std::numeric_limits<Json::Int64>::max() ) )
    return false;
  out = static_cast<Json::Int64>( number );
  return true;
}

/// Existence of an executable implementation for a step's operator id:
/// RS operators (rs:*), then atomic/provider algorithms (gdal:/otb:/native:).
bool operatorExists( const std::string &operatorId )
{
  if ( sicnu::operators::RSOperatorRegistry::instance().hasOperator( operatorId ) )
    return true;
  return sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId ) != nullptr;
}

/// Parameter schema for an operator id (operators first, then atomic
/// algorithms). Null object when the implementation declares no schema.
Json::Value operatorSchema( const std::string &operatorId )
{
  if ( auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId ) )
    return op->schema();
  if ( const auto adapter =
         sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId ) )
    return adapter->descriptor().toInputSchema();
  return Json::Value();
}

/// swarm-review AG-C-9: parameter keys whose role is a filesystem path (the
/// existence check below is only meaningful for those). Keys without a path
/// role — format strings, units, CRS ids — are never existence-checked, so
/// values like "image/tiff" or "deg/sec" can no longer produce a blocking
/// WF_INPUT_NOT_FOUND.
bool isPathRoleKey( const std::string &key )
{
  static const char *kSuffixes[] = { "output", "outputs", "input", "inputs", "path", "paths" };
  for ( const char *suffix : kSuffixes )
  {
    const size_t n = std::strlen( suffix );
    if ( key.size() >= n && key.compare( key.size() - n, n, suffix ) == 0 )
      return true;
  }
  return false;
}

/// Collects string parameter values that the schema declares as paths
/// (path-role keys only — see isPathRoleKey).
std::vector<std::string> inputPathsFromParams( const Json::Value &params )
{
  std::vector<std::string> paths;
  if ( !params.isObject() )
    return paths;
  for ( const auto &key : params.getMemberNames() )
  {
    if ( !isPathRoleKey( key ) )
      continue;
    const Json::Value &value = params[key];
    if ( value.isString() && value.asString().size() > 4 )
      paths.push_back( value.asString() );
    else if ( value.isArray() )
    {
      for ( const auto &item : value )
        if ( item.isString() && item.asString().size() > 4 )
          paths.push_back( item.asString() );
    }
  }
  return paths;
}

} // namespace

class WorkflowPreflightTool final : public SpatialTool
{
  public:
    std::string name() const override { return "workflow:preflight"; }
    std::string displayName() const override { return "Preflight workflow"; }
    std::string description() const override
    {
      return "STATIC validation of a workflow DAG before any execution — never run the workflow "
             "to discover structural errors. Checks: JSON schema (WF_SCHEMA_INVALID), cycles "
             "(WF_CYCLE), unknown operators (WF_UNKNOWN_OPERATOR), missing required parameters "
             "(WF_MISSING_PARAM), input file existence (WF_INPUT_NOT_FOUND), output conflicts "
             "(WF_OUTPUT_CONFLICT), model readiness (WF_MODEL_NOT_READY), RAM estimate "
             "(WF_RESOURCE_HEAVY, advisory). Every issue carries a repairable flag and a "
             "suggested action so planning agents can fix and re-submit.";
    }
    std::vector<std::string> tags() const override
    {
      return { "workflow", "preflight", "planning", "validation", "dag" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value workflow( Json::objectValue );
      workflow["type"] = "object";
      workflow["description"] = "WorkflowDefinition JSON: {id, title, steps: [{id, operatorId, params, inputs, ...}]}";
      props["workflow"] = workflow;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "workflow" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["kind"] = Json::Value( Json::objectValue );
      schema["properties"]["verdict"] = Json::Value( Json::objectValue );
      schema["properties"]["issues"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const Json::Value &workflowJson = input["workflow"];
      if ( !workflowJson.isObject() )
        return SpatialToolResult::failure( "Missing required parameter: workflow (object)",
                                           "INVALID_PARAMETER", "validation" );

      std::vector<Json::Value> issues;
      std::vector<Json::Value> checks;
      const auto addCheck = [ &checks ]( const std::string &name, bool passed,
                                         const std::string &code, Json::Value details ) {
        checks.push_back( makeAssessmentCheck( name, passed, passed ? "info" : "error", code,
                                               details ) );
      };

      const std::string subject =
        workflowJson.isMember( "id" ) && workflowJson["id"].isString() ? workflowJson["id"].asString()
                                                                       : "workflow";

      // --- 1. Structural parse ------------------------------------------------
      if ( !workflowJson.isMember( "steps" ) || !workflowJson["steps"].isArray() )
      {
        issues.push_back( makeIssue( "WF_SCHEMA_INVALID", "error",
                                     "Workflow definition needs a 'steps' array", false, "",
                                     makeRepairSuggestion( "fix_schema", Json::Value() ) ) );
        addCheck( "schema", false, "WF_SCHEMA_INVALID", Json::Value() );
        Json::Value issuesJson0 = toJsonArray( issues );
        return SpatialToolResult::ok(
          makePreflightResult( subject, "blocked", issuesJson0, toJsonArray( checks ) ) );
      }
      sicnu::workflow::WorkflowDefinition definition;
      std::string parseError;
      Json::Value schemaChecks( Json::objectValue );
      const bool parsed =
        sicnu::workflow::workflowDefinitionFromJson( workflowJson, definition, parseError );
      if ( !parsed )
      {
        issues.push_back( makeIssue(
          "WF_SCHEMA_INVALID", "error",
          "Workflow definition does not parse: " + parseError, true, "",
          makeRepairSuggestion(
            "fix_schema",
            Json::Value() ) ) );
        addCheck( "schema", false, "WF_SCHEMA_INVALID", Json::Value() );
        return SpatialToolResult::ok(
          makePreflightResult( subject, "blocked", toJsonArray( issues ), toJsonArray( checks ) ) );
      }
      addCheck( "schema", true, "", Json::Value() );

      // --- 2. Topology ---------------------------------------------------------
      std::vector<std::string> orderedIds;
      std::string topologyError;
      if ( !sicnu::workflow::topologicalSortSteps( definition, orderedIds, topologyError ) )
      {
        issues.push_back( makeIssue( "WF_CYCLE", "error",
                                     "Workflow graph has a cycle or dangling edge: " + topologyError,
                                     true, "", makeRepairSuggestion( "break_cycle", Json::Value() ) ) );
        addCheck( "topology", false, "WF_CYCLE", Json::Value() );
      }
      else
      {
        addCheck( "topology", true, "", Json::Value() );
      }

      // --- 3. Per-step checks ----------------------------------------------------
      std::set<std::string> outputPaths;
      long long totalRamBytes = 0;
      for ( const auto &step : definition.steps )
      {
        const std::string operatorId = step.operatorId;

        // 3a. Operator existence.
        if ( !operatorExists( operatorId ) )
        {
          issues.push_back( makeIssue(
            "WF_UNKNOWN_OPERATOR", "error",
            "Step '" + step.id + "' references unknown operator '" + operatorId + "'",
            true, step.id,
            makeRepairSuggestion( "search_capabilities",
                                  suggestionArgs( step.id ) ) ) );
          continue;
        }

        // 3b. Required parameter presence against the operator schema.
        const Json::Value schema = operatorSchema( operatorId );
        if ( schema.isObject() )
        {
          const std::string missing = validateAgainstRequired( step.params, schema );
          if ( !missing.empty() )
          {
            issues.push_back( makeIssue( "WF_MISSING_PARAM", "error",
                                         "Step '" + step.id + "': " + missing, true, step.id,
                                         makeRepairSuggestion( "fill_params", Json::Value() ) ) );
          }
        }

        // 3c. Input existence.
        for ( const std::string &path : inputPathsFromParams( step.params ) )
        {
          if ( !QFileInfo::exists( QString::fromStdString( path ) ) )
          {
            issues.push_back( makeIssue(
              "WF_INPUT_NOT_FOUND", "error",
              "Step '" + step.id + "' input does not exist: " + path, true, step.id,
              makeRepairSuggestion( "fix_input_path", Json::Value() ) ) );
          }
        }

        // 3d. Output conflicts: same output path declared twice.
        if ( step.params.isObject() && step.params.isMember( "output" ) &&
             step.params["output"].isString() )
        {
          const std::string output = step.params["output"].asString();
          if ( !output.empty() && !outputPaths.insert( output ).second )
          {
            issues.push_back( makeIssue( "WF_OUTPUT_CONFLICT", "error",
                                         "Output path declared twice: " + output, true, step.id,
                                         makeRepairSuggestion( "rename_output", Json::Value() ) ) );
          }
        }

        // 3e. Model readiness when the step names a model.
        if ( step.params.isObject() && step.params.isMember( "model" ) &&
             step.params["model"].isString() )
        {
          const std::string modelName = step.params["model"].asString();
          auto model = sicnu::operators::ModelCatalog::instance().find( modelName );
          if ( model )
          {
            if ( model->readiness != sicnu::operators::ModelReadiness::Ready )
            {
              issues.push_back( makeIssue(
                "WF_MODEL_NOT_READY", "error",
                "Step '" + step.id + "' model '" + modelName + "' is not ready (" +
                  modelReadinessName( model->readiness ) + ")",
                true, step.id,
                makeRepairSuggestion( "select_model", Json::Value() ) ) );
            }
          }
          else
          {
            issues.push_back( makeIssue( "WF_MODEL_NOT_READY", "error",
                                         "Step '" + step.id + "' model '" + modelName +
                                           "' not in catalog",
                                         true, step.id,
                                         makeRepairSuggestion( "select_model", Json::Value() ) ) );
          }
        }

        // 3f. Resource estimate accumulation (advisory only).
        if ( auto op = sicnu::operators::RSOperatorRegistry::instance().create(
               operatorId ) )
        {
          const Json::Value estimate = op->executionEstimate();
          Json::Int64 ramBytes = 0;
          if ( estimate.isObject() && estimate.isMember( "estimatedRamBytes" ) &&
               jsonInt64IfFits( estimate["estimatedRamBytes"], ramBytes ) )
            totalRamBytes += ramBytes;
        }
      }

      constexpr long long kAdvisoryRamBytes = 8LL * 1024 * 1024 * 1024;
      if ( totalRamBytes > kAdvisoryRamBytes )
      {
        issues.push_back( makeIssue(
          "WF_RESOURCE_HEAVY", "warning",
          "Aggregate RAM estimate exceeds 8 GiB (" + std::to_string( totalRamBytes / ( 1024 * 1024 ) ) +
            " MiB) — consider splitting or streaming",
          false, "", Json::Value() ) );
      }

      const bool hasSchemaCheck = true;
      Q_UNUSED( hasSchemaCheck );
      Json::Value checksJson = toJsonArray( checks );
      Json::Value issuesJson = toJsonArray( issues );
      return SpatialToolResult::ok( makePreflightResult( subject, verdictFromIssues( issuesJson ),
                                                         issuesJson, checksJson ) );
    }

  private:
    static Json::Value toJsonArray( const std::vector<Json::Value> &values )
    {
      Json::Value arr( Json::arrayValue );
      for ( const auto &v : values )
        arr.append( v );
      return arr;
    }

    static Json::Value suggestionArgs( const std::string &stepId )
    {
      Json::Value args( Json::objectValue );
      args["step"] = stepId;
      return args;
    }
};

void registerWorkflowPreflightTool()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<WorkflowPreflightTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::spatial_tools
