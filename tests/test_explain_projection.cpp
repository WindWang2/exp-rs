// tests/test_explain_projection.cpp
//
// RS14-15 Slice E coverage: workflow representations normalize into
// ExplanationRequest through the adapters layer.
//   - D17 designer: WorkflowDocument (parsed with the real WorkflowIR 2.0
//     reader) + node id → request; ports project 1:1; the radiometric
//     vocabulary is pinned to the explain state vocabulary on both sides.
//   - Engine 2.0: WorkflowDefinition/StepDef → request; step kinds map onto
//     the closed vocabulary; steps carry no typed ports and stay empty.
//   - Execution evidence: real provenance_<runId>.json envelopes written by
//     ProvenanceGraph::toJson and read back through the strict reader.
// Every unavailable/unknown answer is honest: unknown node ids answer
// nullopt, unknown state tokens degrade to "not declared" with a typed
// problem, malformed provenance files are typed refusals.
#include <catch2/catch_test_macros.hpp>

#include "explain/adapters/provenance_file_evidence.h"
#include "explain/adapters/workflow_projection.h"
#include "explain/state_vocabulary.h"
#include "workflow/workflow_ir_v2.h"
#include "workflow/workflow_provenance.h"
#include "workflow/workflow_types.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define SICNU_TEST_GETPID ::_getpid
#else
#include <unistd.h>
#define SICNU_TEST_GETPID ::getpid
#endif

using namespace sicnu::explain;
using namespace sicnu::explain::adapters;
using namespace sicnu::workflow;

namespace
{

QJsonObject documentJson( const QStringList &nodeIds = { QStringLiteral( "n1" ) } )
{
  QJsonArray nodes;
  for ( const QString &id : nodeIds )
  {
    QJsonObject node;
    node["nodeId"] = id;
    node["operatorId"] = "rs:radiometric_calibration";
    node["displayName"] = "Calibrate " + id;
    QJsonObject parameters;
    parameters["unit"] = "radiance";
    node["parameters"] = parameters;

    auto makePort = []( const QString &name, const QString &state, double resX, int bands ) {
      QJsonObject port;
      port["portName"] = name;
      port["dataType"] = "Raster";
      port["crs"] = "EPSG:32649";
      port["radiometricState"] = state;
      port["resolutionX"] = resX;
      port["resolutionY"] = resX;
      port["bandCount"] = bands;
      port["isRequired"] = true;
      return port;
    };
    QJsonArray inputPorts { makePort( "input", "DN", 30.0, 6 ) };
    QJsonArray outputPorts { makePort( "output", "Radiance", 30.0, 6 ) };
    node["inputPorts"] = inputPorts;
    node["outputPorts"] = outputPorts;
    node["canvasPosition"] = QJsonObject { { "x", 100.0 }, { "y", 200.0 } };
    nodes.append( node );
  }

  QJsonObject document;
  document["version"] = "2.1";
  document["workflowId"] = "wf-d17";
  document["name"] = "DN to radiance";
  document["description"] = "";
  document["metadata"] = QJsonObject();
  document["nodes"] = nodes;
  document["edges"] = QJsonArray();
  return document;
}

WorkflowDocument parsedDocument( const QStringList &nodeIds = { QStringLiteral( "n1" ) } )
{
  const QJsonDocument parsed = QJsonDocument::fromJson( QJsonDocument( documentJson( nodeIds ) ).toJson() );
  const Result<WorkflowDocument> result = WorkflowIR::fromJson( parsed.object() );
  REQUIRE( result.isSuccess() );
  return result.value();
}

bool hasProblem( const std::vector<ProjectionProblem> &problems, const std::string &code )
{
  for ( const ProjectionProblem &problem : problems )
    if ( problem.code == code )
      return true;
  return false;
}

struct EvidenceDir
{
  std::filesystem::path path;
  EvidenceDir()
  {
    path = std::filesystem::temp_directory_path()
           / ( "explain_projection_evidence_" + std::to_string( SICNU_TEST_GETPID() ) );
    std::filesystem::create_directories( path );
  }
  ~EvidenceDir() { std::filesystem::remove_all( path ); }
  void write( const std::string &name, const QByteArray &bytes ) const
  {
    std::ofstream out( path / name, std::ios::binary );
    out << bytes.toStdString();
  }
};

} // namespace

