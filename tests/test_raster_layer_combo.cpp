// test_raster_layer_combo.cpp — shared raster layer picker widget (C5)
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/widgets/raster_layer_combo.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_raster_layer_combo";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "RasterLayerCombo lists project rasters and resolves selection", "[raster_layer_combo]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 10.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  const QString a = dir.filePath( QStringLiteral( "a.tif" ) );
  const QString b = dir.filePath( QStringLiteral( "b.tif" ) );
  REQUIRE( writeGdalOutput( a, 2, 2, bands, gt, QStringLiteral( "EPSG:4326" ), &err ) );
  REQUIRE( writeGdalOutput( b, 2, 2, bands, gt, QStringLiteral( "EPSG:4326" ), &err ) );

  QgsRasterLayer *layerA = new QgsRasterLayer( a, QStringLiteral( "layer_a" ) );
  QgsRasterLayer *layerB = new QgsRasterLayer( b, QStringLiteral( "layer_b" ) );
  REQUIRE( layerA->isValid() );
  REQUIRE( layerB->isValid() );
  QgsProject::instance()->addMapLayer( layerA );
  QgsProject::instance()->addMapLayer( layerB );

  RasterLayerCombo combo;
  CHECK( combo.count() == 0 );
  combo.populate();
  REQUIRE( combo.count() == 2 );
  CHECK_FALSE( combo.currentLayerId().isEmpty() );

  // Selection by id resolves to the right layer.
  combo.selectLayer( layerB->id() );
  CHECK( combo.currentLayerId() == layerB->id() );
  REQUIRE( combo.currentRasterLayer() != nullptr );
  CHECK( combo.currentRasterLayer()->name() == QStringLiteral( "layer_b" ) );

  combo.selectLayer( layerA->id() );
  CHECK( combo.currentRasterLayer()->name() == QStringLiteral( "layer_a" ) );

  // Unknown id is a no-op.
  combo.selectLayer( QStringLiteral( "no-such-layer" ) );
  CHECK( combo.currentLayerId() == layerA->id() );

  QgsProject::instance()->clear();
}
