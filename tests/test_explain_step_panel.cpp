// tests/test_explain_step_panel.cpp
//
// Explainable Workflow (RS14-15 R3): the why-this-step render surface —
// StepExplanationPanel (deterministic view-model rendering) and
// StepExplanationSection (inspector seam + run-scoped evidence lifecycle) —
// exercised offscreen against the REAL sources: the live operator registry,
// a fixture authored-guidance corpus and real provenance_<runId>.json
// records written by ProvenanceGraph and read through ProvenanceFileEvidence.
//
// Pinned honesty contracts:
//   - plan-only / success / failed / tampered evidence produce predictable,
//     DIFFERENT renders; absent evidence renders 执行情况未知, never a
//     fabricated status and never synthesized timestamps;
//   - guidance removal removes authored lines but never machine facts;
//   - an authored contradiction surfaces as problem + trust note and cannot
//     override port facts in the rendered state section;
//   - badges come from FactProvenance, never from the text itself;
//   - project/run switches and the session boundary leave no stale content;
//   - reopen / identical snapshots re-render deterministically with no
//     accumulation and no extra population.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/inspector_host.h"
#include "app/workbench/step_explanation_panel.h"
#include "app/workbench/step_explanation_section.h"
#include "explain/adapters/provenance_file_evidence.h"
#include "explain/adapters/registry_operator_knowledge.h"
#include "explain/adapters/workflow_projection.h"
#include "explain/guidance_store.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "workflow/pipeline_run_coordinator.h"
#include "workflow/workflow_ir_v2.h"
#include "workflow/workflow_provenance.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTabWidget>

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

using namespace sicnu::app;
using namespace sicnu::explain;
using namespace sicnu::explain::adapters;
using namespace sicnu::workflow;

namespace
{

QApplication *ensureApp()
{
    static int fake_argc = 1;
    static char fake_argv0[] = "test_explain_step_panel";
    static char *fake_argv[] = { fake_argv0, nullptr };
    static QApplication app( fake_argc, fake_argv );
    return &app;
}

struct TempDir
{
    std::filesystem::path path;
    TempDir( const std::string &tag )
    {
        path = std::filesystem::temp_directory_path()
               / ( tag + "_" + std::to_string( SICNU_TEST_GETPID() ) );
        std::filesystem::create_directories( path );
    }
    ~TempDir() { std::filesystem::remove_all( path ); }
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

// A minimal guidance corpus entry naming an existing schema parameter.
std::string guidanceWithRationale( const std::string &operatorId )
{
    return guidanceWith( operatorId,
                         "  \"parameterRationale\": [ { \"parameter\": \"unit\", "
                         "\"rationale\": \"输出物理量决定后续可比性\" } ]" );
}

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

        auto makePort = []( const QString &name, const QString &state ) {
            QJsonObject port;
            port["portName"] = name;
            port["dataType"] = "Raster";
            port["crs"] = "EPSG:32649";
            port["radiometricState"] = state;
            port["resolutionX"] = 30.0;
            port["resolutionY"] = 30.0;
            port["bandCount"] = 6;
            port["isRequired"] = true;
            return port;
        };
        QJsonArray inputPorts { makePort( "input", "DN" ) };
        QJsonArray outputPorts { makePort( "output", "Radiance" ) };
        node["inputPorts"] = inputPorts;
        node["outputPorts"] = outputPorts;
        node["canvasPosition"] = QJsonObject { { "x", 100.0 }, { "y", 200.0 } };
        nodes.append( node );
    }

    QJsonObject document;
    document["version"] = "2.1";
    document["workflowId"] = "wf-panel";
    document["name"] = "DN to radiance";
    document["metadata"] = QJsonObject();
    document["nodes"] = nodes;
    document["edges"] = QJsonArray();
    return document;
}