TEST_CASE( "D17 nodes project 1:1 onto explanation requests", "[explain][projection][d17]" )
{
  const WorkflowDocument document = parsedDocument();
  std::vector<ProjectionProblem> problems;
  const std::optional<ExplanationRequest> request = projectWorkflowDocument( document, "n1", problems );

  REQUIRE( request.has_value() );
  CHECK( problems.empty() );
  CHECK( request->workflowKind == WorkflowKindD17Designer );
  CHECK( request->workflowId == "wf-d17" );
  CHECK( request->workflowTitle == "DN to radiance" );
  CHECK( request->stepId == "n1" );
  CHECK( request->stepTitle == "Calibrate n1" );
  CHECK( request->stepKind == StepKindOperator );
  CHECK( request->operatorId == "rs:radiometric_calibration" );
  CHECK( request->parameters["unit"].asString() == "radiance" );

  REQUIRE( request->inputPorts.size() == 1 );
  REQUIRE( request->outputPorts.size() == 1 );
  CHECK( request->inputPorts.front().portName == "input" );
  CHECK( request->inputPorts.front().stateToken == "DN" );
  CHECK( request->inputPorts.front().crs == "EPSG:32649" );
  CHECK( request->inputPorts.front().resolutionX.has_value() );
  CHECK( *request->inputPorts.front().resolutionX == 30.0 );
  CHECK( request->inputPorts.front().bandCount.has_value() );
  CHECK( *request->inputPorts.front().bandCount == 6 );
  CHECK( request->outputPorts.front().stateToken == "Radiance" );
}

TEST_CASE( "unknown nodes and missing operators are typed refusals",
           "[explain][projection][d17][hallucination]" )
{
  const WorkflowDocument document = parsedDocument();

  std::vector<ProjectionProblem> problems;
  CHECK( !projectWorkflowDocument( document, "ghost", problems ).has_value() );
  CHECK( hasProblem( problems, "node_unknown" ) );
}

TEST_CASE( "state tokens outside the shared vocabulary degrade to not-declared",
           "[explain][projection][d17][drift]" )
{
  // The D17 vocabulary and the explain state vocabulary must stay aligned:
  // every projected D17 token is a state-vocabulary member.
  for ( const QString &token :
        { QStringLiteral( "DN" ), QStringLiteral( "Radiance" ), QStringLiteral( "TOA" ),
          QStringLiteral( "BOA" ), QStringLiteral( "Index" ), QStringLiteral( "Mask" ),
          QStringLiteral( "*" ), QStringLiteral( "None" ) } )
  {
    CHECK( isKnownStateToken( token.toStdString() ) );
  }

  QJsonObject document = documentJson();
  QJsonObject node = document["nodes"].toArray().at(0).toObject();
  QJsonObject port = node["outputPorts"].toArray().at(0).toObject();
  port["radiometricState"] = "PLANKTON"; // not in the vocabulary
  node["outputPorts"] = QJsonArray { port };
  document["nodes"] = QJsonArray { node };
  const QJsonDocument parsed = QJsonDocument::fromJson( QJsonDocument( document ).toJson() );
  const Result<WorkflowDocument> result = WorkflowIR::fromJson( parsed.object() );
  REQUIRE( result.isSuccess() );

  std::vector<ProjectionProblem> problems;
  const std::optional<ExplanationRequest> request = projectWorkflowDocument( result.value(), "n1", problems );
  REQUIRE( request.has_value() );
  CHECK( hasProblem( problems, "state_token_unknown" ) );
  CHECK( request->outputPorts.front().stateToken.empty() );
}

