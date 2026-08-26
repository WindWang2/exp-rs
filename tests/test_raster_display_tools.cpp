// tests/test_raster_display_tools.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QTemporaryDir>
#include <QString>
#include <QVariantMap>

#include "agent/interaction_tool_registry.h"
#include "agent/raster_display_service.h"
#include "agent/view_control_service.h"
#include "agent/workspace_snapshot.h"
#include "agent/mcp_server.h"
#include "data/band_role.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "display/qgis_display_manager.h"
#include "processing/framework/tool_call_dispatcher.h"
#include "processing/framework/json_params_converter.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <qgsapplication.h>
#include <qgscontrastenhancement.h>
#include <qgsmapcanvas.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgssinglebandgrayrenderer.h>

using namespace sicnu::agent;
using namespace sicnu::processing;

namespace {

struct RasterTestFixture {
  RasterTestFixture() {
    if ( !QCoreApplication::instance() ) {
      static int argc = 1;
      static char appName[] = "test_raster_display_tools";
      static char *argv[] = { appName, nullptr };
      s_app = new QApplication( argc, argv );
      QgsApplication::initQgis();
      GDALAllRegister();
    }
  }

  static QApplication *s_app;
};
QApplication *RasterTestFixture::s_app = nullptr;

QString createTestMultibandRaster( const QTemporaryDir &dir, const QString &fileName )
{
  const QString path = dir.filePath( fileName );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return {};

  // 6 bands: coastal(1), blue(2), green(3), red(4), nir(5), swir1(6)
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 10, 10, 6, GDT_Float32, nullptr );
  if ( !ds )
    return {};

  double adfGeoTransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
  GDALSetGeoTransform( ds, adfGeoTransform );

  GDALSetMetadataItem( GDALGetRasterBand( ds, 1 ), "SICNU_BAND_ROLE", "coastal", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 2 ), "SICNU_BAND_ROLE", "blue", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 3 ), "SICNU_BAND_ROLE", "green", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 4 ), "SICNU_BAND_ROLE", "red", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 5 ), "SICNU_BAND_ROLE", "nir", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 6 ), "SICNU_BAND_ROLE", "swir1", nullptr );

  std::vector<float> data( 100, 0.0f );
  for ( int b = 1; b <= 6; ++b )
  {
    for ( int i = 0; i < 100; ++i )
    {
      data[i] = static_cast<float>( b * 100 + i * 2 );
    }
    (void)GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, 10, 10,
                        data.data(), 10, 10, GDT_Float32, 0, 0 );
  }

  GDALClose( ds );
  return path;
}

} // namespace

TEST_CASE( "RasterDisplayTools - Tool Registration and Discovery", "[agent][raster][discovery]" )
{
  RasterTestFixture fixture;
  auto &registry = InteractionToolRegistry::instance();
  registry.reset();

  ViewControlService viewService;
  RasterDisplayService rasterService;
  registry.registerBuiltinTools( &viewService, &rasterService );

  SECTION( "Raster display tools are properly registered" )
  {
    REQUIRE( registry.hasTool( "raster:get_display" ) );
    REQUIRE( registry.hasTool( "raster:set_band_composite" ) );
    REQUIRE( registry.hasTool( "raster:set_stretch" ) );
    REQUIRE( registry.hasTool( "raster:reset_display" ) );
  }

  SECTION( "Tool category and name normalization" )
  {
    auto toolOpt = registry.findTool( "raster_set_band_composite" );
    REQUIRE( toolOpt.has_value() );
    REQUIRE( toolOpt->name == "raster:set_band_composite" );
    REQUIRE( toolOpt->category == "raster" );

    auto stretchOpt = registry.findTool( "raster_set_stretch" );
    REQUIRE( stretchOpt.has_value() );
    REQUIRE( stretchOpt->name == "raster:set_stretch" );
  }

  SECTION( "exportOpenAiToolDefinitions includes raster tools" )
  {
    const Json::Value definitions = registry.exportOpenAiToolDefinitions();
    REQUIRE( definitions.isArray() );

    bool foundBandComposite = false;
    bool foundStretch = false;
    for ( const auto &item : definitions )
    {
      if ( item.isMember( "function" ) )
      {
        const std::string name = item["function"]["name"].asString();
        if ( name == "raster_set_band_composite" )
          foundBandComposite = true;
        if ( name == "raster_set_stretch" )
          foundStretch = true;
      }
    }
    REQUIRE( foundBandComposite );
    REQUIRE( foundStretch );
  }
}

