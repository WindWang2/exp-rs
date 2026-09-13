// Workbench 10.0 — unified object identity (goal WP-A)
//
// Covers: kind tokens, the deterministic primary-object priority rule,
// selectedLayerIds stability, resolveSelectionAssetTargets cascade
// (assets > governance results > layer source — first non-empty wins,
// bounded, duplicate-free) and the agent context JSON projection.
// Resolution runs against the real DataManager + WorkspaceService with temp
// GTiff payloads (same fixture style as test_provenance_section).
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/object_identity.h"

#include "app/workbench/selection_context.h"
#include "data/data_manager.h"
#include "data/governance/workspace_service.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <vector>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_object_identity";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
  {
    app = new QgsApplication( fake_argc, fake_argv, false );
    QgsApplication::initQgis();
  }
  return app;
}

QString makeRaster( const QString &name )
{
  static QTemporaryDir dir;
  const QString path = QDir( dir.path() ).filePath( name );
  if ( QFileInfo::exists( path ) )
    return path;
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 8, H = 8;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, ( double ) H, 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.5f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  return path;
}

struct Fixture
{
  sicnu::data::DataManager manager;
  sicnu::workspace::WorkspaceService service;

  Fixture()
  {
    ensureApp();
    service.bindDataManager( &manager );
    static int instance = 0;
    const QString storeDir = QDir::temp().filePath(
      QStringLiteral( "wb10-object-identity-%1-%2" ).arg( QCoreApplication::applicationPid() ).arg( ++instance ) );
    REQUIRE( QDir().mkpath( storeDir ) );
    const QString storePath = QDir( storeDir ).filePath( QStringLiteral( "gov.db" ) );
    REQUIRE( service.openStore( storePath ) );
  }
  ~Fixture() { service.closeStore(); }

  sicnu::data::RegisterResult registerRaster( const QString &name )
  {
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = makeRaster( name );
    return manager.registerSource( sicnu::data::RegisterRequest{ source } );
  }
};

} // namespace

using namespace sicnu::app;

TEST_CASE( "objectKindToken is a stable wire vocabulary", "[object_identity]" )
{
  REQUIRE( objectKindToken( ObjectKind::None ) == QStringLiteral( "none" ) );
  REQUIRE( objectKindToken( ObjectKind::Layer ) == QStringLiteral( "layer" ) );
  REQUIRE( objectKindToken( ObjectKind::Asset ) == QStringLiteral( "asset" ) );
  REQUIRE( objectKindToken( ObjectKind::Result ) == QStringLiteral( "result" ) );
  REQUIRE( objectKindToken( ObjectKind::Dataset ) == QStringLiteral( "dataset" ) );
  REQUIRE( objectKindToken( ObjectKind::ExperimentRun ) == QStringLiteral( "experiment_run" ) );
  REQUIRE( objectKindToken( ObjectKind::Model ) == QStringLiteral( "model" ) );
  REQUIRE( objectKindToken( ObjectKind::WorkflowRun ) == QStringLiteral( "workflow_run" ) );
}

TEST_CASE( "primaryObject follows the documented priority", "[object_identity]" )
{
  SECTION( "nothing selected → null ref" )
  {
    SelectionContextSnapshot snap;
    const auto ref = ContextRules::primaryObject( snap );
    REQUIRE( ref.isNull() );
    REQUIRE( ref.kind == ObjectKind::None );
  }

  SECTION( "domain selections outrank the ambient layer selection" )
  {
    SelectionContextSnapshot snap;
    snap.selectedAssetIds.append( QStringLiteral( "asset-1" ) );
    snap.selectedResultIds.append( QStringLiteral( "res-1" ) );
    snap.selectedDatasetIds.append( QStringLiteral( "ds-1" ) );
    snap.selectedExperimentIds.append( QStringLiteral( "run-1" ) );
    snap.selectedModelIds.append( QStringLiteral( "mdl-1" ) );
    snap.selectedWorkflowRunIds.append( QStringLiteral( "wfr-1" ) );

    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::WorkflowRun );
    snap.selectedWorkflowRunIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::ExperimentRun );
    snap.selectedExperimentIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::Dataset );
    snap.selectedDatasetIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::Model );
    snap.selectedModelIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::Result );
    snap.selectedResultIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::Asset );
    snap.selectedAssetIds.clear();
    REQUIRE( ContextRules::primaryObject( snap ).kind == ObjectKind::None );
  }

  SECTION( "layer identity comes from the active layer first" )
  {
    QgsRasterLayer layer( QStringLiteral( "/tmp/a.tif" ), QStringLiteral( "A" ) );
    QgsRasterLayer other( QStringLiteral( "/tmp/b.tif" ), QStringLiteral( "B" ) );

    SelectionContextSnapshot snap;
    snap.selectedLayers.append( &other );
    snap.activeLayer = &layer;
    const auto ref = ContextRules::primaryObject( snap );
    REQUIRE( ref.kind == ObjectKind::Layer );
    REQUIRE( ref.id == layer.id() );
    REQUIRE( ref.displayName == QStringLiteral( "A" ) );

    snap.activeLayer = nullptr;
    REQUIRE( ContextRules::primaryObject( snap ).id == other.id() );

    REQUIRE( ContextRules::resolvePrimaryLayer( snap ) == &other );
    snap.selectedLayers.clear();
    REQUIRE( ContextRules::resolvePrimaryLayer( snap ) == nullptr );
  }
}