WorkflowDocument parsedDocument( const QStringList &nodeIds = { QStringLiteral( "n1" ) } )
{
    const QJsonDocument parsed = QJsonDocument( documentJson( nodeIds ) );
    const Result<WorkflowDocument> result = WorkflowIR::fromJson( parsed.object() );
    REQUIRE( result.isSuccess() );
    return result.value();
}

// Write a real provenance record through the real writer (fromRunState +
// toJson) and load it through the real adapter — the same chain the
// PipelineRunCoordinator → ProvenanceFileEvidence production path uses.
std::unique_ptr<ProvenanceFileEvidence> writeAndLoadRun(
    TempDir &dir, const QString &runId, const ExecutionState state, const QString &errorMessage,
    const QString &digest, qint64 elapsedMs )
{
    const WorkflowDocument document = parsedDocument();
    QHash<QString, NodeStatusSnapshot> statuses;
    NodeStatusSnapshot snapshot;
    snapshot.nodeId = "n1";
    snapshot.state = state;
    snapshot.elapsedMs = elapsedMs;
    snapshot.errorMessage = errorMessage;
    if ( !digest.isEmpty() )
    {
        snapshot.outputArtifactPath = "/tmp/out/calibrated.tif";
        snapshot.artifactFingerprint = digest;
    }
    statuses.insert( QStringLiteral( "n1" ), snapshot );

    const ProvenanceGraph graph =
        ProvenanceGraph::fromRunState( runId, document, statuses, QStringLiteral( "sig" ) );
    dir.write( "provenance_" + runId.toStdString() + ".json",
               QJsonDocument( graph.toJson() ).toJson( QJsonDocument::Compact ).toStdString() );

    std::vector<EvidenceLoadProblem> problems;
    auto evidence = ProvenanceFileEvidence::loadFromDirectory( dir.path.string(), problems );
    INFO( "load problems must be empty for a well-formed fixture" );
    REQUIRE( evidence != nullptr );
    REQUIRE( problems.empty() );
    return evidence;
}

ExplanationRequest nodeRequest( const WorkflowDocument &document, const QString &nodeId,
                                const std::string &runId = std::string() )
{
    std::vector<ProjectionProblem> problems;
    const std::optional<ExplanationRequest> request =
        projectWorkflowDocument( document, nodeId.toStdString(), problems );
    REQUIRE( request.has_value() );
    ExplanationRequest scoped = *request;
    scoped.runId = runId;
    return scoped;
}

StepExplanationPanel::KnowledgeProvider liveKnowledge()
{
    sicnu::operators::rs::initBuiltinRsOperators();
    static RegistryOperatorKnowledge knowledge( sicnu::operators::RSOperatorRegistry::instance() );
    return []() -> const IOperatorKnowledge * { return &knowledge; };
}

int countOccurrences( const QString &text, const QString &needle )
{
    int count = 0;
    for ( int pos = text.indexOf( needle ); pos >= 0; pos = text.indexOf( needle, pos + 1 ) )
        ++count;
    return count;
}

} // namespace

TEST_CASE( "plan-only: machine facts and authored guidance render, execution stays honestly unknown",
           "[explain][panel]" )
{
    ensureApp();
    TempDir dir( "explain_panel_plan" );
    dir.write( "rs_radiometric_calibration.json", guidanceWithRationale( "rs:radiometric_calibration" ) );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    int shown = 0;
    QObject::connect( &panel, &StepExplanationPanel::explanationShown,
                      [&]() { ++shown; } );

    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1" ) );

    CHECK( panel.hasExplanation() );
    CHECK( panel.requestGeneration() == 1 );
    CHECK( shown == 1 );
    // Authored guidance badge + inferred port-fact synthesis. (The registry
    // adapter intentionally leaves the operator's machine purpose empty, so
    // 系统事实 appears only on execution lines — see the evidence tests.)
    CHECK( panel.renderedText().contains( "[编写指引]" ) );
    CHECK( panel.renderedText().contains( "[推断]" ) );
    CHECK_FALSE( panel.renderedText().contains( "[系统事实]" ) );
    // State synthesis from declared ports: DN -> Radiance.
    CHECK( panel.renderedText().contains( "radiometric_state: DN → Radiance" ) );
    // Plan-only honesty: no status, no invented timestamps.
    CHECK( panel.renderedText().contains( QStringLiteral( "执行情况未知（计划模式" ) ) );
    CHECK_FALSE( panel.renderedText().contains( "状态: " ) );
    CHECK_FALSE( panel.renderedText().contains( "UTC" ) );
    // Deterministic markdown from the same view model.
    CHECK_FALSE( panel.markdown().isEmpty() );
}