TEST_CASE( "RasterDisplayService - Semantic Band Role Resolution", "[agent][raster][band_role]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  REQUIRE( !rasterPath.isEmpty() );

  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "satellite_image" ) );
  REQUIRE( layer->isValid() );
  REQUIRE( layer->bandCount() == 6 );

  RasterDisplayService service;

  SECTION( "Numeric band resolution" )
  {
    Json::Value numVal( 4 );
    auto res = service.resolveBand( layer, numVal );
    REQUIRE( res.second.isEmpty() );
    REQUIRE( res.first == 4 );

    Json::Value numStr( "5" );
    auto strRes = service.resolveBand( layer, numStr );
    REQUIRE( strRes.second.isEmpty() );
    REQUIRE( strRes.first == 5 );
  }

  SECTION( "Semantic band role resolution from GDAL metadata" )
  {
    auto nirRes = service.resolveBand( layer, Json::Value( "nir" ) );
    REQUIRE( nirRes.second.isEmpty() );
    REQUIRE( nirRes.first == 5 );

    auto redRes = service.resolveBand( layer, Json::Value( "red" ) );
    REQUIRE( redRes.second.isEmpty() );
    REQUIRE( redRes.first == 4 );

    auto greenRes = service.resolveBand( layer, Json::Value( "green" ) );
    REQUIRE( greenRes.second.isEmpty() );
    REQUIRE( greenRes.first == 3 );

    auto blueRes = service.resolveBand( layer, Json::Value( "blue" ) );
    REQUIRE( blueRes.second.isEmpty() );
    REQUIRE( blueRes.first == 2 );

    auto swirRes = service.resolveBand( layer, Json::Value( "swir1" ) );
    REQUIRE( swirRes.second.isEmpty() );
    REQUIRE( swirRes.first == 6 );

    auto coastalRes = service.resolveBand( layer, Json::Value( "coastal" ) );
    REQUIRE( coastalRes.second.isEmpty() );
    REQUIRE( coastalRes.first == 1 );
  }

  SECTION( "Error on nonexistent semantic role or out of range band" )
  {
    auto thermalRes = service.resolveBand( layer, Json::Value( "thermal" ) );
    REQUIRE( !thermalRes.second.isEmpty() );
    REQUIRE( thermalRes.first == 0 );

    auto invalidRes = service.resolveBand( layer, Json::Value( "unknown_band_role_xyz" ) );
    REQUIRE( !invalidRes.second.isEmpty() );
    REQUIRE( invalidRes.first == 0 );

    auto outOfRange = service.resolveBand( layer, Json::Value( 99 ) );
    REQUIRE( !outOfRange.second.isEmpty() );
    REQUIRE( outOfRange.first == 0 );
  }

  delete layer;
}

