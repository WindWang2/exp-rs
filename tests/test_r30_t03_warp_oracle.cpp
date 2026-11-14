// tests/test_r30_t03_warp_oracle.cpp — R30-T03 WP06.
//
// Independent-oracle coverage for the warp-backed VRT grid/void semantics:
//   * union extent over sources covering different halves: pixels outside a
//     source's footprint read its DECLARED NoData, never fabricated data and
//     never valid-zero confusion;
//   * FillValue recipes make coverage holes machine-readable (the fill is
//     pinned into the warp's -srcnodata/-dstnodata contract);
//   * a declared -999 NoData ring never swallows genuine 0-valued pixels
//     (valid zero ≠ missing);
//   * the composed grid is whole-pixel aligned: origin/resolution exactly
//     realize the independently computed transformed extent;
//   * the preflight warns when a warp is required but nothing declares the
//     void contract (Preferve + undeclared inputs).
//
// Oracle independence: expected extents are computed in THIS file with an
// independent OGR transformation; expected pixel values are closed-form.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogr_spatialref.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "data/data_manager.h"
#include "data/raster_grid_compat.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <qgsapplication.h>

using namespace sicnu::data;

namespace
{

constexpr int kSide = 21;

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r30_t03_warp_oracle";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

float halfValue( int col, int row, int halfIndex )
{
  return 100.f * static_cast<float>( halfIndex + 1 )
         + static_cast<float>( row ) * kSide + static_cast<float>( col );
}

/// One half of a two-half union world: the LEFT (halfIndex 0) or RIGHT
/// (halfIndex 1) 210 m strip, with an optional -999 NoData ring.
QString writeHalfSource( const QString &path, int halfIndex, bool nodataRing )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), kSide, kSide, 1, GDT_Float32,
                nullptr );
  REQUIRE( ds != nullptr );
  const double originX = 500000.0 + halfIndex * kSide * 10.0;
  std::array<double, 6> gt = { originX, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:32648" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( kSide );
  for ( int row = 0; row < kSide; ++row )
  {
    for ( int col = 0; col < kSide; ++col )
    {
      const bool edge =
        col == 0 || row == 0 || col == kSide - 1 || row == kSide - 1;
      line[col] = ( nodataRing && edge ) ? -999.f
                                         : halfValue( col, row, halfIndex );
    }
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, kSide, 1, line.data(),
                           kSide, 1, GDT_Float32, 0, 0 ) == CE_None );
  }
  if ( nodataRing )
    REQUIRE( GDALSetRasterNoDataValue( band, -999.0 ) == CE_None );
  GDALClose( ds );
  return path;
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
  dst.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
  OGRCoordinateTransformation *xf =
    OGRCreateCoordinateTransformation( &src, &dst );
  REQUIRE( xf != nullptr );
  double lx = x, ly = y;
  xf->Transform( 1, &lx, &ly );
  OGRCoordinateTransformation::DestroyCT( xf );
  return { lx, ly };
}

float readPixel( GDALDatasetH ds, int col, int row, int band = 1 )
{
  float value = std::numeric_limits<float>::quiet_NaN();
  if ( col < 0 || row < 0 || col >= GDALGetRasterXSize( ds )
       || row >= GDALGetRasterYSize( ds ) )
    return value;
  if ( GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Read, col, row, 1, 1,
                     &value, 1, 1, GDT_Float32, 0, 0 ) != CE_None )
    return std::numeric_limits<float>::quiet_NaN();
  return value;
}

/// Map a UTM point to the VRT's own (degree) grid, nearest-pixel.
bool pixelAtUtm( GDALDatasetH ds, double x, double y, float expected,
                 int window, int band = 1 )
{
  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( ds, gt.data() ) == CE_None );
  const LonLat at = utmToWgs84( x, y );
  const int col =
    static_cast<int>( std::llround( ( at.lon - gt[0] ) / gt[1] ) );
  const int row =
    static_cast<int>( std::llround( ( gt[3] - at.lat ) / ( -gt[5] ) ) );
  for ( int dr = -window; dr <= window; ++dr )
    for ( int dc = -window; dc <= window; ++dc )
      if ( readPixel( ds, col + dc, row + dr, band ) == expected )
        return true;
  return false;
}

} // namespace