TEST_CASE( "success-run evidence renders the real status, digest and provenance link",
           "[explain][panel][evidence]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_panel_ok" );
    const auto evidence = writeAndLoadRun( evidenceDir, QStringLiteral( "run-42" ),
                                           ExecutionState::Succeeded, QString(),
                                           QStringLiteral( "sha256full:abc123" ), 4321 );

    TempDir dir( "explain_panel_ok_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                [&]() -> const IExecutionEvidence * { return evidence.get(); } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1", "run-42" ) );

    CHECK( panel.hasExplanation() );
    // Execution facts are the one machine-fact (系统事实) line a record can
    // produce — authored text can never carry this badge.
    CHECK( panel.renderedText().contains( "[系统事实]" ) );
    CHECK( panel.renderedText().contains( QStringLiteral( "状态: Succeeded，耗时 4321 ms" ) ) );
    CHECK( panel.renderedText().contains( "sha256full:abc123" ) );
    CHECK( panel.renderedText().contains( "provenance:run-42#node:n1" ) );
    // The record carries no wall-clock stamps — absence stays absence.
    CHECK_FALSE( panel.renderedText().contains( "UTC" ) );
}

TEST_CASE( "failed-run evidence renders the typed error, never success language",
           "[explain][panel][evidence]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_panel_fail" );
    const auto evidence = writeAndLoadRun( evidenceDir, QStringLiteral( "run-7" ),
                                           ExecutionState::Failed, QStringLiteral( "gdal translate failed" ),
                                           QString(), 12 );

    TempDir dir( "explain_panel_fail_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                [&]() -> const IExecutionEvidence * { return evidence.get(); } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1", "run-7" ) );

    CHECK( panel.renderedText().contains( QStringLiteral( "状态: Failed" ) ) );
    CHECK( panel.renderedText().contains( QStringLiteral( "错误: gdal translate failed" ) ) );
    CHECK_FALSE( panel.renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );
    CHECK_FALSE( panel.renderedText().contains( QStringLiteral( "缓存命中" ) ) );
}

TEST_CASE( "unknown run renders honest unknown even with a loaded evidence source",
           "[explain][panel][evidence]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_panel_missing_run" );
    const auto evidence = writeAndLoadRun( evidenceDir, QStringLiteral( "run-42" ),
                                           ExecutionState::Succeeded, QString(),
                                           QStringLiteral( "sha256full:abc123" ), 100 );

    TempDir dir( "explain_panel_missing_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                [&]() -> const IExecutionEvidence * { return evidence.get(); } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1", "run-missing" ) );

    CHECK( panel.renderedText().contains( QStringLiteral( "执行情况未知（运行 run-missing 中没有此步骤的执行证据）" ) ) );
    CHECK_FALSE( panel.renderedText().contains( "sha256full:abc123" ) );
}

