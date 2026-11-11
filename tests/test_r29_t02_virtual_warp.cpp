// tests/test_r29_t02_virtual_warp.cpp — R29-T02 WP05/WP06.
//
// A virtual raster whose recipe needs a grid transform (inputs share one CRS
// but the explicit target differs; or the input grid is rotated/sheared) must
// be REALLY transformed onto the target grid — not re-tagged with the target
// SRS label while the pixels stay laid out on the input grid (the pre-WP06
// provider behavior, spatially wrong output).
//
// Oracle independence: source pixel values are a closed-form gradient and the
// expected geographic positions of pixel centers are computed in THIS file
// with an independent OGR coordinate transformation — never by re-reading the
// product under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <ogr_spatialref.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "data/data_manager.h"
#include "data/raster_grid_compat.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsapplication.h>
#include <QApplication>

using namespace sicnu::data;

namespace
{

constexpr int kSide = 21; // 21x21 so pixel centers land far from edges

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r29_t02_virtual_warp";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

float sourceValue( int col, int row )
{
  return 1000.f + static_cast<float>( row ) * kSide + static_cast<float>( col );
}

/// North-up UTM48N source: origin (500000, 4500000), 10 m pixels, closed-form
/// gradient. Optional NoData ring on the outermost pixels.
QString writeUtmSource( const QString &path, bool nodataRing )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), kSide, kSide, 1, GDT_Float32,
                nullptr );
  REQUIRE( ds != nullptr );
  std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:32648" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( kSide );
  for ( int row = 0; row < kSide; ++row )
  {
    for ( int col = 0; col < kSide; ++col )
    {
      const bool edge = col == 0 || row == 0 || col == kSide - 1 || row == kSide - 1;
      line[col] = ( nodataRing && edge ) ? -999.f : sourceValue( col, row );
    }
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, kSide, 1, line.data(), kSide,
                           1, GDT_Float32, 0, 0 ) == CE_None );
  }
  if ( nodataRing )
    REQUIRE( GDALSetRasterNoDataValue( band, -999.0 ) == CE_None );
  GDALClose( ds );
  return path;
}

/// Rotated/sheared UTM48N source: gt = {500000, 10, 3, 4500000, 3, -10} with
/// a NoData ring (the bbox corners must warp to NoData, not to fake data).
QString writeRotatedSource( const QString &path )
{
  const QString written = writeUtmSource( path, /*nodataRing=*/true );
  GDALDatasetH ds = GDALOpen( written.toUtf8().constData(), GA_Update );
  REQUIRE( ds != nullptr );
  std::array<double, 6> gt = { 500000.0, 10.0, 3.0, 4500000.0, 3.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALClose( ds );
  return written;
}

AssetId registerRaster( DataManager &manager, const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  return result.assetId;
}

/// Independent forward coordinate transform UTM48N → WGS84 (lon, lat).
struct LonLat
{
  double lon = 0;
  double lat = 0;
};

LonLat utmToWgs84( double x, double y )
{
  OGRSpatialReference src;
  REQUIRE( src.importFromEPSG( 32648 ) == OGRERR_NONE );
  OGRSpatialReference dst;
  REQUIRE( dst.importFromEPSG( 4326 ) == OGRERR_NONE );
  dst.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER ); // void in GDAL 3.13
  OGRCoordinateTransformation *xf =
    OGRCreateCoordinateTransformation( &src, &dst );
  REQUIRE( xf != nullptr );
  double lx = x, ly = y;
  xf->Transform( 1, &lx, &ly ); // C++ Transform returns void in GDAL 3.13
  OGRCoordinateTransformation::DestroyCT( xf );
  REQUIRE( std::isfinite( lx ) );
  REQUIRE( std::isfinite( ly ) );
  return { lx, ly };
}

/// Geographic center of one north-up source pixel (pixel-center convention:
/// x = origin + (col+0.5)*10, y = origin - (row+0.5)*10).
LonLat sourcePixelCenterLonLat( int col, int row )
{
  return utmToWgs84( 500000.0 + ( col + 0.5 ) * 10.0,
                     4500000.0 - ( row + 0.5 ) * 10.0 );
}