TEST_CASE( "RasterDisplayService - Band Composition and RGB Switching", "[agent][raster][composition]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene" ) );
  REQUIRE( layer->isValid() );

  QgsProject::instance()->addMapLayer( layer );

  RasterDisplayService service;
  service.setActiveLayerName( layer->name() );

  SECTION( "Switch to True Color (RGB: red, green, blue)" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["red"] = "red";
    params["green"] = "green";
    params["blue"] = "blue";

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["renderer"].asString() == "MultiBandColor" );
    REQUIRE( res["bands"]["red"].asInt() == 4 );
    REQUIRE( res["bands"]["green"].asInt() == 3 );
    REQUIRE( res["bands"]["blue"].asInt() == 2 );

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() );
    REQUIRE( renderer != nullptr );
    REQUIRE( renderer->redBand() == 4 );
    REQUIRE( renderer->greenBand() == 3 );
    REQUIRE( renderer->blueBand() == 2 );
  }

  SECTION( "Switch to False Color NIR (RGB: nir, red, green)" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["red"] = "nir";
    params["green"] = "red";
    params["blue"] = "green";

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["bands"]["red"].asInt() == 5 );
    REQUIRE( res["bands"]["green"].asInt() == 4 );
    REQUIRE( res["bands"]["blue"].asInt() == 3 );

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() );
    REQUIRE( renderer != nullptr );
    REQUIRE( renderer->redBand() == 5 );
    REQUIRE( renderer->greenBand() == 4 );
    REQUIRE( renderer->blueBand() == 3 );
  }

  SECTION( "Switch to Single Band Gray (gray: nir)" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["gray"] = "nir";

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["renderer"].asString() == "SingleBandGray" );
    REQUIRE( res["bands"]["gray"].asInt() == 5 );

    auto *renderer = dynamic_cast<QgsSingleBandGrayRenderer *>( layer->renderer() );
    REQUIRE( renderer != nullptr );
    REQUIRE( renderer->inputBand() == 5 );
  }

  SECTION( "Set opacity along with composite" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["red"] = 4;
    params["green"] = 3;
    params["blue"] = 2;
    params["opacity"] = 0.65;

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( layer->opacity() == Catch::Approx( 0.65 ) );
  }

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayService - Stretch and Contrast Enhancement", "[agent][raster][stretch]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene_stretch" ) );
  REQUIRE( layer->isValid() );

  QgsProject::instance()->addMapLayer( layer );

  RasterDisplayService service;
  service.setActiveLayerName( layer->name() );

  SECTION( "Set minimum_maximum stretch" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["method"] = "minimum_maximum";

    const Json::Value res = service.setStretch( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["displayMin"].asDouble() <= res["displayMax"].asDouble() );
  }

  SECTION( "Set percent_clip stretch (2% to 98%)" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["method"] = "percent_clip";
    params["lower"] = 2.0;
    params["upper"] = 98.0;

    const Json::Value res = service.setStretch( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["method"].asString() == "percent_clip" );
  }

  SECTION( "Set stddev stretch (factor 2.0)" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["method"] = "stddev";
    params["factor"] = 2.0;

    const Json::Value res = service.setStretch( params );
    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( res["method"].asString() == "stddev" );
  }

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayService - get_display and reset_display", "[agent][raster][display_query_reset]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene_query" ) );
  REQUIRE( layer->isValid() );

  QgsProject::instance()->addMapLayer( layer );

  RasterDisplayService service;
  service.setActiveLayerName( layer->name() );

  SECTION( "get_display returns complete display configuration" )
  {
    Json::Value getParams( Json::objectValue );
    getParams["layer"] = layer->name().toStdString();

    const Json::Value info = service.getDisplay( getParams );
    REQUIRE( info["status"].asString() == "success" );
    REQUIRE( info.isMember( "renderer" ) );
    REQUIRE( info.isMember( "bands" ) );
    REQUIRE( info.isMember( "stretch" ) );
    REQUIRE( info.isMember( "opacity" ) );
    REQUIRE( info.isMember( "displayRevision" ) );
  }

  SECTION( "reset_display restores default presentation and opacity" )
  {
    // Modify display first
    layer->setOpacity( 0.4 );
    Json::Value compParams( Json::objectValue );
    compParams["layer"] = layer->name().toStdString();
    compParams["red"] = 6;
    compParams["green"] = 5;
    compParams["blue"] = 4;
    service.setBandComposite( compParams );

    // Reset
    Json::Value resetParams( Json::objectValue );
    resetParams["layer"] = layer->name().toStdString();
    const Json::Value res = service.resetDisplay( resetParams );

    REQUIRE( res["status"].asString() == "success" );
    REQUIRE( layer->opacity() == Catch::Approx( 1.0 ) );

    auto *renderer = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() );
    REQUIRE( renderer != nullptr );
    REQUIRE( renderer->redBand() == 1 );
    REQUIRE( renderer->greenBand() == 2 );
    REQUIRE( renderer->blueBand() == 3 );
  }

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayService - Error Handling", "[agent][raster][errors]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene_err" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  RasterDisplayService service;

  SECTION( "Nonexistent layer returns error" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = "nonexistent_layer_xyz";
    params["red"] = 1;
    params["green"] = 2;
    params["blue"] = 3;

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res.isMember( "errorMessage" ) );
    REQUIRE( !res["errorMessage"].asString().empty() );
  }

  SECTION( "Invalid band returns error" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["red"] = "nonexistent_role";
    params["green"] = 2;
    params["blue"] = 3;

    const Json::Value res = service.setBandComposite( params );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res.isMember( "errorMessage" ) );
  }

  SECTION( "Invalid stretch method returns error" )
  {
    Json::Value params( Json::objectValue );
    params["layer"] = layer->name().toStdString();
    params["method"] = "invalid_magic_stretch";

    const Json::Value res = service.setStretch( params );
    REQUIRE( res["status"].asString() == "error" );
    REQUIRE( res.isMember( "errorMessage" ) );
  }

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayService - WorkspaceSnapshot displayRevision linkage", "[agent][raster][snapshot]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene_snap" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  RasterDisplayService service;
  service.setActiveLayerName( layer->name() );

  const quint64 initialRev = service.displayRevision();

  Json::Value compParams( Json::objectValue );
  compParams["layer"] = layer->name().toStdString();
  compParams["red"] = "nir";
  compParams["green"] = "red";
  compParams["blue"] = "green";
  service.setBandComposite( compParams );

  REQUIRE( service.displayRevision() > initialRev );

  WorkspaceSnapshot snapshot = WorkspaceSnapshot::capture(
    nullptr, nullptr, layer->name(), service.displayRevision() );
  REQUIRE( snapshot.displayRevision == service.displayRevision() );

  const QString promptHeader = snapshot.toSystemPromptHeader();
  REQUIRE( promptHeader.contains( "Display Revision" ) );

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayTools - Execution via InteractionToolRegistry and Dispatcher", "[agent][raster][dispatch]" )
{
  RasterTestFixture fixture;
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );

  const QString rasterPath = createTestMultibandRaster( tempDir, QStringLiteral( "multiband.tif" ) );
  auto *layer = new QgsRasterLayer( rasterPath, QStringLiteral( "scene_dispatch" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  auto &registry = InteractionToolRegistry::instance();
  registry.reset();

  ViewControlService viewService;
  RasterDisplayService rasterService;
  rasterService.setActiveLayerName( layer->name() );
  registry.registerBuiltinTools( &viewService, &rasterService );

  ToolCallDispatcher dispatcher;
  dispatcher.setInteractionActionHandler( []( const std::string &name, const Json::Value &args ) {
    return InteractionToolRegistry::instance().execute( name, args );
  } );

  SECTION( "Dispatch raster:set_band_composite" )
  {
    Json::Value envelope( Json::objectValue );
    envelope["name"] = "raster:set_band_composite";
    Json::Value args( Json::objectValue );
    args["layer"] = layer->name().toStdString();
    args["red"] = "nir";
    args["green"] = "red";
    args["blue"] = "green";
    envelope["arguments"] = args;

    const Json::Value result = dispatcher.dispatchAndAwait( envelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result["renderer"].asString() == "MultiBandColor" );
    REQUIRE( result["bands"]["red"].asInt() == 5 );
  }

  SECTION( "Dispatch raster:get_display" )
  {
    Json::Value envelope( Json::objectValue );
    envelope["name"] = "raster:get_display";
    Json::Value args( Json::objectValue );
    args["layer"] = layer->name().toStdString();
    envelope["arguments"] = args;

    const Json::Value result = dispatcher.dispatchAndAwait( envelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result.isMember( "bands" ) );
    REQUIRE( result.isMember( "stretch" ) );
  }

  QgsProject::instance()->removeMapLayer( layer );
}

TEST_CASE( "RasterDisplayTools - Zero-band raster gracefully fails resetDisplay", "[agent][raster][zero_band]" )
{
  RasterTestFixture fixture;
  RasterDisplayService service;

  const auto result = service.resetDisplay( "nonexistent_zero_band_layer" );
  CHECK( result["status"].asString() == "error" );
  CHECK_FALSE( result["errorMessage"].asString().empty() );
}




