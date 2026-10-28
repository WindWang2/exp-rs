// tests/test_r25_d_e2e_display_cartography.cpp — R25-D W7
//
// End-to-end display/cartography CONTRACT test over production seams only:
//   synthetic product raster → Data asset → display view (canvas layer)
//   → real TaskCenter job (rs:ndvi through RSOperatorRegistry dispatch)
//   → result raster registered + added to the SAME view (结果入图层)
//   → real cartography:compose / cartography:export jobs (template draft
//     from the same catalog the dock reads)
//   → QGIS project save/clear/load roundtrip keeps the composed layout.
//
// No QSS, no mockups: every step is the seam the desktop shell itself uses
// (DataManager, QgisDisplayManager, TaskCenter/JobEngine, TemplateRegistry,
// cartography operators, QgsProject layout manager).
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsproject.h>

#include "app/display/qgis_display_manager.h"
#include "agent/cartography/cartography_operators.h"
#include "agent/cartography/registry.h"
#include "data/data_manager.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/task_center.h"

#include <qgsprintlayout.h>

#include <json/json.h>
#include <vector>

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

int main( int argc, char *argv[] )
{
  qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  sicnu::operators::rs::initBuiltinRsOperators();
  sicnu::agent::cartography::initCartographyOperators();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;
using sicnu::display::DisplayLayerId;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;
using sicnu::TaskCenter;

namespace
{

/// 4-band 64×64 GeoTIFF: band 3 (red) ≈ 0.2, band 4 (NIR) ≈ 0.8 → NDVI ≈ 0.6.
/// EPSG:4326 georeferenced so the display view and layout map frame get a
/// real extent/CRS.
QString syntheticProductRaster( const QTemporaryDir &dir, const QString &name )
{
  GDALAllRegister();
  const QString path = QDir( dir.path() ).filePath( name );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 64, H = 64, BANDS = 4;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, BANDS, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                           "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  for ( int band = 1; band <= BANDS; ++band )
  {
    const float value = band == 3 ? 0.2f : ( band == 4 ? 0.8f : 0.4f );
    GDALRasterBandH b = GDALGetRasterBand( ds, band );
    std::vector<float> line( W, value );
    for ( int row = 0; row < H; ++row )
      GDALRasterIO( b, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
  return path;
}

sicnu::data::AssetId registerRasterPath( DataManager &manager, const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  const auto registered = manager.registerSource( RegisterRequest{source} );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

DisplayViewId createView( QgisDisplayManager &manager, QgsMapCanvas &canvas,
                          QgsLayerTree &tree, QgsMapLayerStore &store )
{
  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &tree;
  spec.layerStore = &store;
  const auto created = manager.createView( spec );
  REQUIRE( created );
  return created.value();
}

/// Runs one operator through the REAL TaskCenter/JobEngine dispatch (the
/// same path a dialog, workflow node and the cartography dock take) and
/// returns the completed task's result payload.
Json::Value runTaskCenterJob( const std::string &algorithmId, const Json::Value &params )
{
  sicnu::jobs::JobRequest request;
  request.algorithmId = algorithmId;
  request.title = algorithmId;
  request.source = "ui";
  request.params = params;
  const long taskId = TaskCenter::instance().submitJob( request );
  REQUIRE( taskId > 0 );
  const sicnu::AlgorithmTaskInfo info = TaskCenter::instance().waitForTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  return info.resultPayload;
}

} // namespace

TEST_CASE( "product → display → operator result → same view → MapSpec compose/export → project roundtrip",
           "[r25d][e2e][display][cartography]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // ── 1. Synthetic product raster as a Data asset on a real display view ──
  const QString inputPath = syntheticProductRaster( dir, QStringLiteral( "product.tif" ) );
  DataManager dataManager;
  QgisDisplayManager displayManager( &dataManager );
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;
  const DisplayViewId viewId = createView( displayManager, canvas, tree, store );

  const sicnu::data::AssetId inputAsset = registerRasterPath( dataManager, inputPath );
  const auto added = displayManager.addLayer( viewId, inputAsset, { QStringLiteral( "Product" ), true } );
  REQUIRE( added );
  REQUIRE( canvas.layers().size() == 1 );
  REQUIRE( canvas.extent().width() > 0.0 );

  // ── 2. Real TaskCenter job: rs:ndvi over the registry dispatch ─────────
  const QString ndviPath = QDir( dir.path() ).filePath( QStringLiteral( "ndvi.tif" ) );
  {
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = ndviPath.toStdString();
    const Json::Value result = runTaskCenterJob( "rs:ndvi", params );
    REQUIRE( QFileInfo::exists( ndviPath ) );
    (void)result;
  }

  // ── 3. Result joins the SAME view (the 结果入图层 step) ────────────────
  const sicnu::data::AssetId ndviAsset = registerRasterPath( dataManager, ndviPath );
  const auto ndviAdded = displayManager.addLayer( viewId, ndviAsset, { QStringLiteral( "NDVI" ), true } );
  REQUIRE( ndviAdded );
  REQUIRE( canvas.layers().size() == 2 );

  // ── 4. Cartography: template draft over BOTH canvas layers → compose ──
  const Json::Value templates = sicnu::agent::cartography::TemplateRegistry::instance().templates();
  REQUIRE( templates.size() > 0 );
  Json::Value draftParams( Json::objectValue );
  draftParams["layout_name"] = "r25-d-e2e-map";
  draftParams["title"] = "R25-D E2E NDVI Map";
  Json::Value layers( Json::arrayValue );
  layers.append( inputPath.toStdString() );
  layers.append( ndviPath.toStdString() );
  draftParams["layers"] = layers;
  const Json::Value draft = sicnu::agent::cartography::TemplateRegistry::instance().instantiateTemplate(
    QStringLiteral( "vegetation-index-a4l" ), draftParams, nullptr );
  REQUIRE_FALSE( draft.isNull() );

  Json::Value composeParams( Json::objectValue );
  composeParams["mapspec"] = draft;
  const Json::Value composeResult = runTaskCenterJob( "cartography:compose", composeParams );
  CHECK( composeResult.get( "compiled", Json::Value( false ) ).asBool() );

  // The compiled layout registered in the PROJECT layout manager (the same
  // manager the designer and the exporter read).
  QgsPrintLayout *layout = nullptr;
  const QList<QgsPrintLayout *> layouts = QgsProject::instance()->layoutManager()->printLayouts();
  for ( QgsPrintLayout *candidate : layouts )
    if ( candidate->name() == QStringLiteral( "r25-d-e2e-map" ) )
      layout = candidate;
  REQUIRE( layout != nullptr );

  // ── 5. Export through the governed operator (evidence path) ────────────
  {
    Json::Value exportParams( Json::objectValue );
    exportParams["layout"] = "r25-d-e2e-map";
    exportParams["format"] = "png";
    exportParams["directory"] = dir.path().toStdString();
    exportParams["dpi"] = 96;
    const Json::Value exportResult = runTaskCenterJob( "cartography:export", exportParams );
    REQUIRE( exportResult.isMember( "path" ) );
    const QString exported = QString::fromStdString( exportResult["path"].asString() );
    CHECK( QFileInfo::exists( exported ) );
    CHECK( QFileInfo( exported ).size() > 0 );
  }

  // ── 6. Project roundtrip: the composed layout survives save/clear/load ─
  {
    const QString projectPath = QDir( dir.path() ).filePath( QStringLiteral( "e2e.qgs" ) );
    REQUIRE( QgsProject::instance()->write( projectPath ) );
    QgsProject::instance()->clear();
    REQUIRE( QgsProject::instance()->read( projectPath ) );
    bool found = false;
    for ( QgsPrintLayout *candidate : QgsProject::instance()->layoutManager()->printLayouts() )
      if ( candidate->name() == QStringLiteral( "r25-d-e2e-map" ) )
        found = true;
    CHECK( found );
  }
}
