// tests/test_magic_wand_roi.cpp — D15 Package G (magic wand).
//
// Ground-truth policy: the disk's pixel count (dx^2 + dy^2 < r^2) is a
// fixture-geometry fact; polygon coverage is re-derived in the test with an
// independent ray-cast point-in-polygon.  The spec window [1240, 1270]
// brackets the theoretical area pi * 20^2 = 1256.6.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <QPoint>
#include <QPolygonF>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <vector>

#include "app/workbench/classification_studio_widget.h"
#include "synthetic_raster_builder.h"

using rs::app::RsRoiMagicWandTool;

namespace
{
  void ensureQgisApplication()
  {
    if ( QApplication::instance() )
      return;
    static int argc = 1;
    static char appName[] = "test_magic_wand_roi";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
  }

  bool pointInPolygon( const QPolygonF &poly, double px, double py )
  {
    bool inside = false;
    const int n = poly.size();
    for ( int i = 0, j = n - 1; i < n; j = i++ )
    {
      const double xi = poly[i].x(), yi = poly[i].y();
      const double xj = poly[j].x(), yj = poly[j].y();
      if ( ( ( yi > py ) != ( yj > py ) )
           && ( px < ( xj - xi ) * ( py - yi ) / ( yj - yi + 0.0 ) + xi ) )
        inside = !inside;
    }
    return inside;
  }

  struct Coverage
  {
    int insidePixels = 0;
    int backgroundLeaks = 0;
  };

  Coverage polygonCoverage( const QPolygonF &poly, int width, int height,
                            const std::vector<uint8_t> &isForeground )
  {
    Coverage cov;
    for ( int y = 0; y < height; ++y )
      for ( int x = 0; x < width; ++x )
      {
        const bool hit = pointInPolygon( poly, x + 0.5, y + 0.5 );
        if ( hit && isForeground[static_cast<size_t>( y ) * width + x] )
          ++cov.insidePixels;
        else if ( hit && !isForeground[static_cast<size_t>( y ) * width + x] )
          ++cov.backgroundLeaks;
      }
    return cov;
  }
} // namespace

TEST_CASE( "Magic wand extracts the disk with area-grade fidelity and zero leaks",
           "[d15][magicwand]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "disk.tif" ) );
  REQUIRE( !sicnu::testing::RsSyntheticRasterBuilder( 100, 100, 1, GDT_Float32 )
             .withCircle( 1, 50, 50, 20, 100.0f, 0.0f )
             .writeToDisk( path )
             .isEmpty() );

  std::unique_ptr<QgsRasterLayer> layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "disk" ) );
  REQUIRE( layer->isValid() );

  RsRoiMagicWandTool wand;
  const QPolygonF poly = wand.extractRegion( QPoint( 50, 50 ), layer.get(), 5.0 );
  REQUIRE_FALSE( poly.isEmpty() );

  // Fixture geometry: (x-50)^2 + (y-50)^2 <= 20^2 (builder's inclusive
  // circle test); 1257 pixels, inside the spec window around pi*20^2.
  std::vector<uint8_t> foreground( 100 * 100, 0 );
  int expectedDisk = 0;
  for ( int y = 0; y < 100; ++y )
    for ( int x = 0; x < 100; ++x )
    {
      const double d2 = ( x - 50 ) * ( x - 50.0 ) + ( y - 50 ) * ( y - 50.0 );
      if ( d2 <= 400.0 )
      {
        foreground[static_cast<size_t>( y ) * 100 + x] = 1;
        ++expectedDisk;
      }
    }
  REQUIRE( expectedDisk > 1200 ); // sanity: pi * 400 ~ 1256.6

  const Coverage cov = polygonCoverage( poly, 100, 100, foreground );
  // Spec window around the theoretical area ...
  REQUIRE( cov.insidePixels >= 1240 );
  REQUIRE( cov.insidePixels <= 1270 );
  // ... and exact agreement with the fixture geometry (boundary fidelity).
  REQUIRE( cov.insidePixels == expectedDisk );
  // No background pixel leaks into the polygon.
  REQUIRE( cov.backgroundLeaks == 0 );
}

