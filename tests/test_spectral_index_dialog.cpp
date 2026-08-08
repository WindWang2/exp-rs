// test_spectral_index_dialog.cpp — spectral index dialog role-based band preselect
//
// Drives the dialog headlessly: registers a product-role raster and a plain
// raster, then asserts the shared BandRoleCombo band pickers preselect by
// semantic role (or by the positional fallback for plain rasters).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

#include "app/dialogs/spectral_index_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_spectral_index_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "SpectralIndexDialog band pickers preselect by semantic role", "[spectral_index_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Stacked product raster: band1=blue, band2=green, band3=red, band4=nir,
  // band5=swir1 (Landsat-like order).
  const QString product = dir.filePath( QStringLiteral( "product.tif" ) );
  std::vector<std::vector<float>> bands( 5, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( product, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  GDALDatasetH ds = GDALOpenEx( product.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr );
  REQUIRE( ds != nullptr );
  const char *roles[5] = { "blue", "green", "red", "nir", "swir1" };
  for ( int b = 0; b < 5; ++b )
    GDALSetMetadataItem( GDALGetRasterBand( ds, b + 1 ), "SICNU_BAND_ROLE", roles[b], nullptr );
  GDALClose( ds );

  QgsRasterLayer *productLayer = new QgsRasterLayer( product, QStringLiteral( "product" ) );
  REQUIRE( productLayer->isValid() );
  QgsProject::instance()->addMapLayer( productLayer );

  SpectralIndexDialog dialog;
  dialog.setRasterLayer( productLayer );

  auto *nir = dialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexNirCombo" ) );
  auto *red = dialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexRedCombo" ) );
  auto *green = dialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexGreenCombo" ) );
  auto *blue = dialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexBlueCombo" ) );
  auto *swir = dialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexSwirCombo" ) );
  REQUIRE( nir != nullptr );
  REQUIRE( red != nullptr );
  REQUIRE( green != nullptr );
  REQUIRE( blue != nullptr );
  REQUIRE( swir != nullptr );

  // Role-based preselection (1-based band numbers).
  CHECK( nir->currentData().toInt() == 4 );
  CHECK( red->currentData().toInt() == 3 );
  CHECK( green->currentData().toInt() == 2 );
  CHECK( blue->currentData().toInt() == 1 );
  CHECK( swir->currentData().toInt() == 5 );

  // Labels carry the role display names.
  CHECK( nir->itemText( 4 ).contains( QStringLiteral( "NIR" ) ) );

  QgsProject::instance()->clear();

  // Plain raster (no roles): positional fallback picks bands 4/3/2/1, SWIR 5.
  const QString plain = dir.filePath( QStringLiteral( "plain.tif" ) );
  REQUIRE( writeGdalOutput( plain, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
  QgsRasterLayer *plainLayer = new QgsRasterLayer( plain, QStringLiteral( "plain" ) );
  REQUIRE( plainLayer->isValid() );
  QgsProject::instance()->addMapLayer( plainLayer );

  SpectralIndexDialog plainDialog;
  plainDialog.setRasterLayer( plainLayer );
  auto *pNir = plainDialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexNirCombo" ) );
  auto *pSwir = plainDialog.findChild<QComboBox *>( QStringLiteral( "spectralIndexSwirCombo" ) );
  REQUIRE( pNir != nullptr );
  REQUIRE( pSwir != nullptr );
  CHECK( pNir->currentData().toInt() == 4 );
  CHECK( pSwir->currentData().toInt() == 5 );

  QgsProject::instance()->clear();
}