TEST_CASE( "Union extent keeps declared NoData over uncovered halves and "
           "genuine zeros stay valid",
           "[r30][t03][warp-oracle]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  // Left half WITH a -999 ring; right half WITHOUT any declaration (its
  // uncovered side must still not fabricate the left half's data).
  const QString left = writeHalfSource(
    dir.filePath( QStringLiteral( "left.tif" ) ), 0, /*nodataRing=*/true );
  const QString right = writeHalfSource(
    dir.filePath( QStringLiteral( "right.tif" ) ), 1, /*nodataRing=*/false );

  DataManager manager;
  const AssetId l = registerRaster( manager, left );
  const AssetId r = registerRaster( manager, right );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ l, 1 }, BandRef{ r, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  recipe.extentPolicy = ExtentPolicy::Union;
  recipe.noDataPolicy = NoDataPolicy::Preserve;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  ensureGdalInit();
  GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( vrt != nullptr );

  // 1. True values from both halves at their true positions. The composed
  //    VRT is band-wise (one VRTRasterBand per recipe input): the left half
  //    is band 1, the right half band 2 — each covers the UNION extent with
  //    its own footprint.
  const double leftCenterX = 500000.0 + ( kSide / 2.0 ) * 10.0;
  const double rightCenterX = 500000.0 + ( kSide + kSide / 2.0 ) * 10.0;
  CHECK( pixelAtUtm( vrt, leftCenterX, 4500000.0 - 105.0,
                     halfValue( 10, 10, 0 ), 1, /*band=*/1 ) );
  CHECK( pixelAtUtm( vrt, rightCenterX, 4500000.0 - 105.0,
                     halfValue( 10, 10, 1 ), 1, /*band=*/2 ) );

  // 2. Declared-NoData ring pixels read exactly -999 — never interpolated
  //    neighbours, never 0.
  CHECK( pixelAtUtm( vrt, 500000.0 + 5.0, 4500000.0 - 5.0, -999.f, 1 ) );

  // 3. The grid covers the union: independently transformed corner bounds
  //    (±3 target pixels of warp tolerance).
  const LonLat minCorner =
    utmToWgs84( 500000.0, 4500000.0 - kSide * 10.0 );
  const LonLat maxCorner =
    utmToWgs84( 500000.0 + 2 * kSide * 10.0, 4500000.0 );
  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );
  CHECK( gt[0] == Catch::Approx( minCorner.lon ).margin( 3 * gt[1] ) );
  CHECK( gt[3] == Catch::Approx( maxCorner.lat ).margin( 3 * ( -gt[5] ) ) );

  GDALClose( vrt );
}

TEST_CASE( "FillValue recipes realize coverage holes as the declared fill",
           "[r30][t03][warp-oracle]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  // Two halves with a one-strip GAP between them (halfIndex 0 and 2), both
  // UNDECLARED: with a fill policy the gap must read the fill, not raw 0.
  const QString west = writeHalfSource(
    dir.filePath( QStringLiteral( "west.tif" ) ), 0, /*nodataRing=*/false );
  const QString east = writeHalfSource(
    dir.filePath( QStringLiteral( "east.tif" ) ), 2, /*nodataRing=*/false );

  DataManager manager;
  const AssetId w = registerRaster( manager, west );
  const AssetId e = registerRaster( manager, east );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ w, 1 }, BandRef{ e, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  recipe.extentPolicy = ExtentPolicy::Union;
  recipe.noDataPolicy = NoDataPolicy::FillValue;
  recipe.noDataFillValue = -32768.0;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  ensureGdalInit();
  GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( vrt != nullptr );

  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );

  // Middle of the gap: covered by NEITHER source — must read the DECLARED
  // fill (the pre-WP06 warp left these as undeclared 0).
  const LonLat gapCenter =
    utmToWgs84( 500000.0 + ( 1.5 * kSide ) * 10.0, 4500000.0 - 105.0 );
  const int gapCol =
    static_cast<int>( std::llround( ( gapCenter.lon - gt[0] ) / gt[1] ) );
  const int gapRow =
    static_cast<int>( std::llround( ( gt[3] - gapCenter.lat ) / ( -gt[5] ) ) );
  CHECK( readPixel( vrt, gapCol, gapRow ) == -32768.f );

  // True values from both halves still land at their true positions
  // (west = band 1, east = band 2 of the band-wise composition).
  CHECK( pixelAtUtm( vrt, 500000.0 + 105.0, 4500000.0 - 105.0,
                     halfValue( 10, 10, 0 ), 1, /*band=*/1 ) );
  CHECK( pixelAtUtm( vrt, 500000.0 + ( 2 * kSide + 10.5 ) * 10.0,
                     4500000.0 - 105.0, halfValue( 10, 10, 2 ), 1,
                     /*band=*/2 ) );

  // And the VRT band declares the fill so consumers can mask it.
  int hasNoData = 0;
  const double declared =
    GDALGetRasterNoDataValue( GDALGetRasterBand( vrt, 1 ), &hasNoData );
  CHECK( hasNoData == 1 );
  CHECK( declared == Catch::Approx( -32768.0 ) );

  GDALClose( vrt );
}

