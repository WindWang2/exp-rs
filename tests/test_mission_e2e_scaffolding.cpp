// D18 — Mission-level E2E scaffolding (stubs).
// Full Scenario 1–5 bodies land as studios publish Result/Asset identities
// into MissionContext. This target pins the scenario names and the
// save/restore contract shape so CI discovers the suite early.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context.h"

#include <QJsonDocument>
#include <QJsonObject>

using sicnu::app::MissionContext;
using sicnu::app::ObjectKind;
using sicnu::app::WorkbenchObjectRef;

namespace
{

MissionContext makeSeedMission()
{
  MissionContext ctx;
  sicnu::app::ensureMissionId( ctx );
  ctx.missionName = QStringLiteral( "e2e-seed" );
  ctx.projectRef = QStringLiteral( "memory:e2e" );
  ctx.activeWorkflow.workflowId = QStringLiteral( "wf-e2e" );
  ctx.activeWorkflow.schemaVersion = QStringLiteral( "2.0" );
  WorkbenchObjectRef asset{ ObjectKind::Asset, QStringLiteral( "raw-1" ), QStringLiteral( "raw" ) };
  ctx.assets.push_back( asset );
  return ctx;
}

} // namespace

TEST_CASE( "scenario4_save_restore_replay_scaffold", "[d18][mission][e2e][scaffold]" )
{
  const MissionContext original = makeSeedMission();
  const QJsonObject doc = sicnu::app::missionContextToJson( original );
  MissionContext restored;
  QString err;
  REQUIRE( sicnu::app::missionContextFromJson( doc, restored, &err ) );
  REQUIRE( restored.missionId == original.missionId );
  REQUIRE( restored.activeWorkflow.workflowId == original.activeWorkflow.workflowId );
  REQUIRE( sicnu::app::missionContentFingerprint( restored )
           == sicnu::app::missionContentFingerprint( original ) );
}

TEST_CASE( "scenario1_registration_classify_cartography_scaffold", "[d18][mission][e2e][scaffold]" )
{
  // Scaffold only: claims the scenario id. Implementation fills when D14/D15
  // publish Result refs without path re-import.
  MissionContext ctx = makeSeedMission();
  ctx.results.push_back( WorkbenchObjectRef{ ObjectKind::Result, QStringLiteral( "aligned-1" ), QStringLiteral( "aligned" ) } );
  ctx.results.push_back( WorkbenchObjectRef{ ObjectKind::Result, QStringLiteral( "class-1" ), QStringLiteral( "class" ) } );
  ctx.cartographyProducts.push_back(
    WorkbenchObjectRef{ ObjectKind::Result, QStringLiteral( "mapspec-1" ), QStringLiteral( "map" ) } );
  const QJsonObject summary = sicnu::app::missionSummaryJson( ctx );
  REQUIRE( summary.value( QStringLiteral( "recent_results" ) ).toArray().size() >= 2 );
  SUCCEED( "scenario1 scaffold holds mission result chain shape" );
}

TEST_CASE( "scenario3_agent_visual_workflow_roundtrip_scaffold", "[d18][mission][e2e][scaffold]" )
{
  MissionContext ctx = makeSeedMission();
  ctx.activeWorkflow.fingerprint = QStringLiteral( "fp-human-edit" );
  ctx.activeWorkflow.runner = QStringLiteral( "pipeline_run_coordinator" );
  // Agent and UI must share the same workflow id + fingerprint (GOAL §10).
  REQUIRE( !ctx.activeWorkflow.isNull() );
  REQUIRE( !ctx.activeWorkflow.fingerprint.isEmpty() );
  SUCCEED( "scenario3 scaffold records shared workflow identity fields" );
}
