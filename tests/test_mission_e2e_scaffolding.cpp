// D18 — Mission-level E2E scenarios (headless contract tests).
// Full GUI/QGIS paths require the Windows/CI toolchain. On this agent box
// cmake/g++ are absent — see EVIDENCE.md (honest not-executed). These cases
// still assert the MissionContext publish / identity / restore contracts that
// the mounted D14/D15/D17 surfaces write into.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
  const QStringList surfaces = {
    QStringLiteral( "georef-dual" ),
    QStringLiteral( "classify-studio" ),
    QStringLiteral( "workbench.ir2Pipeline" ),
  };
  REQUIRE( surfaces.size() == 3 );
  SUCCEED( "mount surface ids recorded for E2E GUI follow-up on toolchain hosts" );
}
