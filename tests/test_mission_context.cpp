// D18 — MissionContext value type: round-trip, fingerprint, selection builder,
// fail-closed schema, no live-pointer fields in the JSON contract.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context.h"
#include "app/workbench/selection_context.h"

#include <QJsonDocument>
#include <QJsonObject>

using sicnu::app::MissionContext;
using sicnu::app::ObjectKind;
using sicnu::app::WorkbenchObjectRef;
using sicnu::app::SelectionContextSnapshot;

TEST_CASE( "mission_context round-trips through QJson", "[d18][mission]" )
{
  MissionContext ctx;
  ctx.missionId = QStringLiteral( "m-1" );
  ctx.missionName = QStringLiteral( "GF classify" );
  ctx.projectRef = QStringLiteral( "/tmp/demo.qgz" );
  ctx.spatial.crs = QStringLiteral( "EPSG:32649" );
  ctx.spatial.aoiWkt = QStringLiteral( "POLYGON((0 0,1 0,1 1,0 1,0 0))" );
  ctx.spatial.hasExtent = true;
  ctx.spatial.xmin = 0;
  ctx.spatial.ymin = 0;
  ctx.spatial.xmax = 1;
  ctx.spatial.ymax = 1;
  ctx.temporal.collectionId = QStringLiteral( "col-1" );
  ctx.temporal.startIso = QStringLiteral( "2024-01-01" );
  ctx.temporal.endIso = QStringLiteral( "2024-12-31" );
  ctx.activeWorkflow.workflowId = QStringLiteral( "wf-9" );
  ctx.activeWorkflow.schemaVersion = QStringLiteral( "2.0" );
  ctx.activeWorkflow.fingerprint = QStringLiteral( "abc" );
  ctx.activeWorkflow.runner = QStringLiteral( "pipeline_run_coordinator" );

  WorkbenchObjectRef asset;
  asset.kind = ObjectKind::Asset;
  asset.id = QStringLiteral( "asset-1" );
  asset.displayName = QStringLiteral( "GF2" );
  ctx.assets.push_back( asset );

  WorkbenchObjectRef result;
  result.kind = ObjectKind::Result;
  result.id = QStringLiteral( "result-9" );
  ctx.results.push_back( result );

  const QJsonObject doc = sicnu::app::missionContextToJson( ctx );
  REQUIRE( doc.value( QStringLiteral( "kind" ) ).toString() == QLatin1String( "mission_context" ) );
  REQUIRE( doc.value( QStringLiteral( "schema_version" ) ).toString() == QLatin1String( "1.0" ) );
  // Contract: serialized form must never advertise QObject / layer pointers.
  REQUIRE( !doc.contains( QStringLiteral( "qobject" ) ) );
  REQUIRE( !doc.contains( QStringLiteral( "layer_ptr" ) ) );
  REQUIRE( !doc.contains( QStringLiteral( "canvas" ) ) );

  MissionContext restored;
  QString err;
  REQUIRE( sicnu::app::missionContextFromJson( doc, restored, &err ) );
  REQUIRE( err.isEmpty() );
  REQUIRE( restored.missionId == ctx.missionId );
  REQUIRE( restored.missionName == ctx.missionName );
  REQUIRE( restored.projectRef == ctx.projectRef );
  REQUIRE( restored.spatial.crs == ctx.spatial.crs );
  REQUIRE( restored.spatial.aoiWkt == ctx.spatial.aoiWkt );
  REQUIRE( restored.spatial.hasExtent );
  REQUIRE( restored.temporal.collectionId == ctx.temporal.collectionId );
  REQUIRE( restored.activeWorkflow.workflowId == ctx.activeWorkflow.workflowId );
  REQUIRE( restored.activeWorkflow.schemaVersion == QLatin1String( "2.0" ) );
  REQUIRE( restored.assets.size() == 1 );
  REQUIRE( restored.assets.front().id == QLatin1String( "asset-1" ) );
  REQUIRE( restored.results.front().kind == ObjectKind::Result );
}

