// tests/test_explain_agent_tool.cpp
//
// RS14-15 Slice G: the explain:step agent surface, exercised against the
// live operator registry and a fixture guidance corpus. This test compiles
// the tool TU directly; explain_step_tool.cpp pulls registry-initialization
// symbols from sicnu_operators_core and spatial-tool family symbols from
// sicnu_agent (build-wiring drift rule 11), so both are linked here.
// Registration into SpatialToolRegistry and catalog visibility are covered
// by the heavy agent tool lane (test_agent_tools_3).
//
// Pinned behavior: closed modes; fail-closed on unknown operators and
// unknown nodes; hostile documents are typed refusals; guidance conflicts
// surface as problems; the response carries the canonical explanation, a
// deterministic markdown rendering, the validator's report, and an honest
// byte budget (markdown dropped, facts never cut).
#include <catch2/catch_test_macros.hpp>

#include "agent/spatial_tools/explain_step_tool.h"
#include "explain/step_explanation.h"
#include "operators/rs/rs_operators_init.h"

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define SICNU_TEST_GETPID ::_getpid
#else
#include <unistd.h>
#define SICNU_TEST_GETPID ::getpid
#endif

using namespace sicnu::agent::spatial_tools;

namespace
{

struct TempDir
{
  std::filesystem::path path;
  TempDir()
  {
    path = std::filesystem::temp_directory_path()
           / ( "explain_agent_tool_test_" + std::to_string( SICNU_TEST_GETPID() ) );
    std::filesystem::create_directories( path );
  }
  ~TempDir()
  {
    std::filesystem::remove_all( path );
    setExplainGuidanceDirectory( std::string() );
  }
  void write( const std::string &name, const std::string &content )
  {
    std::ofstream out( path / name, std::ios::binary );
    out << content;
  }
};

std::string guidanceWith( const std::string &operatorId, const std::string &extraFields )
{
  return "{\n  \"schema\": \"exp.step_guidance.v1\",\n  \"operatorId\": \"" + operatorId
         + "\",\n  \"purpose\": \"教学：标定算子把 DN 转为辐亮度\",\n" + extraFields + "\n}";
}

Json::Value documentJson()
{
  Json::Value port( Json::objectValue );
  port["portName"] = "input";
  port["dataType"] = "Raster";
  port["crs"] = "EPSG:32649";
  port["radiometricState"] = "DN";
  port["resolutionX"] = 30.0;
  port["resolutionY"] = 30.0;
  port["bandCount"] = 6;
  port["isRequired"] = true;

  Json::Value node( Json::objectValue );
  node["nodeId"] = "n1";
  node["operatorId"] = "rs:radiometric_calibration";
  node["displayName"] = "Calibrate";
  node["parameters"] = Json::Value( Json::objectValue );
  node["inputPorts"] = Json::Value( Json::arrayValue );
  node["inputPorts"].append( port );
  Json::Value outPort = port;
  outPort["portName"] = "output";
  outPort["radiometricState"] = "Radiance";
  node["outputPorts"] = Json::Value( Json::arrayValue );
  node["outputPorts"].append( outPort );
  Json::Value position( Json::objectValue );
  position["x"] = 0.0;
  position["y"] = 0.0;
  node["canvasPosition"] = position;

  Json::Value document( Json::objectValue );
  document["version"] = "2.1";
  document["workflowId"] = "wf-tool";
  document["name"] = "Tool document";
  document["metadata"] = Json::Value( Json::objectValue );
  document["nodes"] = Json::Value( Json::arrayValue );
  document["nodes"].append( node );
  document["edges"] = Json::Value( Json::arrayValue );
  return document;
}

} // namespace

TEST_CASE( "operator mode explains a live operator with authored guidance",
           "[explain][agent_tool]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();

  TempDir dir;
  dir.write( "rs_spectral_index.json",
             guidanceWith( "rs:spectral_index",
                           "  \"parameterRationale\": [ { \"parameter\": \"index\", "
                           "\"rationale\": \"NDVI 是最常用的植被指数\" } ]" ) );
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  REQUIRE( tool != nullptr );
  CHECK( tool->name() == "explain:step" );

  Json::Value input;
  input["mode"] = "operator";
  input["operatorId"] = "rs:spectral_index";
  Json::Value parameters;
  parameters["index"] = "NDVI";
  input["parameters"] = parameters;

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output.isObject() );
  CHECK( result.output["explanation"]["schemaVersion"].asString()
         == sicnu::explain::StepExplanationSchemaV1 );
  CHECK( result.output["explanation"]["operatorId"].asString() == "rs:spectral_index" );
  // Machine fact + authored narrative, both tagged with their provenance.
  REQUIRE( result.output["explanation"]["purpose"].isArray() );
  CHECK( result.output["explanation"]["purpose"].size() >= 1 );
  // Deterministic markdown: identical input renders identical bytes.
  const std::string markdownA = result.output["markdown"].asString();
  const SpatialToolResult again = tool->execute( input );
  CHECK( again.success );
  CHECK( again.output["markdown"].asString() == markdownA );
  CHECK( !markdownA.empty() );
  CHECK( result.output["valid"].asBool() );
}

