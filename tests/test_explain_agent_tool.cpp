// tests/test_explain_agent_tool.cpp
//
// RS14-15 Slice G: the explain:step agent surface, exercised against the
// live operator registry and a fixture guidance corpus. This test compiles
// the tool TU directly (it holds no registry symbols) so the narrow lane
// needs no sicnu_agent link; registration into SpatialToolRegistry and
// catalog visibility are covered by the heavy agent tool lane
// (test_agent_tools_3).
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
#include "workflow/pipeline_run_coordinator.h"
#include "workflow/workflow_ir_v2.h"
#include "workflow/workflow_provenance.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <memory>
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

// ── Run-scoped evidence (RS14-15 R3) ─────────────────────────────────────
// runId + provenanceDirectory attach the step's REAL execution facts from a
// PipelineRunCoordinator provenance record. The plan-only response shape is
// untouched; the run-scoped path adds execution facts plus verbatim load
// problems (a tampered record renders as unknown, never as a status).

namespace
{

struct EvidenceDir
{
  std::filesystem::path path;
  EvidenceDir()
  {
    path = std::filesystem::temp_directory_path()
           / ( "explain_agent_tool_evidence_" + std::to_string( SICNU_TEST_GETPID() ) );
    std::filesystem::create_directories( path );
  }
  ~EvidenceDir() { std::filesystem::remove_all( path ); }
  void write( const std::string &name, const std::string &content )
  {
    std::ofstream out( path / name, std::ios::binary );
    out << content;
  }
};

// Writes a real provenance_<runId>.json through the real writer (the same
// chain PipelineRunCoordinator uses) and returns the containing directory.
EvidenceDir writeProvenanceRun( const QString &runId, const QString &nodeId,
                                sicnu::workflow::ExecutionState state,
                                const QString &errorMessage, const QString &digest,
                                qint64 elapsedMs )
{
  QJsonObject port;
  port["portName"] = "input";
  port["dataType"] = "Raster";
  port["crs"] = "EPSG:32649";
  port["radiometricState"] = "DN";
  port["resolutionX"] = 30.0;
  port["resolutionY"] = 30.0;
  port["bandCount"] = 6;
  port["isRequired"] = true;

  QJsonObject node;
  node["nodeId"] = nodeId;
  node["operatorId"] = "rs:radiometric_calibration";
  node["displayName"] = "Calibrate";
  node["parameters"] = QJsonObject { { "unit", "radiance" } };
  node["inputPorts"] = QJsonArray { port };
  QJsonObject outPort = port;
  outPort["portName"] = "output";
  outPort["radiometricState"] = "Radiance";
  node["outputPorts"] = QJsonArray { outPort };
  node["canvasPosition"] = QJsonObject { { "x", 0.0 }, { "y", 0.0 } };

  QJsonObject document;
  document["version"] = "2.1";
  document["workflowId"] = "wf-tool-run";
  document["name"] = "Tool run document";
  document["metadata"] = QJsonObject();
  document["nodes"] = QJsonArray { node };
  document["edges"] = QJsonArray();

  const QJsonDocument parsed = QJsonDocument( document );
  const auto parsedDoc =
    sicnu::workflow::WorkflowIR::fromJson( parsed.object() );
  REQUIRE( parsedDoc.isSuccess() );

  QHash<QString, sicnu::workflow::NodeStatusSnapshot> statuses;
  sicnu::workflow::NodeStatusSnapshot snapshot;
  snapshot.nodeId = nodeId;
  snapshot.state = state;
  snapshot.elapsedMs = elapsedMs;
  snapshot.errorMessage = errorMessage;
  if ( !digest.isEmpty() )
  {
    snapshot.outputArtifactPath = "/tmp/out/calibrated.tif";
    snapshot.artifactFingerprint = digest;
  }
  statuses.insert( nodeId, snapshot );

  const sicnu::workflow::ProvenanceGraph graph = sicnu::workflow::ProvenanceGraph::fromRunState(
    runId, parsedDoc.value(), statuses, QStringLiteral( "sig" ) );

  EvidenceDir dir;
  dir.write( "provenance_" + runId.toStdString() + ".json",
             QJsonDocument( graph.toJson() ).toJson( QJsonDocument::Compact ).toStdString() );
  return dir;
}

bool hasEvidenceProblem( const Json::Value &response, const std::string &code )
{
  for ( const Json::Value &problem : response["evidenceProblems"] )
    if ( problem["code"].asString() == code )
      return true;
  return false;
}

} // namespace