TEST_CASE( "mission_context rejects wrong kind/version", "[d18][mission]" )
{
  MissionContext out;
  QString err;
  QJsonObject bad;
  bad.insert( QStringLiteral( "kind" ), QStringLiteral( "not_mission" ) );
  bad.insert( QStringLiteral( "schema_version" ), QStringLiteral( "1.0" ) );
  REQUIRE_FALSE( sicnu::app::missionContextFromJson( bad, out, &err ) );
  REQUIRE( err.contains( QStringLiteral( "mission_context" ) ) );

  QJsonObject badVer;
  badVer.insert( QStringLiteral( "kind" ), QStringLiteral( "mission_context" ) );
  badVer.insert( QStringLiteral( "schema_version" ), QStringLiteral( "9.9" ) );
  REQUIRE_FALSE( sicnu::app::missionContextFromJson( badVer, out, &err ) );
  REQUIRE( err.contains( QStringLiteral( "schema_version" ) ) );
}

TEST_CASE( "mission_content_fingerprint is stable for equal science", "[d18][mission]" )
{
  MissionContext a;
  a.missionName = QStringLiteral( "X" );
  a.projectRef = QStringLiteral( "proj" );
  a.assets.push_back( WorkbenchObjectRef{ ObjectKind::Asset, QStringLiteral( "a1" ), QStringLiteral( "A" ) } );

  MissionContext b = a;
  b.missionId = QStringLiteral( "different-session-id" );
  b.metadata.insert( QStringLiteral( "saved_at" ), QStringLiteral( "2026-09-15T00:00:00Z" ) );

  const QString fa = sicnu::app::missionContentFingerprint( a );
  const QString fb = sicnu::app::missionContentFingerprint( b );
  REQUIRE( fa.size() == 64 );
  REQUIRE( fa == fb );

  b.assets.front().id = QStringLiteral( "a2" );
  REQUIRE( sicnu::app::missionContentFingerprint( b ) != fa );
}

TEST_CASE( "missionContextFromSelection projects object lists", "[d18][mission]" )
{
  SelectionContextSnapshot snap;
  snap.workbenchId = QStringLiteral( "classify" );
  snap.selectedAssetIds = QStringList{ QStringLiteral( "asset-7" ) };
  snap.selectedResultIds = QStringList{ QStringLiteral( "res-1" ) };
  snap.selectedDatasetIds = QStringList{ QStringLiteral( "ds-1" ) };
  snap.selectedWorkflowRunIds = QStringList{ QStringLiteral( "run-1" ) };

  const MissionContext ctx = sicnu::app::missionContextFromSelection( snap );
  REQUIRE( ctx.selection.workbenchId == QLatin1String( "classify" ) );
  REQUIRE( ctx.selection.assetIds.contains( QStringLiteral( "asset-7" ) ) );
  REQUIRE( ctx.assets.size() == 1 );
  REQUIRE( ctx.assets.front().kind == ObjectKind::Asset );
  REQUIRE( ctx.results.front().id == QLatin1String( "res-1" ) );
  REQUIRE( ctx.datasets.front().id == QLatin1String( "ds-1" ) );
  REQUIRE( ctx.workflowRuns.front().id == QLatin1String( "run-1" ) );
  REQUIRE( ctx.selection.primary.kind == ObjectKind::WorkflowRun );
  REQUIRE( ctx.selection.primary.id == QLatin1String( "run-1" ) );

  const QJsonObject summary = sicnu::app::missionSummaryJson( ctx );
  REQUIRE( summary.contains( QStringLiteral( "selection" ) ) );
  REQUIRE( summary.contains( QStringLiteral( "recent_results" ) ) );
  REQUIRE( summary.contains( QStringLiteral( "fingerprint" ) ) );
}

TEST_CASE( "ensureMissionId mints uuid when empty", "[d18][mission]" )
{
  MissionContext ctx;
  REQUIRE( ctx.missionId.isEmpty() );
  sicnu::app::ensureMissionId( ctx );
  REQUIRE( ctx.missionId.size() >= 32 );
  const QString first = ctx.missionId;
  sicnu::app::ensureMissionId( ctx );
  REQUIRE( ctx.missionId == first );
}