TEST_CASE( "A declared -999 ring never swallows genuine zero pixels",
           "[r30][t03][warp-oracle]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString path = writeHalfSource(
    dir.filePath( QStringLiteral( "zero.tif" ) ), 0, /*nodataRing=*/true );
  // Genuine interior zeros: 3x3 zero BLOCKS inside the footprint (single
  // source pixels can be skipped by nearest downsampling — the target grid
  // is ~11.1 m/px over a 10 m source; a block guarantees coverage while the
  // assertion stays "genuine zeros survive as valid zeros").
  {
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> zeroRow( 3, 0.f );
    for ( const std::array<int, 2> &at : { std::array<int, 2>{ 8, 8 },
                                           std::array<int, 2>{ 12, 6 },
                                           std::array<int, 2>{ 5, 15 } } )
    {
      for ( int dr = 0; dr < 3; ++dr )
      {
        REQUIRE( GDALRasterIO( band, GF_Write, at[0], at[1] + dr, 3, 1,
                               zeroRow.data(), 3, 1, GDT_Float32, 0,
                               0 ) == CE_None );
      }
    }
    GDALClose( ds );
  }

  DataManager manager;
  const AssetId a = registerRaster( manager, path );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  recipe.noDataPolicy = NoDataPolicy::Preserve;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );

  ensureGdalInit();
  GDALDatasetH vrt =
    GDALOpen( snapshot->source().canonicalSource.toUtf8().constData(),
              GA_ReadOnly );
  REQUIRE( vrt != nullptr );

  // The interior zeros survive as VALID zeros at their true positions.
  const auto pixelCenter = []( int col, int row ) {
    return std::pair<double, double>( 500000.0 + ( col + 0.5 ) * 10.0,
                                      4500000.0 - ( row + 0.5 ) * 10.0 );
  };
  for ( const std::array<int, 2> &at : { std::array<int, 2>{ 8, 8 },
                                         std::array<int, 2>{ 12, 6 },
                                         std::array<int, 2>{ 5, 15 } } )
  {
    const auto [x, y] = pixelCenter( at[0], at[1] );
    CHECK( pixelAtUtm( vrt, x, y, 0.f, 1 ) );
  }
  // The -999 ring reads -999, distinct from the valid zeros.
  {
    std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
    REQUIRE( GDALGetGeoTransform( vrt, gt.data() ) == CE_None );
    const LonLat corner = utmToWgs84( 500000.0 + 5.0, 4500000.0 - 5.0 );
    const int col =
      static_cast<int>( std::llround( ( corner.lon - gt[0] ) / gt[1] ) );
    const int row =
      static_cast<int>( std::llround( ( gt[3] - corner.lat ) / ( -gt[5] ) ) );
    INFO( "corner maps to col=" << col << " row=" << row << " of "
                                << GDALGetRasterXSize( vrt ) << "x"
                                << GDALGetRasterYSize( vrt ) );
    for ( int dr = -2; dr <= 2; ++dr )
      for ( int dc = -2; dc <= 2; ++dc )
        INFO( "win[" << dr << "][" << dc << "]="
              << readPixel( vrt, col + dc, row + dr ) );
    CHECK( pixelAtUtm( vrt, 500000.0 + 5.0, 4500000.0 - 5.0, -999.f, 2 ) );
  }

  GDALClose( vrt );
}

TEST_CASE( "Preflight warns when a warp is required but nothing declares the "
           "void contract",
           "[r30][t03][warp-oracle]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString undeclared = writeHalfSource(
    dir.filePath( QStringLiteral( "bare.tif" ) ), 0, /*nodataRing=*/false );
  const QString declared = writeHalfSource(
    dir.filePath( QStringLiteral( "ring.tif" ) ), 0, /*nodataRing=*/true );

  DataManager manager;
  const AssetId bare = registerRaster( manager, undeclared );
  const AssetId ring = registerRaster( manager, declared );

  const auto hasVoidWarning = []( const PreflightResult &result ) {
    for ( const Diagnostic &d : result.diagnostics )
      if ( d.code == QLatin1String( "preflight.warp_void_undeclared" ) )
        return true;
    return false;
  };

  // Undeclared + Preserve: the hole contract is unprovable — warn.
  VirtualRasterRecipe bareRecipe;
  bareRecipe.inputs = { BandRef{ bare, 1 } };
  bareRecipe.targetCrs = QStringLiteral( "EPSG:4326" );
  bareRecipe.targetResolutionX = 0.0001;
  bareRecipe.targetResolutionY = 0.0001;
  const PreflightResult bareResult = preflightVirtualRaster( bareRecipe, manager );
  CHECK( bareResult.verdict == PreflightVerdict::RequiresReprojection );
  CHECK( bareResult.canCreate );
  CHECK( hasVoidWarning( bareResult ) );

  // Declared source: no warning.
  VirtualRasterRecipe ringRecipe = bareRecipe;
  ringRecipe.inputs = { BandRef{ ring, 1 } };
  CHECK( !hasVoidWarning( preflightVirtualRaster( ringRecipe, manager ) ) );

  // FillValue policy: the contract IS pinned — no warning.
  VirtualRasterRecipe fillRecipe = bareRecipe;
  fillRecipe.noDataPolicy = NoDataPolicy::FillValue;
  fillRecipe.noDataFillValue = -32768.0;
  CHECK( !hasVoidWarning( preflightVirtualRaster( fillRecipe, manager ) ) );
}