TEST_CASE( "guidance removal removes authored lines but machine facts survive",
           "[explain][panel][provenance-of-text]" )
{
    ensureApp();
    TempDir dir( "explain_panel_guidance_switch" );
    dir.write( "rs_radiometric_calibration.json", guidanceWithRationale( "rs:radiometric_calibration" ) );
    TempDir emptyDir( "explain_panel_guidance_empty" ); // a corpus directory without entries
    std::vector<GuidanceLoadProblem> problems;
    const auto withGuidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );
    const auto emptyStore = GuidanceStore::loadFromDirectory( emptyDir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    const auto request = nodeRequest( document, "n1" );

    // With the corpus: machine fact first, authored narrative second.
    const auto guidance = withGuidance.get();
    StepExplanationPanel panel( liveKnowledge(),
                                [&]() -> const IAuthoredGuidance * { return guidance; },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    panel.showStep( request );
    CHECK( panel.renderedText().contains( "[推断]" ) );
    CHECK( panel.renderedText().contains( "[编写指引]" ) );
    CHECK( panel.renderedText().contains( QStringLiteral( "教学：标定算子把 DN 转为辐亮度" ) ) );
    CHECK( panel.renderedText().contains( "radiometric_state: DN → Radiance" ) );

    // The corpus disappears (project switch / course change): the provider
    // now resolves to an empty store. The machine-derived port facts (the
    // DN → Radiance synthesis) stay; the authored narrative goes; the honest
    // no-guidance note appears.
    const auto emptyStorePtr = emptyStore.get();
    StepExplanationPanel panelAfter( liveKnowledge(),
                                     [&]() -> const IAuthoredGuidance * { return emptyStorePtr; },
                                     []() -> const IExecutionEvidence * { return nullptr; } );
    panelAfter.showStep( request );
    CHECK( panelAfter.renderedText().contains( "radiometric_state: DN → Radiance" ) );
    CHECK( panelAfter.renderedText().contains( "[推断]" ) );
    CHECK_FALSE( panelAfter.renderedText().contains( "[编写指引]" ) );
    CHECK( panelAfter.renderedText().contains( QStringLiteral( "本算子暂无编写指引" ) ) );
    CHECK_FALSE( panelAfter.renderedText().contains( QStringLiteral( "教学：标定算子把 DN 转为辐亮度" ) ) );
}

TEST_CASE( "authored contradiction cannot override port facts in the render",
           "[explain][panel][conflict]" )
{
    ensureApp();
    TempDir dir( "explain_panel_conflict" );
    dir.write( "rs_radiometric_calibration.json",
               guidanceWith( "rs:radiometric_calibration",
                             "  \"stateNarrative\": { \"before\": \"BOA\", \"after\": \"BOA\" }" ) );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1" ) );

    // Port facts kept; authored narrative only surfaces as problem + note.
    CHECK( panel.renderedText().contains( "radiometric_state: DN → Radiance" ) );
    CHECK_FALSE( panel.renderedText().contains( "BOA → BOA" ) );
    CHECK( panel.renderedText().contains( "state_contradiction" ) );
    CHECK( panel.renderedText().contains( QStringLiteral( "以端口声明为准" ) ) );
}

TEST_CASE( "teaching text cannot impersonate a system fact badge",
           "[explain][panel][no-leak]" )
{
    ensureApp();
    // The authored purpose TEXT claims to be a 系统事实 — the badge must
    // still come from FactProvenance (编写指引), never from the content.
    TempDir dir( "explain_panel_impersonation" );
    dir.write( "rs_radiometric_calibration.json",
               guidanceWith( "rs:radiometric_calibration",
                             "  \"purpose\": \"系统事实：DN 是辐亮度（伪造声明，不得升级）\"" ) );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1" ) );

    CHECK( panel.renderedText().contains( QStringLiteral( "[编写指引] 系统事实：DN 是辐亮度" ) ) );
}

TEST_CASE( "identical requests re-render deterministically without accumulation",
           "[explain][panel][lifecycle]" )
{
    ensureApp();
    TempDir dir( "explain_panel_determinism" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    const WorkflowDocument document = parsedDocument();
    const auto request = nodeRequest( document, "n1" );

    panel.showStep( request );
    const QString first = panel.markdown();
    const QString firstText = panel.renderedText();
    panel.showStep( request );
    CHECK( panel.markdown() == first );
    CHECK( panel.renderedText() == firstText );
    // One render per call — the second render REPLACED the first.
    CHECK( panel.requestGeneration() == 2 );
    CHECK( countOccurrences( panel.renderedText(), QStringLiteral( "执行情况（Execution）" ) ) == 1 );
}

TEST_CASE( "run switch replaces content with no stale artifacts from the previous run",
           "[explain][panel][lifecycle]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_panel_run_switch" );
    const auto evidence = writeAndLoadRun( evidenceDir, QStringLiteral( "run-42" ),
                                           ExecutionState::Succeeded, QString(),
                                           QStringLiteral( "sha256full:abc123" ), 4321 );

    TempDir dir( "explain_panel_switch_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                [&]() -> const IExecutionEvidence * { return evidence.get(); } );
    const WorkflowDocument document = parsedDocument();

    // Run A shown…
    panel.showStep( nodeRequest( document, "n1", "run-42" ) );
    CHECK( panel.renderedText().contains( "sha256full:abc123" ) );

    // …run B arrives: a full synchronous re-render replaces everything.
    panel.showStep( nodeRequest( document, "n1", "run-other" ) );
    CHECK( panel.renderedText().contains( QStringLiteral( "执行情况未知（运行 run-other" ) ) );
    CHECK_FALSE( panel.renderedText().contains( "sha256full:abc123" ) );

    // The session boundary (project close / story boundary) clears content.
    panel.reset();
    CHECK_FALSE( panel.hasExplanation() );
    CHECK( panel.markdown().isEmpty() );
    CHECK( panel.renderedText().contains( QStringLiteral( "未选择需要解释的步骤" ) ) );
    CHECK( panel.requestGeneration() == 3 );
}

TEST_CASE( "unknown operators fail closed with the builder's typed code",
           "[explain][panel]" )
{
    ensureApp();
    TempDir dir( "explain_panel_unknown_op" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );

    ExplanationRequest request = makeOperatorRequest( WorkflowKindAgentPlan, "adhoc", "s1",
                                                      "Ghost", "rs:definitely_not_registered" );
    panel.showStep( request );

    CHECK_FALSE( panel.hasExplanation() );
    CHECK( panel.lastFailureCode() == "operator_unknown" );
    CHECK( panel.renderedText().contains( QStringLiteral( "无法解释此步骤" ) ) );
}

TEST_CASE( "showNote replaces any previous explanation (never mixed content)",
           "[explain][panel]" )
{
    ensureApp();
    TempDir dir( "explain_panel_note" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    StepExplanationPanel panel( liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
                                []() -> const IExecutionEvidence * { return nullptr; } );
    const WorkflowDocument document = parsedDocument();
    panel.showStep( nodeRequest( document, "n1" ) );
    CHECK( panel.hasExplanation() );

    panel.showNote( QStringLiteral( "当前文档中没有节点 ghost。" ) );
    CHECK_FALSE( panel.hasExplanation() );
    CHECK( panel.markdown().isEmpty() );
    CHECK( panel.renderedText() == QStringLiteral( "当前文档中没有节点 ghost。" ) );
}

TEST_CASE( "section supports only pipeline-node selections with a resolvable document",
           "[explain][section]" )
{
    ensureApp();
    TempDir dir( "explain_section_support" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    StepExplanationSection withDoc(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; } );
    StepExplanationSection withoutDoc(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        []() -> std::optional<WorkflowDocument> { return std::nullopt; } );

    SelectionContextSnapshot empty;
    CHECK_FALSE( withDoc.supports( empty ) );
    CHECK_FALSE( withoutDoc.supports( empty ) );

    SelectionContextSnapshot nodeSelected;
    nodeSelected.selectedPipelineNodeId = QStringLiteral( "n1" );
    CHECK( withDoc.supports( nodeSelected ) );
    CHECK_FALSE( withoutDoc.supports( nodeSelected ) );

    SelectionContextSnapshot wrongNode = nodeSelected;
    wrongNode.selectedPipelineNodeId = QStringLiteral( "ghost" );
    // The section still supports the selection (document resolves); the
    // unknown node renders honestly at populate time.
    CHECK( withDoc.supports( wrongNode ) );
}

TEST_CASE( "section populate projects the node and surfaces run evidence lifecycle",
           "[explain][section][evidence]" )
{
    ensureApp();
    // Separate run directories: run B's record must come from ITS OWN
    // directory, so a section that kept run A's adapter cannot fake run B.
    TempDir evidenceDirA( "explain_section_run_a" );
    writeAndLoadRun( evidenceDirA, QStringLiteral( "run-42" ), ExecutionState::Succeeded, QString(),
                     QStringLiteral( "sha256full:abc123" ), 4321 );
    TempDir evidenceDirB( "explain_section_run_b" );
    writeAndLoadRun( evidenceDirB, QStringLiteral( "run-7" ), ExecutionState::Succeeded, QString(),
                     QStringLiteral( "sha256full:feed" ), 7 );

    TempDir dir( "explain_section_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    StepExplanationSection section(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; } );

    // No run attached: plan-only honesty.
    SelectionContextSnapshot snapshot;
    snapshot.selectedPipelineNodeId = QStringLiteral( "n1" );
    section.populate( snapshot );
    REQUIRE( section.panel() != nullptr );
    CHECK( section.panel()->hasExplanation() );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "执行情况未知（计划模式" ) ) );

    // Run A finalizes: its provenance record is attached and served.
    section.attachRunProvenance(
        QString::fromStdString( ( evidenceDirA.path / "provenance_run-42.json" ).string() ) );
    CHECK( section.currentRunId() == "run-42" );
    CHECK( section.evidenceProblemCodes().isEmpty() );
    section.populate( snapshot );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded，耗时 4321 ms" ) ) );
    CHECK( section.panel()->renderedText().contains( "sha256full:abc123" ) );

    // Run B replaces run A (its own directory): run A's artifact identity
    // must not survive the re-render.
    section.attachRunProvenance(
        QString::fromStdString( ( evidenceDirB.path / "provenance_run-7.json" ).string() ) );
    section.populate( snapshot );
    CHECK( section.currentRunId() == "run-7" );
    CHECK( section.panel()->renderedText().contains( "sha256full:feed" ) );
    CHECK_FALSE( section.panel()->renderedText().contains( "abc123" ) );

    // A new run starts: the previous run's evidence is dropped immediately.
    section.clearRunEvidence();
    section.populate( snapshot );
    CHECK( section.currentRunId().isEmpty() );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "执行情况未知（计划模式" ) ) );
    CHECK_FALSE( section.panel()->renderedText().contains( "sha256full:feed" ) );
}

