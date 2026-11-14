// test_r30_t01_sampling_budget.cpp — R30-T01 WP04/WP05/WP06: cancellable
// bounded-window ROI rasterization (bit-exact against the single-window
// burn), candidate-budget admission before the candidate index is built,
// column-span band reads that must not move a value, and the
// no-usable-partial-results contract on pass-2 cancellation.
#include "rs_pixel_rasterizer.h"
#include "rs_training_data_extraction.h"
#include "qgsgeometry.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace
{

/// GDAL GTiff fixture writer (float32, 1 band) with per-pixel values.
bool createRaster( const QString &path, int W, int H,
                   const std::function<float( int, int )> &value )
{
  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return false;
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  std::vector<float> row( W );
  for ( int y = 0; y < H; ++y )
  {
    for ( int x = 0; x < W; ++x )
      row[x] = value( x, y );
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, W, 1, row.data(), W, 1,
                                      GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
  return true;
}

/// Map-space corner of a pixel-space point under an affine GDAL GT — the
/// rotated-transform case needs its geometry in the raster's map frame.
QString pixelCorner( const double gt[6], int c, int r )
{
  return QStringLiteral( "%1 %2" )
    .arg( gt[0] + gt[1] * c + gt[2] * r, 0, 'f', 6 )
    .arg( gt[3] + gt[4] * c + gt[5] * r, 0, 'f', 6 );
}

QSet<quint64> burnLegacy( const QgsGeometry &geom, const double gt[6], int W, int H, bool *ok )
{
  // The original 4-argument entry point — single geometry-bbox window, no
  // cancel hook. Must stay bit-identical to the chunked path.
  return RsPixelRasterizer::rasterize( geom, gt, W, H, ok );
}

QSet<quint64> burnChunked( const QgsGeometry &geom, const double gt[6], int W, int H,
                           qint64 maxWindowPixels, bool *ok )
{
  return RsPixelRasterizer::rasterize( geom, gt, W, H, ok,
                                       std::function<bool()> {}, nullptr,
                                       maxWindowPixels );
}

} // namespace

// --- WP04: chunked burn equivalence ---------------------------------------------

TEST_CASE( "chunked rasterization is bit-identical to the single-window burn",
           "[classify][rasterize][r30t01]" )
{
  // North-up GT over a 200x160 raster; the geometry spans most of it, so
  // tiny window budgets cut it into many row chunks.
  const int W = 200, H = 160;
  const double gt[6] = { 0, 1, 0, 160, 0, -1 };

  struct Case
  {
    const char *name;
    QString wkt;
  };
  const Case cases[] = {
    { "solid rectangle", QStringLiteral( "POLYGON((10 20, 190 20, 190 140, 10 140, 10 20))" ) },
    { "polygon with hole", QStringLiteral(
        "POLYGON((10 20, 190 20, 190 140, 10 140, 10 20),"
        "(50 60, 100 60, 100 110, 50 110, 50 60))" ) },
    { "diagonal band", QStringLiteral(
        "POLYGON((0 0, 40 0, 200 120, 200 160, 160 160, 0 40, 0 0))" ) },
    { "thin vertical stripe", QStringLiteral(
        "POLYGON((99 0, 101 0, 101 160, 99 160, 99 0))" ) },
  };
  for ( const Case &test : cases )
  {
    CAPTURE( test.name );
    const QgsGeometry geom = QgsGeometry::fromWkt( test.wkt );
    REQUIRE( !geom.isNull() );
    bool okLegacy = false, okChunked = false;
    const QSet<quint64> legacy = burnLegacy( geom, gt, W, H, &okLegacy );
    // Window budgets that force chunk boundaries across the geometry:
    // ~8 rows, ~33 rows, and the full window in one chunk.
    for ( const qint64 budget : { qint64 { 8 * W }, qint64 { 33 * W },
                                  qint64 { qint64( W ) * H } } )
    {
      CAPTURE( budget );
      const QSet<quint64> chunked = burnChunked( geom, gt, W, H, budget, &okChunked );
      CHECK( okLegacy );
      CHECK( okChunked );
      CHECK( chunked == legacy );
    }
  }
}

TEST_CASE( "chunked equivalence holds under rotated and sheared transforms",
           "[classify][rasterize][r30t01]" )
{
  const int W = 120, H = 120;
  // Rotation+shear: gt = [originX, cosθ·px, -sinθ·px + shear, originY, sinθ·px, cosθ·px]
  // (GDAL affine convention). 30° rotation plus an x-per-y shear term.
  const double gt[6] = { 100.0, 0.8 * 1.0, -0.4 * 1.0 + 0.05, 200.0, 0.4 * 1.0, 0.8 * 1.0 };
  // Pixels cols 10..60, rows 20..70, expressed in the raster's (rotated)
  // map frame — a raw axis-aligned WKT here would sit outside the footprint.
  const QgsGeometry geom = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((%1,%2,%3,%4,%1))" )
      .arg( pixelCorner( gt, 10, 20 ), pixelCorner( gt, 60, 20 ),
            pixelCorner( gt, 60, 70 ), pixelCorner( gt, 10, 70 ) ) );
  REQUIRE( !geom.isNull() );
  bool okLegacy = false, okChunked = false;
  const QSet<quint64> legacy = burnLegacy( geom, gt, W, H, &okLegacy );
  REQUIRE( okLegacy );
  REQUIRE( legacy.size() > 100 ); // the geometry genuinely burns
  for ( const qint64 budget : { qint64 { 5 * W }, qint64 { 20 * W } } )
  {
    const QSet<quint64> chunked = burnChunked( geom, gt, W, H, budget, &okChunked );
    CHECK( okChunked );
    CHECK( chunked == legacy );
  }
}