/// Map-pixel center of the ROTATED source (gt2 = 3, gt4 = 3) in the source's
/// own UTM CRS — the rotated-case VRT stays in EPSG:32648, so the window
/// search runs in metres, NOT projected degrees (R1 review P1-4).
LonLat rotatedPixelCenterUtm( int col, int row )
{
  const double x = 500000.0 + ( col + 0.5 ) * 10.0 + ( row + 0.5 ) * 3.0;
  const double y = 4500000.0 + ( col + 0.5 ) * 3.0 - ( row + 0.5 ) * 10.0;
  return { x, y };
}

/// Reads one Float32 pixel from the VRT; NaN when outside the raster.
float readVrtPixel( GDALDatasetH ds, int col, int row )
{
  if ( col < 0 || row < 0 || col >= GDALGetRasterXSize( ds )
       || row >= GDALGetRasterYSize( ds ) )
    return std::numeric_limits<float>::quiet_NaN();
  float value = std::numeric_limits<float>::quiet_NaN();
  if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, col, row, 1, 1, &value,
                     1, 1, GDT_Float32, 0, 0 ) != CE_None )
    return std::numeric_limits<float>::quiet_NaN();
  return value;
}

/// True when @p expected appears within a ±@p window pixel neighborhood of
/// @p at (x/y expressed in the VRT's own CRS units — degrees for a
/// geographic target, metres for a projected one) — nearest-neighbour
/// placement tolerance.
bool windowContains( GDALDatasetH ds, const LonLat &at, double resX, double resY,
                     const std::array<double, 6> &gt, float expected, int window )
{
  const int col = static_cast<int>( std::llround( ( at.lon - gt[0] ) / resX ) );
  const int row = static_cast<int>( std::llround( ( gt[3] - at.lat ) / resY ) );
  for ( int dr = -window; dr <= window; ++dr )
    for ( int dc = -window; dc <= window; ++dc )
      if ( readVrtPixel( ds, col + dc, row + dr ) == expected )
        return true;
  return false;
}

} // namespace

TEST_CASE( "Preflight: shared input CRS with a differing explicit target "
           "requires reprojection",
           "[r29][t02][virtual-warp]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  const AssetId a = registerRaster(
    manager, writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), false ) );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );

  const PreflightResult result = preflightVirtualRaster( recipe, manager );
  CHECK( result.verdict == PreflightVerdict::RequiresReprojection );
  // Cross-unit reprojection (metres → degrees) without an explicit target
  // resolution cannot pin the warp grid — canCreate is false (R1 review P2:
  // the unit-blind fallback rounds to a degenerate ~1x1 product).
  CHECK( !result.canCreate );

  // With the target resolution pinned, the same shape is creatable.
  VirtualRasterRecipe pinned = recipe;
  pinned.targetResolutionX = 0.0001;
  pinned.targetResolutionY = 0.0001;
  const PreflightResult pinnedResult = preflightVirtualRaster( pinned, manager );
  CHECK( pinnedResult.verdict == PreflightVerdict::RequiresReprojection );
  CHECK( pinnedResult.canCreate );

  // Sanity: the same recipe WITHOUT the differing target stays Compatible.
  VirtualRasterRecipe sameCrsRecipe = recipe;
  sameCrsRecipe.targetCrs.clear();
  const PreflightResult sameCrs = preflightVirtualRaster( sameCrsRecipe, manager );
  CHECK( sameCrs.verdict == PreflightVerdict::Compatible );
  CHECK( sameCrs.canCreate );
}

