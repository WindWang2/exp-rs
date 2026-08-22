#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFileInfo>

#include "vector_test_fixtures.h"
#include <QMap>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>

#include <vector>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>

#include "app/active_view_host.h"
#include "app/project_context.h"

namespace {

/// Synthesise a small GeoTIFF per distinct `relative` path and cache them (plus
/// the holding temp dir) for the process lifetime, so tests do not depend on a
/// committed sample raster under data/samples/. Distinct relative paths yield
/// distinct files so the Data Manager does not dedup them by SourceKey.
/// dem.hdr is synthesized as an ENVI pair (dem.dat + dem.hdr) so the GDAL
/// provider's .hdr→.dat path-pair resolution runs against a real file.
QString syntheticSample( const QString &relative )
{
  static QTemporaryDir dir;
  static QMap<QString, QString> cache;
  auto it = cache.constFind( relative );
  if ( it != cache.constEnd() )
    return it.value();

  GDALAllRegister();
  const bool enviPair = relative == QLatin1String( "dem.hdr" );
  // ENVI driver writes dem.dat + dem.hdr together; return the .hdr path so the
  // data manager's path-pair rewrite (hdr → dat) is exercised end to end.
  const QString path = dir.path() + QLatin1Char( '/' ) +
                       ( enviPair ? QStringLiteral( "dem.dat" )
                                  : QString::number( cache.size() ) + QStringLiteral( ".tif" ) );
  GDALDriverH driver = GDALGetDriverByName( enviPair ? "ENVI" : "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 16, H = 16;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection(
    ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  cache.insert( relative, enviPair ? dir.path() + QStringLiteral( "/dem.hdr" ) : path );
  return enviPair ? dir.path() + QStringLiteral( "/dem.hdr" ) : path;
}

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
/// Sample rasters under data/samples/ (and the legacy phr_xs.tif) are no longer
/// committed; redirect those to a synthesised sample (one per distinct path).
/// Other paths (e.g. does-not-exist.tif) resolve to the real data tree so they
/// stay missing.
QString fixturePath( const QString &relative )
{
  if ( relative.startsWith( QLatin1String( "samples/" ) ) ||
       relative == QLatin1String( "phr_xs.tif" ) ||
       relative == QLatin1String( "dem.hdr" ) )
  {
    return syntheticSample( relative );
  }
  if ( relative == QLatin1String( "test_vectors.geojson" ) )
  {
    return vector_test_fixtures::syntheticGeoJsonPath();
  }
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  #ifdef _WIN32
  _exit( result );
#else
  return result;
#endif
}

TEST_CASE( "ActiveViewHost opens a raster through the project Data Context",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();
  CHECK( host.activeViewId() == context->mainViewId() );
  CHECK( host.mainViewId() == context->mainViewId() );

  const auto loaded = host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );

  REQUIRE( loaded );
  CHECK( context->dataManager().assets().size() == 1 );
  CHECK( context->dataManager().leaseCount(
             context->dataManager().assets().first().id() ) == 1 );
  CHECK( project->count() == 1 );
  const auto mainView = context->displayManager().view( context->mainViewId() );
  REQUIRE( mainView );
  REQUIRE( mainView->layerIds().size() == 1 );
  CHECK( mainView->layerIds().first() == loaded.value() );
  CHECK( context->displayManager().mapLayer( loaded.value() ) != nullptr );
}

TEST_CASE( "ActiveViewHost removes presentation without unloading its Data Asset",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();

  const auto loaded = host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  REQUIRE( loaded );
  const sicnu::data::AssetId assetId =
      context->dataManager().assets().first().id();
  QgsMapLayer *mapLayer = context->displayManager().mapLayer( loaded.value() );
  REQUIRE( mapLayer != nullptr );
  treeView.setCurrentLayer( mapLayer );
  REQUIRE( host.selectedLayers().size() == 1 );

  // The removal path confirms via a modal QMessageBox by default; inject an
  // automatic Yes so the test is non-interactive (and never hangs on a live
  // desktop or under offscreen runs).
  host.setConfirmationFn( []( const QString & ) { return true; } );

  host.removeSelectedDisplayLayers();

  CHECK( context->dataManager().asset( assetId ).has_value() );
  CHECK( context->dataManager().leaseCount( assetId ) == 0 );
  CHECK_FALSE( context->displayManager().layer( loaded.value() ).has_value() );
  CHECK( project->count() == 0 );
}

TEST_CASE( "ActiveViewHost displayAsset shows an existing catalog entry",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();

  // Register via open, remove display, then displayAsset again.
  const auto first = host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  REQUIRE( first );
  const sicnu::data::AssetId assetId =
      context->dataManager().assets().first().id();
  REQUIRE( context->displayManager().removeLayer( first.value() ) );
  CHECK( context->dataManager().leaseCount( assetId ) == 0 );
  CHECK( context->dataManager().asset( assetId ).has_value() );

  const auto second = host.displayAsset( assetId );
  REQUIRE( second );
  CHECK( context->dataManager().leaseCount( assetId ) == 1 );
  CHECK( context->displayManager().mapLayer( second.value() ) != nullptr );
  CHECK( project->count() == 1 );
}

TEST_CASE( "setActiveViewId rejects unknown views",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );

