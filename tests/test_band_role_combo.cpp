// test_band_role_combo.cpp — shared semantic band-role selector widget
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <qgsapplication.h>

#include <array>
#include <vector>

#include <gdal.h>

#include "app/widgets/band_role_combo.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_band_role_combo";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "BandRoleCombo lists bands with semantic roles and preselects by role",
           "[band_role_combo]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // A stacked product-style raster: band 1 NIR, band 2 Red, band 3 QA.
  const QString path = dir.filePath( QStringLiteral( "product.tif" ) );
  std::vector<std::vector<float>> bands( 3, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( path, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr );
  REQUIRE( ds != nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 1 ), "SICNU_BAND_ROLE", "nir", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 2 ), "SICNU_BAND_ROLE", "red", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 3 ), "SICNU_BAND_ROLE", "qa", nullptr );
  GDALClose( ds );

  BandRoleCombo combo;
  CHECK_FALSE( combo.hasRaster() );
  CHECK( combo.selectedBand() == 0 );

  combo.setRaster( path );
  REQUIRE( combo.hasRaster() );
  // "自动" item + 3 bands.
  REQUIRE( combo.count() == 4 );
  // Labels carry the role display names.
  CHECK( combo.itemText( 1 ).contains( QStringLiteral( "NIR" ) ) );
  CHECK( combo.itemText( 2 ).contains( QStringLiteral( "Red" ) ) );
  CHECK( combo.itemText( 3 ).contains( QStringLiteral( "QA" ) ) );

  // Default selection is the "自动" item.
  CHECK( combo.selectedBand() == 0 );
  CHECK( combo.selectedRole() == sicnu::data::BandRole::Unknown );

  // Preselect by role.
  combo.selectBandByRole( sicnu::data::BandRole::QA );
  CHECK( combo.selectedBand() == 3 );
  CHECK( combo.selectedRole() == sicnu::data::BandRole::QA );

  combo.selectBandByRole( sicnu::data::BandRole::NIR );
  CHECK( combo.selectedBand() == 1 );
  CHECK( combo.selectedRole() == sicnu::data::BandRole::NIR );

  // A role absent from the raster falls back to "自动".
  combo.selectBandByRole( sicnu::data::BandRole::Thermal );
  CHECK( combo.selectedBand() == 0 );

  // Unreadable source clears to the empty (auto-only) state.
  combo.setRaster( dir.filePath( QStringLiteral( "missing.tif" ) ) );
  CHECK( combo.count() == 1 );
  CHECK( combo.selectedBand() == 0 );
}