TEST_CASE( "cancellation is reported as cancelled, never as empty success",
           "[classify][rasterize][r30t01]" )
{
  const int W = 100, H = 100;
  const double gt[6] = { 0, 1, 0, 100, 0, -1 };
  const QgsGeometry geom = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((0 0, 100 0, 100 100, 0 100, 0 0))" ) );

  SECTION( "cancelled before any burn" )
  {
    bool ok = true;
    bool cancelled = false;
    const auto indices = RsPixelRasterizer::rasterize(
      geom, gt, W, H, &ok, []() { return true; }, &cancelled );
    CHECK( indices.isEmpty() );
    CHECK_FALSE( ok );
    CHECK( cancelled );
  }
  SECTION( "cancelled between chunks" )
  {
    // First poll passes (the pre-work check), the second (between chunks)
    // cancels: with a 10-row budget the burn runs in many chunks.
    int calls = 0;
    bool ok = true;
    bool cancelled = false;
    const auto indices = RsPixelRasterizer::rasterize(
      geom, gt, W, H, &ok, [&calls]() { return ++calls > 1; }, &cancelled,
      qint64 { 10 * W } );
    CHECK( indices.isEmpty() );
    CHECK_FALSE( ok );
    CHECK( cancelled );
  }
  SECTION( "no cancel hook keeps the legacy contract" )
  {
    // Same overload, null hook: identical result to the 4-argument form.
    bool ok = false;
    const auto indices = RsPixelRasterizer::rasterize(
      geom, gt, W, H, &ok, std::function<bool()> {}, nullptr, 0 );
    CHECK( ok );
    CHECK( indices.size() > 9000 );
  }
}

// --- WP05: candidate-budget admission --------------------------------------------

TEST_CASE( "maxCandidates refuses before materializing the candidate index",
           "[classification][training-data][r30t01]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  const QString raster = tmp.path() + "/src.tif";
  REQUIRE( createRaster( raster, 16, 16, []( int, int ) { return 1.0f; } ) );

  RsTrainingGeometry roi;
  roi.classId = 1;
  roi.geometry = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((0 0, 16 0, 16 16, 0 16, 0 0))" ) );

  RsTrainingDataExtraction::Options options;
  options.maxCandidates = 16; // the 16x16 ROI burns far more pixels

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, { roi }, options );
  REQUIRE_FALSE( res.ok );
  CHECK( res.error == RsTrainingDataResult::Error::BudgetExceeded );
  // Nothing partial survives the refusal.
  CHECK( res.X.empty() );
  CHECK( res.y.empty() );
  CHECK( res.sampleGroupIds.empty() );
  CHECK( res.classCounts.isEmpty() );
  CHECK( res.errorMessage.contains( QStringLiteral( "budget" ) ) );

  // A budget above the candidate count behaves exactly like unlimited.
  RsTrainingDataExtraction::Options generous = options;
  generous.maxCandidates = 100000;
  const RsTrainingDataResult ok = RsTrainingDataExtraction::extract(
    raster, { 1 }, { roi }, generous );
  CHECK( ok.ok );
}

