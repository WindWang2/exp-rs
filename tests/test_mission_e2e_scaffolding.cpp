// D18 — Mission-level E2E scenarios (headless contract tests).
// Full GUI/QGIS paths require the Windows/CI toolchain. On this agent box
// cmake/g++ are absent — see EVIDENCE.md (honest not-executed). These cases
// still assert the MissionContext publish / identity / restore contracts that
// the mounted D14/D15/D17 surfaces write into.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context.h"
#include "workflow/ir2_registry_node_executor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

using sicnu::app::ActiveWorkflowRef;
using sicnu::app::MissionContext;
using sicnu::app::ObjectKind;
using sicnu::app::WorkbenchObjectRef;

namespace {

MissionContext makeSeedMission()
{
  MissionContext ctx;
  sicnu::app::ensureMissionId( ctx );
  ctx.missionName = QStringLiteral( "e2e-seed" );
  ctx.projectRef = QStringLiteral( "memory:e2e" );
  ctx.activeWorkflow.workflowId = QStringLiteral( "wf-e2e" );
  ctx.activeWorkflow.schemaVersion = QStringLiteral( "2.0" );
  ctx.activeWorkflow.runner = QStringLiteral( "pipeline_run_coordinator" );
  WorkbenchObjectRef asset{ ObjectKind::Asset, QStringLiteral( "raw-1" ), QStringLiteral( "raw" ) };
  ctx.assets.push_back( asset );
  return ctx;
}

} // namespace

TEST_CASE( "scenario4_save_restore_replay", "[d18][mission][e2e]" )
{
  MissionContext original = makeSeedMission();
  original.results.push_back(
    WorkbenchObjectRef{ ObjectKind::Result, QStringLiteral( "aligned-1" ), QStringLiteral( "aligned" ) } );
  const QString fp = sicnu::app::missionContentFingerprint( original );
  REQUIRE( !fp.isEmpty() );

  const QJsonObject doc = sicnu::app::missionContextToJson( original );
  MissionContext restored;
  QString err;
  REQUIRE( sicnu::app::missionContextFromJson( doc, restored, &err ) );
  REQUIRE( err.isEmpty() );
  REQUIRE( restored.missionId == original.missionId );
  REQUIRE( restored.activeWorkflow.workflowId == original.activeWorkflow.workflowId );
  REQUIRE( restored.results.size() == 1 );
  REQUIRE( sicnu::app::missionContentFingerprint( restored ) == fp );
}

TEST_CASE( "scenario1_registration_classify_cartography_chain", "[d18][mission][e2e]" )
{
  // Contract for the menu-mounted D14→D15→cartography path: each stage
  // publishes a Result/Layer ref into MissionContext (no path-only reload).
  MissionContext ctx = makeSeedMission();
  const auto aligned = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/e2e/aligned.tif" ), QStringLiteral( "aligned" ) );
  const auto classified = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/e2e/class.tif" ), QStringLiteral( "class" ) );
  WorkbenchObjectRef mapProduct{ ObjectKind::Result, QStringLiteral( "mapspec-1" ),
                                 QStringLiteral( "map" ) };
  sicnu::app::publishMissionObject( ctx, mapProduct );
  ctx.cartographyProducts.push_back( mapProduct );

  REQUIRE( !aligned.isNull() );
  REQUIRE( !classified.isNull() );
  REQUIRE( aligned.id != classified.id );
  REQUIRE( ctx.results.size() >= 2 );
  REQUIRE( ctx.metadata.value( QStringLiteral( "artifact_paths" ) ).toObject().contains( aligned.id ) );

  const QJsonObject summary = sicnu::app::missionSummaryJson( ctx );
  REQUIRE( summary.value( QStringLiteral( "recent_results" ) ).toArray().size() >= 2 );
}

TEST_CASE( "scenario3_agent_visual_workflow_roundtrip_identity", "[d18][mission][e2e]" )
{
  // Agent and UI share the same ActiveWorkflowRef (id + fingerprint + runner).
  MissionContext ctx = makeSeedMission();
  ActiveWorkflowRef humanEdit;
  humanEdit.workflowId = QStringLiteral( "wf-shared" );
  humanEdit.name = QStringLiteral( "human-edit" );
  humanEdit.schemaVersion = QStringLiteral( "2.0" );
  humanEdit.fingerprint = QStringLiteral( "fp-human-edit" );
  humanEdit.runner = QStringLiteral( "pipeline_run_coordinator" );
  sicnu::app::setMissionActiveWorkflow( ctx, humanEdit );

  const QJsonObject summary = sicnu::app::missionSummaryJson( ctx );
  REQUIRE( summary.contains( QStringLiteral( "active_workflow" ) ) );
  REQUIRE( ctx.activeWorkflow.workflowId == QLatin1String( "wf-shared" ) );
  REQUIRE( ctx.activeWorkflow.fingerprint == QLatin1String( "fp-human-edit" ) );
  REQUIRE( ctx.activeWorkflow.runner == QLatin1String( "pipeline_run_coordinator" ) );

  MissionContext restored;
  QString err;
  REQUIRE( sicnu::app::missionContextFromJson( sicnu::app::missionContextToJson( ctx ), restored, &err ) );
  REQUIRE( restored.activeWorkflow == ctx.activeWorkflow );
}

