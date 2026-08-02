// tests/test_workspace_snapshot.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include "agent/workspace_snapshot.h"
#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/source_descriptor.h"
#include "active_view_host.h"
#include "app/display/qgis_display_manager.h"

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

#include <memory>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using sicnu::data::AssetCapability;
using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::RasterStructure;
using sicnu::data::RegisterRequest;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::VectorLayerStructure;
using sicnu::data::VectorStructure;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::internal::SourceProvider;
using sicnu::data::internal::SourceProviderRegistry;

namespace {

class SnapshotTestSourceProvider : public SourceProvider
{
public:
  bool supports( const SourceDescriptor &source ) const override
  {
    return source.providerKey == QStringLiteral( "snapshot-test" );
  }

  Result<ResolvedSource> resolve( const SourceDescriptor &source ) const override
  {
    RasterStructure structure;
    structure.driverName = QStringLiteral( "GTiff" );
    structure.width = 256;
    structure.height = 128;
    structure.bandCount = 3;
    structure.crsWkt = QStringLiteral( "EPSG:4326" );

    return Result<ResolvedSource>::success(
      ResolvedSource{ AssetKind::Raster,
                      AssetState::Ready,
                      AssetCapability::Renderable | AssetCapability::ReadablePixels,
                      StorageKind::Memory,
                      source.canonicalSource.isEmpty() ? QStringLiteral( "snapshot-raster" )
                                                       : source.canonicalSource,
                      QString(),
                      QString(),
                      structure } );
  }
};

class SnapshotVectorSourceProvider : public SourceProvider
{
public:
  bool supports( const SourceDescriptor &source ) const override
  {
    return source.providerKey == QStringLiteral( "snapshot-vector-test" );
  }

  Result<ResolvedSource> resolve( const SourceDescriptor &source ) const override
  {
    VectorStructure structure;
    structure.driverName = QStringLiteral( "GeoJSON" );
    structure.layerCount = 2;

    VectorLayerStructure layer;
    layer.name = QStringLiteral( "boundaries" );
    layer.featureCount = 42;
    layer.geometryType = QStringLiteral( "Polygon" );
    layer.crsWkt = QStringLiteral( "EPSG:4326" );
    structure.layers.append( layer );

    return Result<ResolvedSource>::success(
      ResolvedSource{ AssetKind::Vector,
                      AssetState::Ready,
                      AssetCapability::Renderable | AssetCapability::QueryableFeatures |
                        AssetCapability::Relocatable,
                      StorageKind::Memory,
                      source.canonicalSource.isEmpty() ? QStringLiteral( "snapshot-vector" )
                                                       : source.canonicalSource,
                      QString(),
                      QString(),
                      structure } );
  }
};

std::unique_ptr<DataManager> makeSnapshotDataManager()
{
  SourceProviderRegistry providers;
  providers.add( std::make_unique<SnapshotTestSourceProvider>() );
  return providers.createDataManager();
}

} // namespace

TEST_CASE( "WorkspaceSnapshot - Pure C++ Serialization & Prompt Formatting", "[agent][workspace_snapshot]" )
{
  SECTION( "Empty WorkspaceSnapshot produces empty prompt header" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot;

    QString prompt = snapshot.toSystemPromptHeader();
    REQUIRE( prompt.contains( QStringLiteral( "[WORKSPACE CONTEXT]" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "Loaded Data Assets: (None)" ) ) );
  }

  SECTION( "Asset with unset kind renders as Unknown in prompt" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot;

    sicnu::agent::DataAssetInfo asset;
    asset.id = QStringLiteral( "asset-unknown" );
    asset.displayName = QStringLiteral( "mystery.dat" );
    snapshot.assets.append( asset );

    const QString prompt = snapshot.toSystemPromptHeader();
    REQUIRE( prompt.contains( QStringLiteral( "[Unknown]" ) ) );
  }

  SECTION( "Populated WorkspaceSnapshot renders raster & map view info in prompt" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot;

    sicnu::agent::DataAssetInfo asset;
    asset.id = QStringLiteral( "asset-101" );
    asset.displayName = QStringLiteral( "landsat_sample.tif" );
    asset.path = QStringLiteral( "/tmp/landsat_sample.tif" );
    asset.kind = AssetKind::Raster;
    asset.width = 1024;
    asset.height = 768;
    asset.bandCount = 4;
    asset.crsWkt = QStringLiteral( "EPSG:32648" );

    snapshot.assets.append( asset );

    snapshot.mapView.crsAuthId = QStringLiteral( "EPSG:32648" );
    snapshot.mapView.extentStr = QStringLiteral( "100.0,20.0,105.0,25.0" );
    snapshot.mapView.scale = 50000.0;
    snapshot.mapView.activeLayerName = QStringLiteral( "landsat_sample" );

    QString prompt = snapshot.toSystemPromptHeader();
    REQUIRE( prompt.contains( QStringLiteral( "landsat_sample.tif" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "1024x768, 4 bands" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "- CRS: EPSG:32648" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "- Extent: 100.0,20.0,105.0,25.0" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "Selected Layer: landsat_sample" ) ) );
  }

  SECTION( "WorkspaceSnapshot::capture with null pointers produces empty snapshot" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot = sicnu::agent::WorkspaceSnapshot::capture( nullptr, nullptr );
    REQUIRE( snapshot.assets.isEmpty() );
    REQUIRE( snapshot.mapView.crsAuthId.isEmpty() );
    REQUIRE( snapshot.mapView.activeLayerName.isEmpty() );
  }
}