TEST_CASE( "selectedLayerIds is active-first and duplicate-free", "[object_identity]" )
{
  QgsRasterLayer layer( QStringLiteral( "/tmp/a.tif" ), QStringLiteral( "A" ) );
  SelectionContextSnapshot snap;
  REQUIRE( ContextRules::selectedLayerIds( snap ).isEmpty() );
  snap.selectedLayers.append( &layer );
  snap.activeLayer = &layer;
  const QStringList ids = ContextRules::selectedLayerIds( snap );
  REQUIRE( ids.size() == 1 );
  REQUIRE( ids.first() == layer.id() );
}

TEST_CASE( "resolveSelectionAssetTargets reproduces the provenance cascade",
           "[object_identity][provenance]" )
{
  ensureApp();
  Fixture fx;
  const auto base = fx.registerRaster( QStringLiteral( "id-base.tif" ) );
  const auto derived = fx.registerRaster( QStringLiteral( "id-derived.tif" ) );
  REQUIRE( !base.assetId.isNull() );
  REQUIRE( !derived.assetId.isNull() );

  SECTION( "asset selection wins over everything else" )
  {
    SelectionContextSnapshot snap;
    snap.selectedAssetIds.append( base.assetId.toString() );
    snap.selectedLayers.append( nullptr );
    const auto targets = resolveSelectionAssetTargets( snap, &fx.manager, &fx.service );
    REQUIRE( targets.size() == 1 );
    REQUIRE( targets.first() == base.assetId );
  }

  SECTION( "governance entity resolution when no asset is selected" )
  {
    // A governed entity whose id is NOT a catalog asset id but whose
    // canonicalSource points at a registered file: the cascade must land on
    // the underlying catalog asset through the path lookup.
    sicnu::workspace::GovernedAsset governed;
    governed.assetId = QStringLiteral( "wb10-entity" );
    governed.canonicalSource = makeRaster( QStringLiteral( "id-derived.tif" ) );
    governed.kind = QStringLiteral( "raster" );
    REQUIRE( fx.service.store().upsertAsset( governed ).operator bool() );

    SelectionContextSnapshot snap;
    snap.selectedResultIds.append( QStringLiteral( "wb10-entity" ) );
    const auto targets = resolveSelectionAssetTargets( snap, &fx.manager, &fx.service );
    REQUIRE_FALSE( targets.isEmpty() );
    REQUIRE( targets.first() == derived.assetId );

    // An unknown entity id never resolves to anything (no invention).
    SelectionContextSnapshot unknown;
    unknown.selectedResultIds.append( QStringLiteral( "no-such-entity" ) );
    REQUIRE( resolveSelectionAssetTargets( unknown, &fx.manager, &fx.service ).isEmpty() );
  }

  SECTION( "layer source resolution is last and unknown paths resolve empty" )
  {
    SelectionContextSnapshot snap;
    snap.selectedResultIds.append( QStringLiteral( "no-such-entity" ) );
    snap.selectedAssetIds.clear();
    snap.selectedResultIds.clear();
    const auto targets = resolveSelectionAssetTargets( snap, &fx.manager, &fx.service );
    REQUIRE( targets.isEmpty() );
  }

  SECTION( "null data manager refuses everything" )
  {
    SelectionContextSnapshot snap;
    snap.selectedAssetIds.append( base.assetId.toString() );
    REQUIRE( resolveSelectionAssetTargets( snap, nullptr, &fx.service ).isEmpty() );
  }
}

TEST_CASE( "workbenchContextToJson projects selection and facts", "[object_identity][agent]" )
{
  SelectionContextSnapshot snap;
  snap.workbenchId = QStringLiteral( "map" );
  snap.selectedAssetIds.append( QStringLiteral( "asset-9" ) );
  snap.selectedDatasetIds.append( QStringLiteral( "ds-9" ) );
  snap.hasInFlightTask = true;

  const Json::Value json = workbenchContextToJson(
    snap, QStringList{ QStringLiteral( "rs.spectralIndex" ), QStringLiteral( "workbench.temporal" ) } );

  REQUIRE( json.isObject() );
  REQUIRE( json["workbenchId"].asString() == "map" );
  REQUIRE( json["primaryObject"]["kind"].asString() == "dataset" );
  REQUIRE( json["primaryObject"]["id"].asString() == "ds-9" );
  REQUIRE( json["facts"]["hasDatasetSelection"].asBool() );
  REQUIRE( json["facts"]["hasInFlightTask"].asBool() );
  REQUIRE( json["selectedAssets"][0].asString() == "asset-9" );
  REQUIRE( json["selectedDatasets"][0].asString() == "ds-9" );
  REQUIRE( json["availableCommands"].size() == 2 );
  REQUIRE( json["availableCommands"][0].asString() == "rs.spectralIndex" );
}