// --- WP06: span reads + failure-state closure -------------------------------------

TEST_CASE( "column-span reads return the exact per-pixel values",
           "[classification][training-data][r30t01]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  const int W = 64, H = 32;
  const QString raster = tmp.path() + "/src.tif";
  // Value encodes (row, col) uniquely: v = row * 1000 + col.
  REQUIRE( createRaster( raster, W, H,
                         []( int x, int y ) { return float( y * 1000 + x ); } ) );

  // A narrow vertical ROI far from column 0: rows 2..29, columns 51..54 —
  // every read must be span-offset correctly or the values shift.
  RsTrainingGeometry roi;
  roi.classId = 3;
  roi.geometry = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((51 2, 55 2, 55 30, 51 30, 51 2))" ) );

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract( raster, { 1 }, { roi } );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows > 0 );
  // Sample order: single class, ascending pixel index (row-major) — so the
  // FIRST row of X is pixel (2,51)-ish and every row's value decodes back
  // to a pixel inside the ROI span.
  for ( int s = 0; s < res.X.rows; ++s )
  {
    const float v = res.X.at<float>( s, 0 );
    const int row = int( v ) / 1000;
    const int col = int( v ) % 1000;
    CHECK( row >= 2 );
    CHECK( row <= 29 );
    CHECK( col >= 51 );
    CHECK( col <= 54 );
    CHECK( res.y.at<int>( s, 0 ) == 3 );
  }
  // The exact kept set equals the same extraction run with a cached index
  // set (both paths must agree bit-for-bit on values and row order).
  RsTrainingGeometry cached;
  cached.classId = 3;
  bool burnOk = false;
  const QSet<quint64> burned = RsPixelRasterizer::rasterize(
    roi.geometry, std::array<double, 6> { 0, 1, 0, double( H ), 0, -1 }.data(), W, H,
    &burnOk );
  REQUIRE( burnOk );
  cached.pixelIndices = QVector<quint64>( burned.begin(), burned.end() );
  const RsTrainingDataResult resCached = RsTrainingDataExtraction::extract(
    raster, { 1 }, { cached } );
  REQUIRE( resCached.ok );
  REQUIRE( resCached.X.rows == res.X.rows );
  for ( int s = 0; s < res.X.rows; ++s )
    CHECK( resCached.X.at<float>( s, 0 ) == res.X.at<float>( s, 0 ) );
}

TEST_CASE( "pass-2 cancellation leaves no usable partial results",
           "[classification][training-data][r30t01]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  const QString raster = tmp.path() + "/src.tif";
  REQUIRE( createRaster( raster, 8, 8, []( int, int ) { return 1.0f; } ) );

  RsTrainingGeometry roi;
  roi.classId = 1;
  roi.geometry = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((0 0, 8 0, 8 8, 0 8, 0 0))" ) );

  // Progress fractions: burn ≤ 0.4, validity pass 0.4..0.65 — accept
  // everything below 0.99 and cancel at the FIRST pass-2 checkpoint
  // (bandFraction 0.65+0.35·1/1 = 1.0), which fires AFTER X/y/groupIds
  // were populated — the exact window where they used to leak.
  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, { roi }, RsTrainingDataExtraction::Options(),
    []( double fraction, const QString & ) { return fraction < 0.99; } );
  REQUIRE_FALSE( res.ok );
  CHECK( res.error == RsTrainingDataResult::Error::Cancelled );
  CHECK( res.X.empty() );
  CHECK( res.y.empty() );
  CHECK( res.sampleGroupIds.empty() );
  CHECK( res.classCounts.isEmpty() );
}
