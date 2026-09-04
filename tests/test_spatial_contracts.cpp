// tests/test_spatial_contracts.cpp
// Phase A — Spatial Reasoning Contract document tests (ADR 0128).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/contracts/spatial_contracts.h"

using namespace sicnu::agent::contracts;

namespace {

Json::Value stringArray( std::initializer_list<const char *> values )
{
  Json::Value arr( Json::arrayValue );
  for ( const char *v : values )
    arr.append( v );
  return arr;
}

Json::Value sampleRasterInspect()
{
  Json::Value out( Json::objectValue );
  out["path"] = "/data/sentinel2.tif";
  out["driver"] = "GTiff";
  Json::Value size( Json::objectValue );
  size["width"] = 10980;
  size["height"] = 10980;
  out["size"] = size;
  Json::Value crs( Json::objectValue );
  crs["authid"] = "EPSG:32650";
  out["crs"] = crs;
  Json::Value bands( Json::arrayValue );
  for ( const char *role : { "blue", "green", "red", "nir" } )
  {
    Json::Value b( Json::objectValue );
    b["role"] = role;
    bands.append( b );
  }
  out["bands"] = bands;
  out["SICNU_RADIOMETRIC_STATE"] = "surface_reflectance";
  return out;
}

} // namespace

TEST_CASE( "Contract envelope stamps and checks schema version", "[agent][contracts]" )
{
  Json::Value payload( Json::objectValue );
  payload["target"] = "x";
  const Json::Value doc = makeEnvelope( "preflight_result", payload );
  REQUIRE( doc["schema_version"].asString() == std::string( kContractsSchemaVersion ) );
  REQUIRE( doc["kind"].asString() == "preflight_result" );
  REQUIRE( doc["target"].asString() == "x" );
  CHECK( checkEnvelope( doc, "preflight_result" ).empty() );
  CHECK_FALSE( checkEnvelope( doc, "execution_plan" ).empty() );

  Json::Value broken = doc;
  broken.removeMember( "schema_version" );
  CHECK_FALSE( checkEnvelope( broken, "preflight_result" ).empty() );

  broken = doc;
  broken["schema_version"] = "9.9";
  CHECK_FALSE( checkEnvelope( broken, "preflight_result" ).empty() );
}

TEST_CASE( "DatasetUnderstanding adapts raster inspection output", "[agent][contracts]" )
{
  const Json::Value doc = datasetUnderstandingFromRasterInspect( sampleRasterInspect() );
  REQUIRE( doc["kind"].asString() == "dataset_understanding" );
  CHECK( validateDatasetUnderstanding( doc ).empty() );
  REQUIRE( doc["band_roles"].size() == 4 );
  CHECK( doc["band_roles"][3].asString() == "nir" );
  CHECK( doc["radiometric_state"].asString() == "surface_reflectance" );
  CHECK( doc["crs"]["authid"].asString() == "EPSG:32650" );

  // Missing path is a validation failure.
  Json::Value incomplete = doc;
  incomplete.removeMember( "path" );
  CHECK_FALSE( validateDatasetUnderstanding( incomplete ).empty() );
}

TEST_CASE( "CapabilityCandidate validates compatibility bounds", "[agent][contracts]" )
{
  const Json::Value candidate = makeCapabilityCandidate(
    "rs:spectral_index", "algorithm", 0.92,
    stringArray( { "band roles red+nir available" } ),
    Json::Value( Json::arrayValue ),
    makeCostEstimate( "low", 512, 30, false, true ) );
  CHECK( validateCapabilityCandidate( candidate ).empty() );
  CHECK( candidate["compatibility"].asDouble() == Catch::Approx( 0.92 ) );
  CHECK( candidate["estimated_cost"]["large_raster_safe"].asBool() );

  CHECK_FALSE( validateCapabilityCandidate( makeCapabilityCandidate( "", "algorithm", 0.5,
                                                                     Json::Value(), Json::Value(),
                                                                     Json::Value() ) )
                 .empty() );
  // The builder clamps out-of-range scores; the validator still rejects
  // documents that arrive with them (e.g. round-tripped from an agent).
  Json::Value outOfRange = makeCapabilityCandidate( "x", "algorithm", 1.0, Json::Value(),
                                                    Json::Value(), Json::Value() );
  outOfRange["compatibility"] = 1.5;
  CHECK_FALSE( validateCapabilityCandidate( outOfRange ).empty() );
  CHECK_FALSE( validateCapabilityCandidate( makeCapabilityCandidate( "x", "alien", 0.5,
                                                                     Json::Value(), Json::Value(),
                                                                     Json::Value() ) )
                 .empty() );
}