TEST_CASE( "Magic wand respects the pixel budget and rejects invalid seeds",
           "[d15][magicwand]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString path = dir.filePath( QStringLiteral( "disk_cap.tif" ) );
  REQUIRE( !sicnu::testing::RsSyntheticRasterBuilder( 100, 100, 1, GDT_Float32 )
             .withCircle( 1, 50, 50, 20, 100.0f, 0.0f )
             .writeToDisk( path )
             .isEmpty() );
  std::unique_ptr<QgsRasterLayer> layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "disk" ) );
  REQUIRE( layer->isValid() );

  RsRoiMagicWandTool wand;
  const QPolygonF capped = wand.extractRegion( QPoint( 50, 50 ), layer.get(), 5.0, 50 );
  std::vector<uint8_t> foreground( 100 * 100, 0 );
  int expectedDisk = 0;
  for ( int y = 0; y < 100; ++y )
    for ( int x = 0; x < 100; ++x )
    {
      const double d2 = ( x - 50 ) * ( x - 50.0 ) + ( y - 50 ) * ( y - 50.0 );
      if ( d2 <= 400.0 )
      {
        foreground[static_cast<size_t>( y ) * 100 + x] = 1;
        ++expectedDisk;
      }
    }
  const Coverage cov = polygonCoverage( capped, 100, 100, foreground );
  REQUIRE( cov.insidePixels <= 50 + 8 ); // hard budget (small trace slack)

  // Out-of-bounds seed.
  REQUIRE( wand.extractRegion( QPoint( -1, 50 ), layer.get(), 5.0 ).isEmpty() );
  REQUIRE( wand.extractRegion( QPoint( 50, 200 ), layer.get(), 5.0 ).isEmpty() );

  // Null layer.
  REQUIRE( wand.extractRegion( QPoint( 50, 50 ), nullptr, 5.0 ).isEmpty() );
}

TEST_CASE( "Magic wand blocks on any band difference (no spectral leaks)",
           "[d15][magicwand]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString path = dir.filePath( QStringLiteral( "twoband.tif" ) );
  // Top-left quadrant: band1 = 100, band2 = 0 — except column x = 10
  // (y < 50) where band2 = 100.  Seeded at (20, 20) with vector [100, 0],
  // the fill must cover the quadrant but NOT the x = 10 column (identical
  // band 1, band 2 differs by 100 >> tolerance) and NOT the right half or
  // bottom half (band 1 differs).
  sicnu::testing::RsSyntheticRasterBuilder builder( 100, 100, 2, GDT_Float32 );
  for ( int y = 0; y < 50; ++y )
    for ( int x = 0; x < 50; ++x )
    {
      builder.withPixel( 1, x, y, 100.0f );
      builder.withPixel( 2, x, y, x == 10 ? 100.0f : 0.0f );
    }
  REQUIRE( !builder.writeToDisk( path ).isEmpty() );
  std::unique_ptr<QgsRasterLayer> layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "two" ) );
  REQUIRE( layer->isValid() );

  RsRoiMagicWandTool wand;
  const QPolygonF poly = wand.extractRegion( QPoint( 20, 20 ), layer.get(), 5.0 );
  REQUIRE_FALSE( poly.isEmpty() );

  // Covered: quadrant pixels on the seed's side of the slit.
  REQUIRE( pointInPolygon( poly, 20.5, 20.5 ) );
  REQUIRE( pointInPolygon( poly, 45.5, 45.5 ) );
  // Excluded: the band-2 mismatch column splits the quadrant; the fill is
  // connected, so the strip left of the slit is a separate component the
  // wand must NOT leak into.
  REQUIRE_FALSE( pointInPolygon( poly, 10.5, 20.5 ) );
  REQUIRE_FALSE( pointInPolygon( poly, 5.5, 5.5 ) );
  // Excluded: band-1 mismatch regions.
  REQUIRE_FALSE( pointInPolygon( poly, 60.5, 20.5 ) );
  REQUIRE_FALSE( pointInPolygon( poly, 20.5, 60.5 ) );
}