TEST_CASE( "WorkspaceSnapshot::capture extracts assets from DataManager", "[agent][workspace_snapshot][capture]" )
{
  auto manager = makeSnapshotDataManager();
  REQUIRE( manager );

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "snapshot-test" );
  source.canonicalSource = QStringLiteral( "capture-scene.tif" );

  RegisterRequest request;
  request.source = source;
  const auto registered = manager->registerSource( request );
  REQUIRE( !registered.assetId.isNull() );

  const auto snapshot = sicnu::agent::WorkspaceSnapshot::capture( manager.get(), nullptr );
  REQUIRE( snapshot.assets.size() == 1 );
  CHECK( snapshot.assets.first().kind == AssetKind::Raster );
  CHECK( snapshot.assets.first().path == QStringLiteral( "capture-scene.tif" ) );
  CHECK( snapshot.assets.first().width == 256 );
  CHECK( snapshot.assets.first().height == 128 );
  CHECK( snapshot.assets.first().bandCount == 3 );
  CHECK( snapshot.assets.first().id == registered.assetId.toString() );
}

TEST_CASE( "WorkspaceSnapshot::capture prompt includes raster and vector asset details",
           "[agent][workspace_snapshot][capture]" )
{
  SourceProviderRegistry providers;
  providers.add( std::make_unique<SnapshotTestSourceProvider>() );
  providers.add( std::make_unique<SnapshotVectorSourceProvider>() );
  std::unique_ptr<DataManager> manager = providers.createDataManager();
  REQUIRE( manager );

  RegisterRequest rasterReq;
  rasterReq.source.providerKey = QStringLiteral( "snapshot-test" );
  rasterReq.source.canonicalSource = QStringLiteral( "/tmp/landsat_sample.tif" );
  const auto rasterRes = manager->registerSource( rasterReq );
  REQUIRE( !rasterRes.assetId.isNull() );

  RegisterRequest vectorReq;
  vectorReq.source.providerKey = QStringLiteral( "snapshot-vector-test" );
  vectorReq.source.canonicalSource = QStringLiteral( "/tmp/boundaries.geojson" );
  const auto vectorRes = manager->registerSource( vectorReq );
  REQUIRE( !vectorRes.assetId.isNull() );

  const auto snapshot = sicnu::agent::WorkspaceSnapshot::capture( manager.get(), nullptr );
  REQUIRE( snapshot.assets.size() == 2 );

  const QString prompt = snapshot.toSystemPromptHeader();
  REQUIRE( prompt.contains( QStringLiteral( "[WORKSPACE CONTEXT]" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "/tmp/landsat_sample.tif" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "/tmp/boundaries.geojson" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "[Vector]" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "2 vector layers" ) ) );
}

TEST_CASE( "WorkspaceSnapshot::capture reads map view facades from ActiveViewHost",
           "[agent][workspace_snapshot][capture]" )
{
  QgsMapCanvas canvas;
  canvas.setDestinationCrs( QgsCoordinateReferenceSystem::fromEpsgId( 4326 ) );
  canvas.setExtent( QgsRectangle( 10.0, 20.0, 30.0, 40.0 ) );

  ActiveViewHost host( &canvas, nullptr, nullptr, nullptr, nullptr,
                       sicnu::display::DisplayViewId(), nullptr );

  const auto snapshot = sicnu::agent::WorkspaceSnapshot::capture( nullptr, &host );
  REQUIRE( snapshot.assets.isEmpty() );
  CHECK( snapshot.mapView.crsAuthId.contains( QStringLiteral( "4326" ) ) );
  // Canvas may normalize extent; require a non-empty serialized extent when set.
  if ( !canvas.extent().isEmpty() && !canvas.extent().isNull() )
  {
    CHECK_FALSE( snapshot.mapView.extentStr.isEmpty() );
    CHECK( snapshot.mapView.extentStr.count( QLatin1Char( ',' ) ) == 3 );
  }
  CHECK( snapshot.mapView.scale > 0.0 );

  const QString prompt = snapshot.toSystemPromptHeader();
  REQUIRE( prompt.contains( QStringLiteral( "Map View State:" ) ) );
  CHECK( prompt.contains( QStringLiteral( "- CRS:" ) ) );
  if ( !snapshot.mapView.extentStr.isEmpty() )
    CHECK( prompt.contains( QStringLiteral( "- Extent:" ) ) );
}
