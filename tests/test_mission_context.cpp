// D18 — MissionContext value type: round-trip, fingerprint, selection builder,
// fail-closed schema, no live-pointer fields in the JSON contract.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/mission_context.h"
#include "app/workbench/selection_context.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>

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

#include "app/workbench/mission_context_store.h"

#include <QTemporaryDir>

TEST_CASE( "mission sidecar save/load round-trip", "[d18][mission][persist]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString project = dir.filePath( QStringLiteral( "demo.qgz" ) );
  QFile touch( project );
  REQUIRE( touch.open( QIODevice::WriteOnly ) );
  touch.write( "x" );
  touch.close();

  MissionContext ctx;
  ctx.missionName = QStringLiteral( "persist" );
  ctx.assets.push_back( WorkbenchObjectRef{ ObjectKind::Asset, QStringLiteral( "a" ), QStringLiteral( "A" ) } );
  QString err;
  REQUIRE( sicnu::app::saveMissionContextToSidecar( project, ctx, &err ) );
  REQUIRE( err.isEmpty() );
  const QString side = sicnu::app::missionSidecarPathForProject( project );
  REQUIRE( side.endsWith( QStringLiteral( ".mission.json" ) ) );
  REQUIRE( QFileInfo::exists( side ) );

  MissionContext loaded;
  REQUIRE( sicnu::app::loadMissionContextFromSidecar( project, loaded, &err ) );
  REQUIRE( loaded.missionName == QLatin1String( "persist" ) );
  REQUIRE( loaded.assets.front().id == QLatin1String( "a" ) );
  REQUIRE( !loaded.missionId.isEmpty() );
}

TEST_CASE( "mission_context publish result from path is stable and idempotent", "[d18][mission]" )
{
  MissionContext ctx;
  sicnu::app::ensureMissionId( ctx );
  const auto a = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/aligned.tif" ), QStringLiteral( "aligned" ) );
  REQUIRE( !a.isNull() );
  REQUIRE( a.kind == ObjectKind::Result );
  REQUIRE( a.id.startsWith( QLatin1String( "result-" ) ) );
  REQUIRE( ctx.results.size() == 1 );
  REQUIRE( ctx.selection.resultIds.contains( a.id ) );
  REQUIRE( ctx.metadata.value( QStringLiteral( "artifact_paths" ) ).toObject().contains( a.id ) );

  const auto b = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/aligned.tif" ), QStringLiteral( "aligned-again" ) );
  REQUIRE( b.id == a.id );
  REQUIRE( ctx.results.size() == 1 );
  REQUIRE( ctx.results.front().displayName == QLatin1String( "aligned-again" ) );
}

TEST_CASE( "mission_context set active workflow shared identity", "[d18][mission]" )
{
  MissionContext ctx;
  sicnu::app::ActiveWorkflowRef wf;
  wf.workflowId = QStringLiteral( "wf-ir2" );
  wf.schemaVersion = QStringLiteral( "2.0" );
  wf.fingerprint = QStringLiteral( "deadbeef" );
  wf.runner = QStringLiteral( "pipeline_run_coordinator" );
  sicnu::app::setMissionActiveWorkflow( ctx, wf );
  REQUIRE( !ctx.missionId.isEmpty() );
  REQUIRE( ctx.activeWorkflow.workflowId == QLatin1String( "wf-ir2" ) );
  REQUIRE( ctx.activeWorkflow.fingerprint == QLatin1String( "deadbeef" ) );
}


#include <QDomDocument>

TEST_CASE( "mission XML dual-write round-trip beside sidecar", "[d18][mission][persist]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString project = dir.filePath( QStringLiteral( "demo.qgs" ) );
  QFile touch( project );
  REQUIRE( touch.open( QIODevice::WriteOnly ) );
  touch.write( "<qgis/>" );
  touch.close();

  MissionContext ctx;
  ctx.missionName = QStringLiteral( "dual" );
  ctx.activeWorkflow.workflowId = QStringLiteral( "wf-xml" );
  ctx.activeWorkflow.schemaVersion = QStringLiteral( "2.0" );
  ctx.activeWorkflow.runner = QStringLiteral( "pipeline_run_coordinator" );

  QDomDocument doc;
  REQUIRE( doc.setContent( QStringLiteral( "<qgis></qgis>" ) ) );
  QString err;
  REQUIRE( sicnu::app::persistMissionContextWithProject( project, doc, ctx, &err ) );
  REQUIRE( err.isEmpty() );
  REQUIRE( QFileInfo::exists( sicnu::app::missionSidecarPathForProject( project ) ) );
  REQUIRE( !doc.documentElement().firstChildElement( QStringLiteral( "sicnuMissionContext" ) ).isNull() );

  // Sidecar preference: delete XML-equivalent by loading via restore helper.
  MissionContext loaded;
  bool loadedFlag = false;
  REQUIRE( sicnu::app::restoreMissionContextWithProject( project, doc, loaded, &loadedFlag, &err ) );
  REQUIRE( loadedFlag );
  REQUIRE( loaded.missionName == QLatin1String( "dual" ) );
  REQUIRE( loaded.activeWorkflow.workflowId == QLatin1String( "wf-xml" ) );

  // XML-only path: remove sidecar and restore from document.
  REQUIRE( QFile::remove( sicnu::app::missionSidecarPathForProject( project ) ) );
  MissionContext fromXml;
  loadedFlag = false;
  REQUIRE( sicnu::app::restoreMissionContextWithProject( project, doc, fromXml, &loadedFlag, &err ) );
  REQUIRE( loadedFlag );
  REQUIRE( fromXml.missionName == QLatin1String( "dual" ) );
}

TEST_CASE( "classification path product upgrades provisional result", "[d18][mission]" )
{
  MissionContext ctx;
  sicnu::app::ensureMissionId( ctx );
  // Provisional request id (studio without path).
  WorkbenchObjectRef provisional{ ObjectKind::Result, QStringLiteral( "classify-studio-0-0" ),
                                  QStringLiteral( "provisional" ) };
  sicnu::app::publishMissionObject( ctx, provisional );
  REQUIRE( ctx.results.size() == 1 );

  // Path product lands — stable path-backed Result supersedes provisional identity for Agent.
  const auto product = sicnu::app::publishMissionResultFromPath(
    ctx, QStringLiteral( "/tmp/e2e/class_product.tif" ), QStringLiteral( "class-product" ) );
  REQUIRE( !product.isNull() );
  REQUIRE( product.id != provisional.id );
  REQUIRE( ctx.results.size() == 2 );
  REQUIRE( ctx.metadata.value( QStringLiteral( "artifact_paths" ) ).toObject().contains( product.id ) );
  REQUIRE( ctx.selection.resultIds.contains( product.id ) );
}