TEST_CASE( "identity re-announce never wipes live evidence; a real identity change does",
           "[explain][section][lifecycle]" )
{
    ensureApp();
    // Shell flow pinned at the section seam: the dock re-announces identity
    // on EVERY canvas interaction (node clicks, Run clicks). If a
    // re-announce cleared run evidence, the natural flow select→run→click
    // would always degrade to plan-only and the run-scoped surface would be
    // unreachable in the app.
    TempDir evidenceDir( "explain_section_identity" );
    writeAndLoadRun( evidenceDir, QStringLiteral( "run-42" ), ExecutionState::Succeeded, QString(),
                     QStringLiteral( "sha256full:abc123" ), 4321 );

    TempDir dir( "explain_section_identity_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    StepExplanationSection section(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; } );

    // Dock creation seeds the identity (first announce is a real change).
    CHECK( section.noteWorkflowIdentity( QStringLiteral( "wf-1" ), QStringLiteral( "fp-1" ) ) );

    // Run finished → evidence attached; user clicks the SAME document's node
    // repeatedly — each click re-announces the same identity.
    section.attachRunProvenance(
        QString::fromStdString( ( evidenceDir.path / "provenance_run-42.json" ).string() ) );
    SelectionContextSnapshot snapshot;
    snapshot.selectedPipelineNodeId = QStringLiteral( "n1" );
    section.populate( snapshot );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );

    for ( int click = 0; click < 3; ++click )
        CHECK_FALSE( section.noteWorkflowIdentity( QStringLiteral( "wf-1" ), QStringLiteral( "fp-1" ) ) );
    section.populate( snapshot );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );
    CHECK( section.panel()->renderedText().contains( "sha256full:abc123" ) );

    // A real change (New document / LabSpec lift / content edit) clears.
    CHECK( section.noteWorkflowIdentity( QStringLiteral( "wf-2" ), QStringLiteral( "fp-2" ) ) );
    section.populate( snapshot );
    CHECK_FALSE( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );
    CHECK_FALSE( section.panel()->renderedText().contains( "sha256full:abc123" ) );
    CHECK( section.currentRunId().isEmpty() );
}

