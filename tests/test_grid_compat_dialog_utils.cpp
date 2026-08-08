// test_grid_compat_dialog_utils.cpp — dialog grid-preflight helper
//
// rasterGridCompatibilityMessage() is the shared pre-run check dialogs use
// before submitting multi-raster operators (ADR 0066 seam, slice 53). This
// test pins the verdicts: compatible grids pass, blocking issues (resolution,
// CRS) produce actionable messages, ungeoreferenced pairs fall back to
// compatible, and the fusion exemption ignores pixel-size differences.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>

#include "app/dialogs/dialog_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_grid_compat_dialog_utils";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

/// Writes a 2x2 single-band raster with the given geotransform and CRS.
QString writeRaster( const QTemporaryDir &dir, const QString &name,
                     const std::array<double, 6> &gt, const char *crs )
{
  const QString path = dir.filePath( name );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 10.0f ) );
  QString err;
  REQUIRE( writeGdalOutput( path, 2, 2, bands, gt, QString::fromUtf8( crs ), &err ) );
  return path;
}

} // namespace

TEST_CASE( "Grid preflight helper verdicts", "[grid_compat][dialog_utils]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const std::array<double, 6> gt10 = { 500000, 10, 0, 4000000, 0, -10 };
  const std::array<double, 6> gt20 = { 500000, 20, 0, 4000000, 0, -20 };
  const std::array<double, 6> gtShifted = { 500005, 10, 0, 4000000, 0, -10 };

  const QString a = writeRaster( dir, QStringLiteral( "a.tif" ), gt10, "EPSG:32648" );
  const QString bSame = writeRaster( dir, QStringLiteral( "b.tif" ), gt10, "EPSG:32648" );
  const QString cCoarse = writeRaster( dir, QStringLiteral( "c.tif" ), gt20, "EPSG:32648" );
  const QString dOtherCrs = writeRaster( dir, QStringLiteral( "d.tif" ), gt10, "EPSG:4326" );
  const QString eMisaligned = writeRaster( dir, QStringLiteral( "e.tif" ), gtShifted, "EPSG:32648" );

  SECTION( "Identical grids pass" )
  {
    CHECK( rasterGridCompatibilityMessage( a, bSame ).isEmpty() );
  }

  SECTION( "Resolution mismatch is actionable" )
  {
    const QString msg = rasterGridCompatibilityMessage( a, cCoarse );
    REQUIRE_FALSE( msg.isEmpty() );
    CHECK( msg.contains( QStringLiteral( "10" ) ) );
    CHECK( msg.contains( QStringLiteral( "20" ) ) );
  }

  SECTION( "CRS mismatch is actionable" )
  {
    const QString msg = rasterGridCompatibilityMessage( a, dOtherCrs );
    REQUIRE_FALSE( msg.isEmpty() );
  }

  SECTION( "Sub-pixel origin misalignment blocks" )
  {
    const QString msg = rasterGridCompatibilityMessage( a, eMisaligned );
    REQUIRE_FALSE( msg.isEmpty() );
  }

  SECTION( "Fusion exemption ignores pixel-size differences only" )
  {
    CHECK( rasterGridCompatibilityMessage( a, cCoarse, true ).isEmpty() );
    // A CRS mismatch still blocks under the fusion exemption.
    CHECK_FALSE( rasterGridCompatibilityMessage( a, dOtherCrs, true ).isEmpty() );
  }

  SECTION( "Ungeoreferenced pair falls back to compatible" )
  {
    const std::array<double, 6> gtZero = { 0, 0, 0, 0, 0, 0 };
    const QString u1 = writeRaster( dir, QStringLiteral( "u1.tif" ), gtZero, "" );
    const QString u2 = writeRaster( dir, QStringLiteral( "u2.tif" ), gtZero, "" );
    CHECK( rasterGridCompatibilityMessage( u1, u2 ).isEmpty() );
  }

  SECTION( "Missing file yields a generic message" )
  {
    const QString msg = rasterGridCompatibilityMessage(
      a, dir.filePath( QStringLiteral( "missing.tif" ) ) );
    REQUIRE_FALSE( msg.isEmpty() );
  }
}