TEST_CASE( "Provider warps shared-CRS inputs onto an explicit differing "
           "target grid with true pixel placement",
           "[r29][t02][virtual-warp]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  const AssetId a = registerRaster(
    manager, writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), false ) );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot = manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );

  const QString vrtPath = snapshot->source().canonicalSource;
  ensureGdalInit();
  GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( vrt != nullptr );

  // 1. The CRS is the target, semantically (not merely a relabel of the
  //    input's WKT).
  const QString vrtCrs = QString::fromUtf8( GDALGetProjectionRef( vrt ) );
  CHECK( isSameCrs( vrtCrs, QStringLiteral( "EPSG:4326" ) ) );

  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );
  const double resX = gt[1], resY = -gt[5];

  // 2. The grid covers the TRANSFORMED source extent (±3 target pixels), not
  //    the raw UTM numbers — a relabeled grid would sit near 500000/4500000
  //    in degrees and this comparison would be off by six orders of magnitude.
  const std::array<LonLat, 4> extentCorners = {
    utmToWgs84( 500000.0, 4500000.0 ),
    utmToWgs84( 500000.0 + kSide * 10.0, 4500000.0 ),
    utmToWgs84( 500000.0, 4500000.0 - kSide * 10.0 ),
    utmToWgs84( 500000.0 + kSide * 10.0, 4500000.0 - kSide * 10.0 ) };
  double minLon = extentCorners[0].lon, maxLon = extentCorners[0].lon;
  double minLat = extentCorners[0].lat, maxLat = extentCorners[0].lat;
  for ( const LonLat &corner : extentCorners )
  {
    minLon = std::min( minLon, corner.lon );
    maxLon = std::max( maxLon, corner.lon );
    minLat = std::min( minLat, corner.lat );
    maxLat = std::max( maxLat, corner.lat );
  }
  CHECK( gt[0] == Catch::Approx( minLon ).margin( 3 * resX ) );
  CHECK( gt[3] == Catch::Approx( maxLat ).margin( 3 * resY ) );
  CHECK( ( gt[0] + GDALGetRasterXSize( vrt ) * resX )
         == Catch::Approx( maxLon ).margin( 3 * resX ) );
  CHECK( ( gt[3] - GDALGetRasterYSize( vrt ) * resY )
         == Catch::Approx( minLat ).margin( 3 * resY ) );

  // 3. Known pixels land at their true geographic positions: the center and
  //    an off-center point, each projected independently in this test. Under
  //    a relabel the projected coordinates map far outside the raster and no
  //    window search can find the values.
  CHECK( windowContains( vrt, sourcePixelCenterLonLat( 10, 10 ), resX, resY, gt,
                         sourceValue( 10, 10 ), 1 ) );
  CHECK( windowContains( vrt, sourcePixelCenterLonLat( 3, 17 ), resX, resY, gt,
                         sourceValue( 3, 17 ), 1 ) );
  CHECK( windowContains( vrt, sourcePixelCenterLonLat( 17, 4 ), resX, resY, gt,
                         sourceValue( 17, 4 ), 1 ) );

  GDALClose( vrt );
}

TEST_CASE( "Provider warps a rotated input to a north-up grid and keeps "
           "NoData outside the true footprint",
           "[r29][t02][virtual-warp]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;
  const AssetId a = registerRaster(
    manager, writeRotatedSource( dir.filePath( QStringLiteral( "rot.tif" ) ) ) );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 } };
  recipe.targetResolutionX = 10.0;
  recipe.targetResolutionY = 10.0;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  recipe.noDataPolicy = NoDataPolicy::Preserve;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot = manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );

  const QString vrtPath = snapshot->source().canonicalSource;
  ensureGdalInit();
  GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( vrt != nullptr );

  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );

  // 1. The target grid is axis-aligned north-up (rotation removed).
  CHECK( gt[2] == 0.0 );
  CHECK( gt[4] == 0.0 );
  // 2. In the SAME CRS as the input (semantic compare).
  CHECK( isSameCrs( QString::fromUtf8( GDALGetProjectionRef( vrt ) ),
                    QStringLiteral( "EPSG:32648" ) ) );

  // 3. Known pixel at its true (rotated-source) position, in UTM metres —
  //    this VRT stays in EPSG:32648 (asserted above).
  CHECK( windowContains( vrt, rotatedPixelCenterUtm( 10, 10 ), gt[1], -gt[5],
                         gt, sourceValue( 10, 10 ), 1 ) );
  CHECK( windowContains( vrt, rotatedPixelCenterUtm( 4, 16 ), gt[1], -gt[5],
                         gt, sourceValue( 4, 16 ), 1 ) );

  // 4. The bbox corners of a rotated footprint lie OUTSIDE the true quad:
  //    they must read NoData (-999), never fabricated interior values.
  const int w = GDALGetRasterXSize( vrt );
  const int h = GDALGetRasterYSize( vrt );
  CHECK( readVrtPixel( vrt, 0, 0 ) == -999.f );
  CHECK( readVrtPixel( vrt, w - 1, 0 ) == -999.f );
  CHECK( readVrtPixel( vrt, 0, h - 1 ) == -999.f );
  CHECK( readVrtPixel( vrt, w - 1, h - 1 ) == -999.f );

  GDALClose( vrt );
}