TEST_CASE( "Engine 2.0 steps project onto the closed step-kind vocabulary",
           "[explain][projection][engine2]" )
{
  WorkflowDefinition definition;
  definition.id = "guided-1";
  definition.title = "Guided calibration";

  StepDef operatorStep;
  operatorStep.id = "s1";
  operatorStep.title = "Calibrate DN";
  operatorStep.kind = StepKind::Operator;
  operatorStep.operatorId = "rs:radiometric_calibration";
  operatorStep.params = Json::Value( Json::objectValue );
  operatorStep.params["unit"] = "radiance";

  StepDef reviewStep;
  reviewStep.id = "s2";
  reviewStep.title = "Check output";
  reviewStep.kind = StepKind::Review;

  definition.steps = { operatorStep, reviewStep };

  std::vector<ProjectionProblem> problems;
  const std::optional<ExplanationRequest> operatorRequest =
    projectEngine2Step( definition, "s1", "Succeeded", problems );
  REQUIRE( operatorRequest.has_value() );
  CHECK( problems.empty() );
  CHECK( operatorRequest->workflowKind == WorkflowKindGuidedPipeline );
  CHECK( operatorRequest->workflowId == "guided-1" );
  CHECK( operatorRequest->stepKind == StepKindOperator );
  CHECK( operatorRequest->operatorId == "rs:radiometric_calibration" );
  CHECK( operatorRequest->parameters["unit"].asString() == "radiance" );
  CHECK( operatorRequest->executionStatus == "Succeeded" );
  // Engine 2.0 steps declare no typed ports: facts stay unknown, not invented.
  CHECK( operatorRequest->inputPorts.empty() );
  CHECK( operatorRequest->outputPorts.empty() );

  const std::optional<ExplanationRequest> reviewRequest =
    projectEngine2Step( definition, "s2", "", problems );
  REQUIRE( reviewRequest.has_value() );
  CHECK( reviewRequest->stepKind == StepKindReview );
  CHECK( reviewRequest->operatorId.empty() );
  CHECK( !reviewRequest->isOperatorStep() );

  std::vector<ProjectionProblem> unknownProblems;
  CHECK( !projectEngine2Step( definition, "ghost", "", unknownProblems ).has_value() );
  CHECK( hasProblem( unknownProblems, "node_unknown" ) );
}

TEST_CASE( "provenance records project onto execution evidence with machine links",
           "[explain][projection][provenance]" )
{
  // Build a real record through the real writer (fromRunState + toJson).
  const WorkflowDocument document = parsedDocument();
  QHash<QString, NodeStatusSnapshot> statuses;
  NodeStatusSnapshot ok;
  ok.nodeId = "n1";
  ok.state = ExecutionState::Succeeded;
  ok.elapsedMs = 4321;
  ok.isCacheHit = false;
  ok.outputArtifactPath = "/tmp/out/calibrated.tif";
  ok.artifactFingerprint = "sha256full:abc123";
  ok.lineageSignature = "sig-1";
  statuses.insert( QStringLiteral( "n1" ), ok );

  const ProvenanceGraph graph =
    ProvenanceGraph::fromRunState( QStringLiteral( "run-42" ), document, statuses,
                                   QStringLiteral( "plan-sig" ) );
  const QJsonObject recordJson = graph.toJson();

  EvidenceDir dir;
  dir.write( "provenance_run-42.json", QJsonDocument( recordJson ).toJson() );

  std::vector<EvidenceLoadProblem> problems;
  const std::unique_ptr<ProvenanceFileEvidence> evidence =
    ProvenanceFileEvidence::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( evidence != nullptr );
  CHECK( evidence->runCount() == 1 );
  CHECK( problems.empty() );

  const std::optional<StepEvidence> step = evidence->evidenceFor( "run-42", "n1" );
  REQUIRE( step.has_value() );
  CHECK( step->status == "Succeeded" );
  REQUIRE( step->elapsedMs.has_value() );
  CHECK( *step->elapsedMs == 4321 );
  REQUIRE( step->cacheHit.has_value() );
  CHECK( *step->cacheHit == false );
  CHECK( step->artifactPath == "/tmp/out/calibrated.tif" );
  CHECK( step->artifactDigest == "sha256full:abc123" );
  // The record has no wall-clock stamps: unknown stays unknown.
  CHECK( step->startedUtc.empty() );
  CHECK( step->endedUtc.empty() );
  // The evidence cites its record via the pinned machine-kind grammar.
  REQUIRE( step->links.size() == 1 );
  CHECK( isMachineEvidenceKind( step->links.front().kind ) );
  CHECK( step->links.front().kind == EvidenceProvenance );
  CHECK( step->links.front().target == "provenance:run-42#node:n1" );
  CHECK( isValidEvidenceTarget( step->links.front().kind, step->links.front().target ) );

  // Unknown run and unknown step answer nullopt (never placeholder success).
  CHECK( !evidence->evidenceFor( "run-missing", "n1" ).has_value() );
  CHECK( !evidence->evidenceFor( "run-42", "ghost" ).has_value() );
}