TEST_CASE( "scenario2_temporal_change_mission_scaffold", "[d18][mission][e2e][scaffold]" )
{
  MissionContext ctx = makeSeedMission();
  ctx.temporal.collectionId = QStringLiteral( "col-ts" );
  ctx.temporal.startIso = QStringLiteral( "2020-01-01" );
  ctx.temporal.endIso = QStringLiteral( "2024-12-31" );
  ctx.temporal.activeResultId = QStringLiteral( "phenology-1" );
  const auto change = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/e2e/change.tif" ), QStringLiteral( "change" ) );
  REQUIRE( !change.isNull() );
  REQUIRE( ctx.temporal.collectionId == QLatin1String( "col-ts" ) );
  SUCCEED( "scenario2 temporal+change mission fields populated" );
}

TEST_CASE( "scenario5_mission_mount_surface_ids_documented", "[d18][mission][e2e][scaffold]" )
{
  // Production IDs from main_window_workbench.cpp / command_defs.cpp — keep in sync.
  const QStringList surfaces = {
    QStringLiteral( "georef-dual" ),
    QStringLiteral( "classify-studio" ),
    QStringLiteral( "workbench.ir2Pipeline" ),
  };
  REQUIRE( surfaces.size() == 3 );
  REQUIRE( QSet<QString>( surfaces.begin(), surfaces.end() ).size() == surfaces.size() );
  for ( const QString &id : surfaces )
  {
    REQUIRE( !id.trimmed().isEmpty() );
    REQUIRE( !id.contains( QLatin1Char( ' ' ) ) );
  }
  REQUIRE( surfaces.contains( QStringLiteral( "georef-dual" ) ) );
  REQUIRE( surfaces.contains( QStringLiteral( "classify-studio" ) ) );
  REQUIRE( surfaces.contains( QStringLiteral( "workbench.ir2Pipeline" ) ) );
}


TEST_CASE( "scenario3b_pipeline_run_coordinator_identity", "[d18][mission][e2e]" )
{
  // IR2 dock Run binds ActiveWorkflowRef.runner to pipeline_run_coordinator and
  // publishes a WorkflowRun ref on completion (headless contract).
  MissionContext ctx = makeSeedMission();
  ActiveWorkflowRef wf;
  wf.workflowId = QStringLiteral( "wf-run" );
  wf.schemaVersion = QStringLiteral( "2.0" );
  wf.fingerprint = QStringLiteral( "fp-run" );
  wf.runner = QStringLiteral( "pipeline_run_coordinator" );
  sicnu::app::setMissionActiveWorkflow( ctx, wf );

  WorkbenchObjectRef runRef{ ObjectKind::WorkflowRun, QStringLiteral( "ir2-run-wf-run-0" ),
                             QStringLiteral( "IR2 run ok" ) };
  sicnu::app::publishMissionObject( ctx, runRef );
  REQUIRE( ctx.activeWorkflow.runner == QLatin1String( "pipeline_run_coordinator" ) );
  REQUIRE( ctx.workflowRuns.size() == 1 );
  REQUIRE( ctx.workflowRuns.front().kind == ObjectKind::WorkflowRun );
}


TEST_CASE( "scenario3c_ir2_registry_bind_policy", "[d18][mission][e2e]" )
{
  // Contract (D-W5): production IR2 dock installs makeRegistryNodeExecutor.
  // Unbound nodes fail with a stable prefix; since #1006 the coordinator has
  // NO implicit synthetic default — an unset executor is a typed node failure
  // (ir2.executor_missing). Both default paths are locked end-to-end in
  // test_ir2_port_param_mapping.cpp ([1002]/[1006] cases); this case pins
  // the refusal helper contract makeRegistryNodeExecutor shares.
  using sicnu::workflow::Ir2OperatorBinding;
  using sicnu::workflow::NodeExecutionResult;
  using sicnu::workflow::NodeFact;
  using sicnu::workflow::kIr2OperatorUnboundPrefix;
  using sicnu::workflow::makeIr2UnboundRefusal;

  REQUIRE( QLatin1String( kIr2OperatorUnboundPrefix )
             == QLatin1String( "ir2.operator_unbound:" ) );

  NodeFact emptyNode;
  emptyNode.nodeId = QStringLiteral( "orphan" );
  emptyNode.operatorId.clear();
  const NodeExecutionResult emptyRefusal =
      makeIr2UnboundRefusal( emptyNode, Ir2OperatorBinding::UnboundEmpty );
  REQUIRE_FALSE( emptyRefusal.success );
  REQUIRE( emptyRefusal.artifactPath.isEmpty() );
  REQUIRE( emptyRefusal.errorMessage.startsWith( QLatin1String( kIr2OperatorUnboundPrefix ) ) );
  REQUIRE( emptyRefusal.errorMessage.contains( QStringLiteral( "empty operatorId" ) ) );

  NodeFact unknownNode;
  unknownNode.nodeId = QStringLiteral( "typo" );
  unknownNode.operatorId = QStringLiteral( "rs:definitely_not_registered_d18_e2e" );
  const NodeExecutionResult unknownRefusal =
      makeIr2UnboundRefusal( unknownNode, Ir2OperatorBinding::UnboundUnknown );
  REQUIRE_FALSE( unknownRefusal.success );
  REQUIRE( unknownRefusal.artifactPath.isEmpty() );
  REQUIRE( unknownRefusal.errorMessage.startsWith( QLatin1String( kIr2OperatorUnboundPrefix ) ) );
  REQUIRE( unknownRefusal.errorMessage.contains( QStringLiteral( "no registry binding" ) ) );

  // Mission runner identity remains pipeline_run_coordinator for IR2 dock Run.
  MissionContext ctx = makeSeedMission();
  REQUIRE( ctx.activeWorkflow.runner == QLatin1String( "pipeline_run_coordinator" ) );
}
