// src/agent/contracts/spatial_contracts.cpp
#include "spatial_contracts.h"

#include <algorithm>

#include <json/writer.h>

namespace sicnu::agent::contracts {

namespace {

Json::Value stringArray( std::initializer_list<const char *> values )
{
  Json::Value arr( Json::arrayValue );
  for ( const char *v : values )
    arr.append( v );
  return arr;
}

} // namespace

Json::Value makeEnvelope( const std::string &kind, Json::Value payload )
{
  Json::Value doc( Json::objectValue );
  doc["schema_version"] = kContractsSchemaVersion;
  doc["kind"] = kind;
  for ( const auto &key : payload.getMemberNames() )
    doc[key] = payload[key];
  return doc;
}

std::string checkEnvelope( const Json::Value &doc, const std::string &expectedKind )
{
  if ( !doc.isObject() )
    return "document must be a JSON object";
  if ( !doc.isMember( "schema_version" ) || !doc["schema_version"].isString() )
    return "missing string field 'schema_version'";
  if ( doc["schema_version"].asString() != kContractsSchemaVersion )
    return "unsupported schema_version '" + doc["schema_version"].asString() + "'";
  if ( !doc.isMember( "kind" ) || !doc["kind"].isString() )
    return "missing string field 'kind'";
  if ( !expectedKind.empty() && doc["kind"].asString() != expectedKind )
    return "expected kind '" + expectedKind + "', got '" + doc["kind"].asString() + "'";
  return std::string();
}

// ---------------------------------------------------------------------------
// DatasetUnderstanding
// ---------------------------------------------------------------------------

Json::Value datasetUnderstandingFromRasterInspect( const Json::Value &rasterInspect )
{
  Json::Value body( Json::objectValue );
  body["source_kind"] = "raster";
  body["path"] = rasterInspect.get( "path", "" );
  body["driver"] = rasterInspect.get( "driver", "" );
  body["size"] = rasterInspect.get( "size", Json::Value( Json::objectValue ) );
  body["pixel_size"] = rasterInspect.get( "pixelSize", Json::Value() );
  body["extent"] = rasterInspect.get( "extent", Json::Value() );
  body["crs"] = rasterInspect.get( "crs", Json::Value() );

  // Band roles in band order — the field algorithms actually dispatch on.
  Json::Value bandRoles( Json::arrayValue );
  if ( rasterInspect.isMember( "bands" ) && rasterInspect["bands"].isArray() )
  {
    for ( const auto &band : rasterInspect["bands"] )
      bandRoles.append( band.get( "role", "" ) );
  }
  body["band_roles"] = bandRoles;
  body["band_count"] = static_cast<Json::Int>( bandRoles.size() );

  for ( const char *key : { "SICNU_PRODUCT_TYPE", "SICNU_SPACECRAFT", "SICNU_PROCESSING_LEVEL",
                            "SICNU_RADIOMETRIC_STATE", "SICNU_ACQUISITION_DATE" } )
  {
    if ( rasterInspect.isMember( key ) )
      body["radiometric_state"] = rasterInspect[key];
  }
  return makeEnvelope( "dataset_understanding", std::move( body ) );
}

Json::Value datasetUnderstandingFromVectorInspect( const Json::Value &vectorInspect )
{
  Json::Value body( Json::objectValue );
  body["source_kind"] = "vector";
  body["path"] = vectorInspect.get( "path", "" );
  body["crs"] = vectorInspect.get( "crs", Json::Value() );
  body["feature_count"] = vectorInspect.get( "featureCount", Json::Value() );
  body["geometry_type"] = vectorInspect.get( "geometryType", Json::Value() );
  if ( vectorInspect.isMember( "fields" ) )
    body["fields"] = vectorInspect["fields"];
  return makeEnvelope( "dataset_understanding", std::move( body ) );
}

std::vector<std::string> validateDatasetUnderstanding( const Json::Value &doc )
{
  std::vector<std::string> problems;
  const std::string env = checkEnvelope( doc, "dataset_understanding" );
  if ( !env.empty() )
  {
    problems.push_back( env );
    return problems;
  }
  if ( !doc.isMember( "source_kind" ) || !doc["source_kind"].isString() )
    problems.push_back( "missing string field 'source_kind'" );
  if ( !doc.isMember( "path" ) || !doc["path"].isString() || doc["path"].asString().empty() )
    problems.push_back( "missing string field 'path'" );
  if ( doc.isMember( "band_roles" ) && !doc["band_roles"].isArray() )
    problems.push_back( "'band_roles' must be an array" );
  return problems;
}

// ---------------------------------------------------------------------------
// CapabilityCandidate
// ---------------------------------------------------------------------------

Json::Value makeCostEstimate( const std::string &costClass, Json::Int estimatedRamMb,
                              Json::Int estimatedSeconds, bool gpuAccelerated,
                              bool largeRasterSafe )
{
  Json::Value cost( Json::objectValue );
  cost["cost_class"] = costClass;
  if ( estimatedRamMb > 0 )
    cost["estimated_ram_mb"] = estimatedRamMb;
  if ( estimatedSeconds > 0 )
    cost["estimated_seconds"] = estimatedSeconds;
  cost["gpu_accelerated"] = gpuAccelerated;
  cost["large_raster_safe"] = largeRasterSafe;
  return cost;
}

Json::Value makeCapabilityCandidate( const std::string &candidateId, const std::string &kind,
                                     double compatibility, Json::Value reasons,
                                     Json::Value warnings, Json::Value estimatedCost )
{
  Json::Value c( Json::objectValue );
  c["candidate"] = candidateId;
  c["kind"] = kind;
  c["compatibility"] = std::max( 0.0, std::min( 1.0, compatibility ) );
  c["reasons"] = reasons.isArray() ? reasons : Json::Value( Json::arrayValue );
  c["warnings"] = warnings.isArray() ? warnings : Json::Value( Json::arrayValue );
  c["estimated_cost"] = estimatedCost.isObject() ? estimatedCost : Json::Value( Json::objectValue );
  return c;
}

std::vector<std::string> validateCapabilityCandidate( const Json::Value &candidate )
{
  std::vector<std::string> problems;
  if ( !candidate.isObject() )
  {
    problems.push_back( "candidate must be an object" );
    return problems;
  }
  if ( !candidate.isMember( "candidate" ) || !candidate["candidate"].isString() ||
       candidate["candidate"].asString().empty() )
    problems.push_back( "missing string field 'candidate'" );
  if ( !candidate.isMember( "kind" ) || !candidate["kind"].isString() )
    problems.push_back( "missing string field 'kind'" );
  else if ( candidate["kind"].asString() != "algorithm" && candidate["kind"].asString() != "model" )
    problems.push_back( "'kind' must be 'algorithm' or 'model'" );
  if ( !candidate.isMember( "compatibility" ) || !candidate["compatibility"].isNumeric() )
    problems.push_back( "missing numeric field 'compatibility'" );
  else if ( candidate["compatibility"].asDouble() < 0.0 ||
            candidate["compatibility"].asDouble() > 1.0 )
    problems.push_back( "'compatibility' must be within [0,1]" );
  for ( const char *key : { "reasons", "warnings" } )
    if ( candidate.isMember( key ) && !candidate[key].isArray() )
      problems.push_back( std::string( "'" ) + key + "' must be an array" );
  return problems;
}

// ---------------------------------------------------------------------------
// PreflightResult
// ---------------------------------------------------------------------------

Json::Value makeRepairSuggestion( const std::string &action, Json::Value arguments )
{
  Json::Value s( Json::objectValue );
  s["action"] = action;
  s["arguments"] = arguments.isObject() ? arguments : Json::Value( Json::objectValue );
  return s;
}

Json::Value makeIssue( const std::string &code, const std::string &severity,
                       const std::string &message, bool repairable,
                       const std::string &itemId, Json::Value suggestedAction )
{
  Json::Value issue( Json::objectValue );
  issue["code"] = code;
  issue["severity"] = severity; // "error" | "warning" | "info"
  issue["message"] = message;
  issue["repairable"] = repairable;
  if ( !itemId.empty() )
    issue["item_id"] = itemId;
  if ( suggestedAction.isObject() && suggestedAction.isMember( "action" ) )
    issue["suggested_action"] = suggestedAction;
  return issue;
}

std::string verdictFromIssues( const Json::Value &issues )
{
  bool blocking = false;
  bool repairable = false;
  if ( issues.isArray() )
  {
    for ( const auto &issue : issues )
    {
      const std::string severity = issue.get( "severity", "" ).asString();
      if ( severity == "error" )
        blocking = true;
      else if ( issue.get( "repairable", false ).asBool() )
        repairable = true;
    }
  }
  if ( blocking )
    return "blocked";
  if ( repairable )
    return "fixable";
  return "ok";
}

Json::Value makePreflightResult( const std::string &subject, const std::string &verdict,
                                 Json::Value issues, Json::Value checks )
{
  Json::Value body( Json::objectValue );
  body["subject"] = subject;
  body["verdict"] = verdict;
  body["issues"] = issues.isArray() ? issues : Json::Value( Json::arrayValue );
  body["checks"] = checks.isArray() ? checks : Json::Value( Json::arrayValue );
  return makeEnvelope( "preflight_result", std::move( body ) );
}

std::vector<std::string> validatePreflightResult( const Json::Value &doc )
{
  std::vector<std::string> problems;
  const std::string env = checkEnvelope( doc, "preflight_result" );
  if ( !env.empty() )
  {
    problems.push_back( env );
    return problems;
  }
  if ( !doc.isMember( "verdict" ) || !doc["verdict"].isString() )
    problems.push_back( "missing string field 'verdict'" );
  else
  {
    const std::string v = doc["verdict"].asString();
    if ( v != "ok" && v != "fixable" && v != "blocked" )
      problems.push_back( "'verdict' must be ok|fixable|blocked" );
  }
  if ( !doc.isMember( "subject" ) || !doc["subject"].isString() )
    problems.push_back( "missing string field 'subject'" );
  if ( doc.isMember( "issues" ) && doc["issues"].isArray() )
  {
    for ( const auto &issue : doc["issues"] )
    {
      if ( !issue.isObject() || !issue.isMember( "code" ) || !issue.isMember( "severity" ) ||
           !issue.isMember( "message" ) )
      {
        problems.push_back( "each issue needs code/severity/message" );
        break;
      }
    }
  }
  return problems;
}

// ---------------------------------------------------------------------------
// ExecutionPlan
// ---------------------------------------------------------------------------

Json::Value makeExecutionStep( const std::string &stepId, const std::string &operatorId,
                               Json::Value params, Json::Value inputs )
{
  Json::Value step( Json::objectValue );
  step["id"] = stepId;
  step["operator_id"] = operatorId;
  step["params"] = params.isObject() ? params : Json::Value( Json::objectValue );
  step["inputs"] = inputs.isArray() ? inputs : Json::Value( Json::arrayValue );
  return step;
}

Json::Value makeExecutionPlan( const std::string &planId, Json::Value steps, Json::Value estimates )
{
  Json::Value body( Json::objectValue );
  body["plan_id"] = planId;
  body["steps"] = steps.isArray() ? steps : Json::Value( Json::arrayValue );
  body["estimates"] = estimates.isObject() ? estimates : Json::Value( Json::objectValue );
  return makeEnvelope( "execution_plan", std::move( body ) );
}

std::vector<std::string> validateExecutionPlan( const Json::Value &doc )
{
  std::vector<std::string> problems;
  const std::string env = checkEnvelope( doc, "execution_plan" );
  if ( !env.empty() )
  {
    problems.push_back( env );
    return problems;
  }
  if ( !doc.isMember( "steps" ) || !doc["steps"].isArray() )
  {
    problems.push_back( "missing array field 'steps'" );
    return problems;
  }
  std::vector<std::string> ids;
  for ( const auto &step : doc["steps"] )
  {
    if ( !step.isObject() || !step.isMember( "id" ) || !step.isMember( "operator_id" ) )
    {
      problems.push_back( "each step needs id/operator_id" );
      return problems;
    }
    ids.push_back( step["id"].asString() );
  }
  // Unique step ids.
  for ( size_t i = 0; i < ids.size(); ++i )
    for ( size_t j = i + 1; j < ids.size(); ++j )
      if ( ids[i] == ids[j] )
        problems.push_back( "duplicate step id '" + ids[i] + "'" );
  // Referential integrity of inputs.
  for ( const auto &step : doc["steps"] )
  {
    if ( !step.isMember( "inputs" ) || !step["inputs"].isArray() )
      continue;
    for ( const auto &input : step["inputs"] )
    {
      const std::string from = input.get( "step", "" ).asString();
      bool found = false;
      for ( const auto &id : ids )
        found = found || id == from;
      if ( !found )
        problems.push_back( "step '" + step["id"].asString() + "' references unknown step '" +
                            from + "'" );
    }
  }
  return problems;
}

// ---------------------------------------------------------------------------
// ResultAssessment
// ---------------------------------------------------------------------------

Json::Value makeAssessmentCheck( const std::string &check, bool passed,
                                 const std::string &severity, const std::string &code,
                                 Json::Value details )
{
  Json::Value c( Json::objectValue );
  c["check"] = check;
  c["passed"] = passed;
  c["severity"] = severity; // "info" when passed; "warning"/"error" allowed when failed
  c["code"] = code;
  c["details"] = details.isObject() ? details : Json::Value( Json::objectValue );
  return c;
}

Json::Value makeResultAssessment( const std::string &target, const std::string &verdict,
                                  Json::Value checks, Json::Value provenance )
{
  Json::Value body( Json::objectValue );
  body["target"] = target;
  body["verdict"] = verdict; // "pass" | "warn" | "fail"
  body["checks"] = checks.isArray() ? checks : Json::Value( Json::arrayValue );
  body["provenance"] = provenance.isObject() ? provenance : Json::Value( Json::objectValue );
  return makeEnvelope( "result_assessment", std::move( body ) );
}

std::vector<std::string> validateResultAssessment( const Json::Value &doc )
{
  std::vector<std::string> problems;
  const std::string env = checkEnvelope( doc, "result_assessment" );
  if ( !env.empty() )
  {
    problems.push_back( env );
    return problems;
  }
  if ( !doc.isMember( "target" ) || !doc["target"].isString() )
    problems.push_back( "missing string field 'target'" );
  if ( !doc.isMember( "verdict" ) || !doc["verdict"].isString() )
    problems.push_back( "missing string field 'verdict'" );
  else
  {
    const std::string v = doc["verdict"].asString();
    if ( v != "pass" && v != "warn" && v != "fail" )
      problems.push_back( "'verdict' must be pass|warn|fail" );
  }
  if ( doc.isMember( "checks" ) && doc["checks"].isArray() )
  {
    for ( const auto &check : doc["checks"] )
    {
      if ( !check.isObject() || !check.isMember( "check" ) || !check.isMember( "passed" ) )
      {
        problems.push_back( "each check needs check/passed" );
        break;
      }
    }
  }
  return problems;
}

// ---------------------------------------------------------------------------
// Bounded-output helpers
// ---------------------------------------------------------------------------

Json::Value paginate( const Json::Value &items, int offset, int limit )
{
  const int total = items.isArray() ? static_cast<int>( items.size() ) : 0;
  if ( limit <= 0 )
    limit = 50;
  if ( offset < 0 )
    offset = 0;

  Json::Value page( Json::objectValue );
  Json::Value slice( Json::arrayValue );
  const int end = std::min( total, offset + limit );
  for ( int i = offset; i < end; ++i )
    slice.append( items[i] );
  page["items"] = slice;
  page["total"] = total;
  page["offset"] = offset;
  page["next_offset"] = ( end < total ) ? end : -1;
  return page;
}

size_t serializedSize( const Json::Value &doc )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, doc ).size();
}

} // namespace sicnu::agent::contracts