TEST_CASE( "failed executions carry typed error messages, cache hits their reuse",
           "[explain][projection][provenance]" )
{
  // Two nodes so both snapshots map onto the document.
  const WorkflowDocument document = parsedDocument( { QStringLiteral( "n1" ), QStringLiteral( "n2" ) } );
  QHash<QString, NodeStatusSnapshot> statuses;

  NodeStatusSnapshot failed;
  failed.nodeId = "n1";
  failed.state = ExecutionState::Failed;
  failed.errorMessage = "gdal translate failed";
  failed.elapsedMs = 12;
  statuses.insert( QStringLiteral( "n1" ), failed );

  NodeStatusSnapshot reused;
  reused.nodeId = "n2";
  reused.state = ExecutionState::Succeeded;
  reused.isCacheHit = true;
  reused.outputArtifactPath = "/tmp/cache/other.tif";
  reused.artifactFingerprint = "sha256full:feed";
  statuses.insert( QStringLiteral( "n2" ), reused );

  const ProvenanceGraph graph =
    ProvenanceGraph::fromRunState( QStringLiteral( "run-7" ), document, statuses, QStringLiteral( "sig" ) );

  EvidenceDir dir;
  dir.write( "provenance_run-7.json", QJsonDocument( graph.toJson() ).toJson() );

  std::vector<EvidenceLoadProblem> problems;
  const std::unique_ptr<ProvenanceFileEvidence> evidence =
    ProvenanceFileEvidence::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( evidence != nullptr );

  const std::optional<StepEvidence> failure = evidence->evidenceFor( "run-7", "n1" );
  REQUIRE( failure.has_value() );
  CHECK( failure->status == "Failed" );
  CHECK( failure->errorMessage == "gdal translate failed" );
  CHECK( failure->artifactPath.empty() );

  const std::optional<StepEvidence> cache = evidence->evidenceFor( "run-7", "n2" );
  REQUIRE( cache.has_value() );
  CHECK( cache->artifactPath == "/tmp/cache/other.tif" );
  CHECK( cache->artifactDigest == "sha256full:feed" );
}

TEST_CASE( "malformed provenance files are typed refusals, not empty success",
           "[explain][projection][provenance][hostile]" )
{
  EvidenceDir dir;
  dir.write( "provenance_broken.json", QByteArray( "{ not json " ) );
  dir.write( "provenance_wrong.json", QByteArray( "{\"kind\": \"not_provenance\"}" ) );

  std::vector<EvidenceLoadProblem> problems;
  const std::unique_ptr<ProvenanceFileEvidence> evidence =
    ProvenanceFileEvidence::loadFromDirectory( dir.path.string(), problems );
  REQUIRE( evidence != nullptr );
  CHECK( evidence->runCount() == 0 );
  bool parseFailed = false;
  bool envelopeRefused = false;
  for ( const EvidenceLoadProblem &problem : problems )
  {
    parseFailed = parseFailed || problem.code == "parse_failed";
    envelopeRefused = envelopeRefused || problem.code == "envelope_refused";
  }
  CHECK( parseFailed );
  CHECK( envelopeRefused );

  std::vector<EvidenceLoadProblem> missingProblems;
  const std::unique_ptr<ProvenanceFileEvidence> missing = ProvenanceFileEvidence::loadFromDirectory(
    "/definitely/not/a/real/dir/provenance", missingProblems );
  REQUIRE( missing != nullptr );
  CHECK( missing->runCount() == 0 );
  CHECK( !missingProblems.empty() );
  CHECK( missingProblems.front().code == "directory_missing" );
}