  CHECK( host.setActiveViewId( context->mainViewId() ) );
  CHECK_FALSE( host.setActiveViewId( sicnu::display::DisplayViewId{} ) );
  CHECK_FALSE( host.setActiveViewId( sicnu::display::DisplayViewId::generate() ) );
  CHECK( host.activeViewId() == context->mainViewId() );
}

TEST_CASE( "Project Context explicitly clears Data and Display state for a new "
           "project",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();
  REQUIRE( host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) ) );

  const auto cleared = context->clearProject( *project );

  REQUIRE( cleared );
  CHECK( context->dataManager().assets().isEmpty() );
  CHECK( project->count() == 0 );
  const auto mainView = context->displayManager().view( context->mainViewId() );
  REQUIRE( mainView );
  CHECK( mainView->layerIds().isEmpty() );

  const auto loadedAgain = host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  REQUIRE( loadedAgain );
  CHECK( context->dataManager().assets().size() == 1 );
  CHECK( project->count() == 1 );
}

TEST_CASE( "Generic openPath delegates vector discovery to providers",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();

  const auto vectorLoaded = host.openPath(
      fixturePath( QStringLiteral( "test_vectors.geojson" ) ) );
  REQUIRE( vectorLoaded );
  REQUIRE( context->dataManager().assets().size() == 1 );
  CHECK( context->dataManager().assets().first().kind() ==
         sicnu::data::AssetKind::Vector );
}

TEST_CASE( "ENVI path-pair resolution stays inside the GDAL provider",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();

  const auto enviLoaded =
      host.openRasterPath( fixturePath( QStringLiteral( "dem.hdr" ) ) );
  REQUIRE( enviLoaded );
  REQUIRE( context->dataManager().assets().size() == 1 );
  const auto rasterAssets = context->dataManager().assets(
      sicnu::data::AssetQuery{ sicnu::data::AssetKind::Raster } );
  REQUIRE( rasterAssets.size() == 1 );
  CHECK( QFileInfo( rasterAssets.first().source().canonicalSource ).fileName() ==
         QStringLiteral( "dem.dat" ) );
}

TEST_CASE( "Loaded layer survives event-loop turns (registry bridge re-parenting)",
           "[active_view_host][data_context]" )
{
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore() };
  auto createdContext = sicnu::app::ProjectContext::create( viewSpec );
  REQUIRE( createdContext );
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  ActiveViewHost host( &canvas, &treeView, nullptr,
                       &context->dataManager(), &context->displayManager(),
                       context->mainViewId(), nullptr );
  host.initLayerTree();

  REQUIRE( host.openRasterPath(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) ) );

  QCoreApplication::processEvents();
  QCoreApplication::processEvents();

  CHECK( project->count() == 1 );
  CHECK( canvas.layerCount() == 1 );
  QgsLayerTreeLayer *node =
      project->layerTreeRoot()->findLayer( project->mapLayers().first()->id() );
  REQUIRE( node != nullptr );
  CHECK( node->parent() == host.findOrCreateGroup(
                               QStringLiteral( "Raster Layers" ) ) );
}