TEST_CASE( "tampered provenance records surface as typed problems and stay unknown",
           "[explain][section][hostile]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_section_tampered" );
    evidenceDir.write( "provenance_run-9.json", std::string( "{ not json " ) );

    // A good record that later gets corrupted in place (retry/tamper on the
    // same run id).
    TempDir goodDir( "explain_section_tampered_good" );
    writeAndLoadRun( goodDir, QStringLiteral( "run-42" ), ExecutionState::Succeeded, QString(),
                     QStringLiteral( "sha256full:abc123" ), 4321 );

    TempDir dir( "explain_section_tampered_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    StepExplanationSection section(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; } );

    // Serve the good record first.
    section.attachRunProvenance(
        QString::fromStdString( ( goodDir.path / "provenance_run-42.json" ).string() ) );
    SelectionContextSnapshot snapshot42;
    snapshot42.selectedPipelineNodeId = QStringLiteral( "n1" );
    section.populate( snapshot42 );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );

    // The same record becomes unreadable on disk; re-attaching the SAME run
    // must drop the stale success — the refused record serves NO run scope
    // at all, so the render is refusal-phrased unknown + typed problem,
    // never the old status.
    goodDir.write( "provenance_run-42.json", std::string( "{ corrupted " ) );
    section.attachRunProvenance(
        QString::fromStdString( ( goodDir.path / "provenance_run-42.json" ).string() ) );
    CHECK( section.currentRunId().isEmpty() );
    REQUIRE_FALSE( section.evidenceProblemCodes().isEmpty() );
    CHECK( section.evidenceProblemCodes().first().contains( "parse_failed" ) );
    section.populate( snapshot42 );
    CHECK_FALSE( section.panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );
    CHECK( section.panel()->renderedText().contains(
        QStringLiteral( "执行情况未知（证据记录被拒绝" ) ) );
    CHECK( section.panel()->renderedText().contains( QStringLiteral( "证据记录问题" ) ) );

    // A fresh section against a tampered-only directory behaves the same.
    StepExplanationSection freshSection(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; } );
    freshSection.attachRunProvenance(
        QString::fromStdString( ( evidenceDir.path / "provenance_run-9.json" ).string() ) );
    CHECK( freshSection.currentRunId().isEmpty() );
    REQUIRE_FALSE( freshSection.evidenceProblemCodes().isEmpty() );
    CHECK( freshSection.evidenceProblemCodes().first().contains( "parse_failed" ) );

    SelectionContextSnapshot snapshot;
    snapshot.selectedPipelineNodeId = QStringLiteral( "n1" );
    freshSection.populate( snapshot );
    CHECK( freshSection.panel()->renderedText().contains(
        QStringLiteral( "执行情况未知（证据记录被拒绝" ) ) );
    CHECK_FALSE( freshSection.panel()->renderedText().contains( "状态: " ) );

    // A file not matching the pinned record-name grammar serves no evidence
    // either — and says so (single grammar truth: the adapter's).
    section.attachRunProvenance(
        QString::fromStdString( ( evidenceDir.path / "checkpoint_run-1.json" ).string() ) );
    CHECK( section.currentRunId().isEmpty() );
    CHECK( section.evidenceProblemCodes().first().contains( "malformed_name" ) );

    // A well-formed name whose run id can never produce a valid evidence
    // link (whitespace) is refused by the SAME grammar, not half-accepted.
    section.attachRunProvenance(
        QString::fromStdString( ( evidenceDir.path / "provenance_a b.json" ).string() ) );
    CHECK( section.currentRunId().isEmpty() );
    CHECK( section.evidenceProblemCodes().first().contains( "malformed_name" ) );
}