TEST_CASE( "unknown operators fail closed with a typed error", "[explain][agent_tool][hallucination]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir dir; // empty corpus
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "operator";
  input["operatorId"] = "rs:definitely_not_registered";

  const SpatialToolResult result = tool->execute( input );
  CHECK( !result.success );
  CHECK( result.errorCode == "UNKNOWN_OPERATOR" );
}

TEST_CASE( "workflow_document mode explains a node and refuses hostile documents",
           "[explain][agent_tool]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir dir;
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();

  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";
  const SpatialToolResult explained = tool->execute( input );
  REQUIRE( explained.success );
  CHECK( explained.output["explanation"]["operatorId"].asString() == "rs:radiometric_calibration" );
  // State synthesis from the projected ports is visible in the document.
  CHECK( explained.output["markdown"].asString().find( "radiometric_state" )
         != std::string::npos );

  Json::Value unknownNode = input;
  unknownNode["nodeId"] = "ghost";
  const SpatialToolResult unknown = tool->execute( unknownNode );
  CHECK( !unknown.success );
  CHECK( unknown.errorCode == "NODE_UNKNOWN" );

  Json::Value hostile = input;
  hostile["document"]["nodes"][0]["nodeId"] = 17; // wrong-typed required field
  const SpatialToolResult refused = tool->execute( hostile );
  CHECK( !refused.success );
  CHECK( refused.errorCode == "DOCUMENT_REFUSED" );

  Json::Value missing = input;
  missing.removeMember( "document" );
  const SpatialToolResult invalid = tool->execute( missing );
  CHECK( !invalid.success );
  CHECK( invalid.errorCode == "INVALID_PARAMETER" );
}

TEST_CASE( "guidance conflicts surface as problems, not silent merges",
           "[explain][agent_tool][conflict]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();

  TempDir dir;
  // The authored narrative claims BOA; the document's ports declare
  // DN -> Radiance. The port facts must win and the conflict must surface.
  dir.write( "rs_radiometric_calibration.json",
             guidanceWith( "rs:radiometric_calibration",
                           "  \"stateNarrative\": { \"before\": \"BOA\", \"after\": \"BOA\" }" ) );
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  CHECK( result.output["valid"].asBool() );
  bool conflictSurfaced = false;
  for ( const Json::Value &problem : result.output["problems"] )
    if ( problem["code"].asString() == "state_contradiction" )
      conflictSurfaced = true;
  CHECK( conflictSurfaced );
  // Port facts kept: the synthesized transition carries DN -> Radiance.
  const Json::Value &transitions = result.output["explanation"]["stateChanges"];
  bool factsKept = false;
  for ( const Json::Value &transition : transitions )
  {
    if ( transition["aspect"].asString() == "radiometric_state"
         && transition["before"].asString() == "DN"
         && transition["after"].asString() == "Radiance" )
      factsKept = true;
  }
  CHECK( factsKept );
}

TEST_CASE( "the byte budget drops markdown honestly and never cuts facts",
           "[explain][agent_tool][budget]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir dir;
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output["budget"].isObject() );
  CHECK( result.output["budget"]["truncated"].asBool() == false );
  CHECK( result.output["budget"]["outputBytes"].asUInt64() > 0 );
  CHECK( result.output["budget"]["maxOutputBytes"].asUInt64() > 0 );
  // Invariant: markdown present ⟺ nothing was truncated.
  CHECK( result.output.isMember( "markdown" ) == !result.output["budget"]["truncated"].asBool() );
}

TEST_CASE( "an oversized problem set trips the budget: markdown dropped, facts kept",
           "[explain][agent_tool][budget][hostile]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();

  TempDir dir;
  // A hostile guidance entry: 64 (the loader's per-file cap) rationales
  // naming long parameters that do not exist in the live schema. Each one
  // becomes an unknown_parameter_reference problem embedding the long name,
  // deterministically pushing the full response past the byte budget.
  const std::string longName( 200, 'x' );
  std::string rationales;
  for ( int i = 0; i < 64; ++i )
  {
    if ( i > 0 )
      rationales += ",";
    rationales += "{ \"parameter\": \"" + longName + std::to_string( i )
                  + "\", \"rationale\": \"ghost rationale\" }";
  }
  dir.write( "rs_spectral_index.json",
             guidanceWith( "rs:spectral_index",
                           "  \"parameterRationale\": [ " + rationales + " ]" ) );
  setExplainGuidanceDirectory( dir.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "operator";
  input["operatorId"] = "rs:spectral_index";

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output["problems"].size() == 64 );
  // The truncation is honest: markdown gone, flag set, budget stated.
  CHECK( !result.output.isMember( "markdown" ) );
  CHECK( result.output["budget"]["truncated"].asBool() == true );
  CHECK( result.output["budget"]["maxOutputBytes"].asUInt64() > 0 );
  // The authoritative explanation document is never cut mid-fact.
  CHECK( result.output["explanation"]["schemaVersion"].asString()
         == sicnu::explain::StepExplanationSchemaV1 );
}