TEST_CASE( "PreflightResult verdict derivation", "[agent][contracts]" )
{
  Json::Value issues( Json::arrayValue );
  issues.append( makeIssue( "WF_UNKNOWN_OPERATOR", "error", "operator rs:nope missing", false,
                            "step1", Json::Value() ) );
  CHECK( verdictFromIssues( issues ) == "blocked" );

  Json::Value fixable( Json::arrayValue );
  fixable.append( makeIssue( "MAP_MISSING_TITLE", "warning", "no title", true, "",
                             makeRepairSuggestion( "add_title", Json::Value() ) ) );
  CHECK( verdictFromIssues( fixable ) == "fixable" );

  Json::Value clean( Json::arrayValue );
  CHECK( verdictFromIssues( clean ) == "ok" );

  const Json::Value doc = makePreflightResult( "wf-1", "blocked", issues, Json::Value( Json::arrayValue ) );
  CHECK( validatePreflightResult( doc ).empty() );
  CHECK( doc["issues"][0]["code"].asString() == "WF_UNKNOWN_OPERATOR" );
  CHECK( doc["issues"][0]["item_id"].asString() == "step1" );

  // Issue with suggested action and item id round-trips.
  Json::Value withFix( Json::arrayValue );
  withFix.append( makeIssue( "MAP_MISSING_LEGEND", "warning", "no legend", true, "map-1",
                             makeRepairSuggestion( "add_legend", Json::Value() ) ) );
  CHECK( withFix[0]["item_id"].asString() == "map-1" );
  CHECK( withFix[0]["suggested_action"]["action"].asString() == "add_legend" );
}

TEST_CASE( "ExecutionPlan referential integrity", "[agent][contracts]" )
{
  Json::Value steps( Json::arrayValue );
  Json::Value in1( Json::arrayValue );
  Json::Value ref1( Json::objectValue );
  ref1["step"] = "ndvi";
  ref1["port"] = "output";
  in1.append( ref1 );
  steps.append( makeExecutionStep( "ndvi", "rs:spectral_index", Json::Value(), Json::Value() ) );
  steps.append( makeExecutionStep( "stats", "rs:band_math", Json::Value(), in1 ) );

  const Json::Value plan = makeExecutionPlan( "plan-1", steps, Json::Value() );
  CHECK( validateExecutionPlan( plan ).empty() );

  // Broken reference
  Json::Value badRef( Json::objectValue );
  badRef["step"] = "ghost";
  badRef["port"] = "output";
  Json::Value badInputs( Json::arrayValue );
  badInputs.append( badRef );
  steps[1]["inputs"] = badInputs;
  const Json::Value brokenPlan = makeExecutionPlan( "plan-1", steps, Json::Value() );
  CHECK_FALSE( validateExecutionPlan( brokenPlan ).empty() );

  // Duplicate ids
  Json::Value dupSteps( Json::arrayValue );
  dupSteps.append( makeExecutionStep( "a", "rs:x", Json::Value(), Json::Value() ) );
  dupSteps.append( makeExecutionStep( "a", "rs:y", Json::Value(), Json::Value() ) );
  CHECK_FALSE( validateExecutionPlan( makeExecutionPlan( "p", dupSteps, Json::Value() ) ).empty() );
}

TEST_CASE( "ResultAssessment document shape", "[agent][contracts]" )
{
  Json::Value checks( Json::arrayValue );
  checks.append( makeAssessmentCheck( "nodata_ratio", true, "info", "", Json::Value() ) );
  Json::Value details( Json::objectValue );
  details["actual"] = -1.0;
  details["expected_min"] = -1.0;
  details["expected_max"] = 1.0;
  checks.append( makeAssessmentCheck( "value_range", true, "info", "", details ) );

  const Json::Value doc = makeResultAssessment( "asset-12", "pass", checks, Json::Value() );
  CHECK( validateResultAssessment( doc ).empty() );
  REQUIRE( doc["checks"].size() == 2 );
  CHECK( doc["checks"][1]["details"]["expected_max"].asDouble() == Catch::Approx( 1.0 ) );

  Json::Value bad = doc;
  bad["verdict"] = "maybe";
  CHECK_FALSE( validateResultAssessment( bad ).empty() );
}

TEST_CASE( "paginate slices with terminal next_offset", "[agent][contracts]" )
{
  Json::Value items( Json::arrayValue );
  for ( int i = 0; i < 12; ++i )
    items.append( i );

  Json::Value page = paginate( items, 0, 5 );
  CHECK( page["items"].size() == 5 );
  CHECK( page["total"].asInt() == 12 );
  CHECK( page["next_offset"].asInt() == 5 );

  page = paginate( items, 5, 5 );
  CHECK( page["items"].size() == 5 );
  CHECK( page["next_offset"].asInt() == 10 );

  page = paginate( items, 10, 5 );
  CHECK( page["items"].size() == 2 );
  CHECK( page["next_offset"].asInt() == -1 );

  page = paginate( items, 99, 5 );
  CHECK( page["items"].size() == 0 );
  CHECK( page["next_offset"].asInt() == -1 );

  // limit<=0 falls back to the default page size
  page = paginate( items, 0, 0 );
  CHECK( page["items"].size() == 12 ); // default 50 covers all 12
  CHECK( page["next_offset"].asInt() == -1 );
}

TEST_CASE( "serializedSize is bounded-reportable", "[agent][contracts]" )
{
  Json::Value doc = sampleRasterInspect();
  const size_t size = serializedSize( doc );
  CHECK( size > 0 );
  CHECK( size < kMaxToolOutputBytes );
}