TEST_CASE( "run-scoped mode attaches real execution facts from the provenance record",
           "[explain][agent_tool][evidence]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance; // empty corpus: execution facts need no authored text
  setExplainGuidanceDirectory( guidance.path.string() );

  const EvidenceDir evidenceDir =
    writeProvenanceRun( QStringLiteral( "run-42" ), QStringLiteral( "n1" ),
                        sicnu::workflow::ExecutionState::Succeeded, QString(),
                        QStringLiteral( "sha256full:abc123" ), 4321 );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";
  input["runId"] = "run-42";
  input["provenanceDirectory"] = evidenceDir.path.string();

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output.isMember( "evidenceProblems" ) );
  CHECK( result.output["evidenceProblems"].empty() );
  const Json::Value &execution = result.output["explanation"]["execution"];
  REQUIRE( execution.isObject() );
  CHECK( execution["status"].asString() == "Succeeded" );
  CHECK( execution["elapsedMs"].asInt64() == 4321 );
  CHECK( execution["artifactDigest"].asString() == "sha256full:abc123" );
  REQUIRE( execution["evidence"].isArray() );
  REQUIRE( execution["evidence"].size() >= 1 );
  CHECK( execution["evidence"][0]["target"].asString() == "provenance:run-42#node:n1" );
  // The markdown rendering carries the machine-status line too.
  CHECK( result.output["markdown"].asString().find( "状态: Succeeded" ) != std::string::npos );
  CHECK( result.output["valid"].asBool() );
}

TEST_CASE( "run-scoped mode renders a failed step's typed error",
           "[explain][agent_tool][evidence]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance;
  setExplainGuidanceDirectory( guidance.path.string() );

  const EvidenceDir evidenceDir =
    writeProvenanceRun( QStringLiteral( "run-7" ), QStringLiteral( "n1" ),
                        sicnu::workflow::ExecutionState::Failed,
                        QStringLiteral( "gdal translate failed" ), QString(), 12 );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";
  input["runId"] = "run-7";
  input["provenanceDirectory"] = evidenceDir.path.string();

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  CHECK( result.output["explanation"]["execution"]["status"].asString() == "Failed" );
  CHECK( result.output["explanation"]["execution"]["errorMessage"].asString()
         == "gdal translate failed" );
}

TEST_CASE( "an unknown run stays honestly unknown (no execution, no refusal)",
           "[explain][agent_tool][evidence]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance;
  setExplainGuidanceDirectory( guidance.path.string() );

  const EvidenceDir evidenceDir =
    writeProvenanceRun( QStringLiteral( "run-42" ), QStringLiteral( "n1" ),
                        sicnu::workflow::ExecutionState::Succeeded, QString(),
                        QStringLiteral( "sha256full:abc123" ), 100 );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";
  input["runId"] = "run-missing";
  input["provenanceDirectory"] = evidenceDir.path.string();

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  CHECK_FALSE( result.output["explanation"].isMember( "execution" ) );
  CHECK( result.output["evidenceProblems"].empty() );
}

TEST_CASE( "a tampered provenance record surfaces typed load problems, never a status",
           "[explain][agent_tool][evidence][hostile]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance;
  setExplainGuidanceDirectory( guidance.path.string() );

  EvidenceDir evidenceDir;
  evidenceDir.write( "provenance_run-9.json", std::string( "{ not json " ) );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";
  input["runId"] = "run-9";
  input["provenanceDirectory"] = evidenceDir.path.string();

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  CHECK( hasEvidenceProblem( result.output, "parse_failed" ) );
  CHECK_FALSE( result.output["explanation"].isMember( "execution" ) );
}

TEST_CASE( "a lone runId or provenanceDirectory is refused instead of partially interpreted",
           "[explain][agent_tool][evidence]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance;
  setExplainGuidanceDirectory( guidance.path.string() );

  const EvidenceDir evidenceDir =
    writeProvenanceRun( QStringLiteral( "run-42" ), QStringLiteral( "n1" ),
                        sicnu::workflow::ExecutionState::Succeeded, QString(),
                        QStringLiteral( "sha256full:abc123" ), 100 );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value base;
  base["mode"] = "workflow_document";
  base["document"] = documentJson();
  base["nodeId"] = "n1";

  Json::Value loneRunId = base;
  loneRunId["runId"] = "run-42";
  const SpatialToolResult noDir = tool->execute( loneRunId );
  CHECK( !noDir.success );
  CHECK( noDir.errorCode == "INVALID_PARAMETER" );

  Json::Value loneDirectory = base;
  loneDirectory["provenanceDirectory"] = evidenceDir.path.string();
  const SpatialToolResult noRun = tool->execute( loneDirectory );
  CHECK( !noRun.success );
  CHECK( noRun.errorCode == "INVALID_PARAMETER" );
}

TEST_CASE( "plan-only responses keep their exact shape (no evidenceProblems member)",
           "[explain][agent_tool][evidence]" )
{
  sicnu::operators::rs::initBuiltinRsOperators();
  TempDir guidance;
  setExplainGuidanceDirectory( guidance.path.string() );

  const SpatialToolPtr tool = createExplainStepTool();
  Json::Value input;
  input["mode"] = "workflow_document";
  input["document"] = documentJson();
  input["nodeId"] = "n1";

  const SpatialToolResult result = tool->execute( input );
  REQUIRE( result.success );
  CHECK_FALSE( result.output.isMember( "evidenceProblems" ) );
  CHECK_FALSE( result.output["explanation"].isMember( "execution" ) );
  CHECK( result.output["budget"]["truncated"].asBool() == false );
  // The markdown contract holds on the plan-only path.
  CHECK( result.output.isMember( "markdown" ) );
}