TEST_CASE( "inspector host lifecycle: placeholder, reopen and no duplicate population",
           "[explain][section][host]" )
{
    ensureApp();
    TempDir evidenceDir( "explain_host_runs" );
    const auto evidence = writeAndLoadRun( evidenceDir, QStringLiteral( "run-42" ),
                                           ExecutionState::Succeeded, QString(),
                                           QStringLiteral( "sha256full:abc123" ), 4321 );

    TempDir dir( "explain_host_guidance" );
    std::vector<GuidanceLoadProblem> problems;
    const auto guidance = GuidanceStore::loadFromDirectory( dir.path.string(), problems );

    const WorkflowDocument document = parsedDocument();
    InspectorHost host;
    auto *section = new StepExplanationSection(
        liveKnowledge(), [&]() -> const IAuthoredGuidance * { return guidance.get(); },
        [&]() -> std::optional<WorkflowDocument> { return document; }, &host );
    host.registerSection( section );
    section->attachRunProvenance(
        QString::fromStdString( ( evidenceDir.path / "provenance_run-42.json" ).string() ) );

    // Unsupported selection → placeholder, no tab, no stale content.
    SelectionContextSnapshot empty;
    host.setSnapshot( empty );
    CHECK( host.findChild<QTabWidget *>() == nullptr );
    CHECK( section->panel()->requestGeneration() == 0 );

    // Node selected → the section populates exactly once per snapshot.
    SelectionContextSnapshot nodeSelected;
    nodeSelected.selectedPipelineNodeId = QStringLiteral( "n1" );
    host.setSnapshot( nodeSelected );
    const quint64 generationAfterFirst = section->panel()->requestGeneration();
    CHECK( generationAfterFirst == 1 );
    CHECK( section->panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );

    // Identical snapshot re-populates exactly once and renders the same bytes
    // (deterministic replacement, never accumulation).
    host.setSnapshot( nodeSelected );
    CHECK( section->panel()->requestGeneration() == generationAfterFirst + 1 );
    CHECK( countOccurrences( section->panel()->renderedText(),
                             QStringLiteral( "执行情况（Execution）" ) ) == 1 );

    // Hide/show of the section itself (dock toggle) triggers NO additional
    // population — only selection changes drive renders.
    section->hide();
    section->show();
    CHECK( section->panel()->requestGeneration() == generationAfterFirst + 1 );

    // Selection leaves the pipeline node → the section is dropped from the
    // tabs and its content is not consulted again until a new selection.
    host.setSnapshot( empty );
    CHECK( host.findChild<QTabWidget *>() == nullptr );

    // …and returns cleanly (close-reopen of the hosting dock pattern).
    host.setSnapshot( nodeSelected );
    CHECK( section->panel()->renderedText().contains( QStringLiteral( "状态: Succeeded" ) ) );
    CHECK( countOccurrences( section->panel()->renderedText(),
                             QStringLiteral( "执行情况（Execution）" ) ) == 1 );
}
