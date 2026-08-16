// Phase 10A Task 10.6 — Jeffries-Matusita separability tests.
//
// Validates RsJmSeparability::pairJm() across four regimes:
//   - identical distributions (JM ~ 0)
//   - completely separated (JM ~ 2)
//   - moderate overlap (0.8 < JM < 1.9)
//   - degenerate samples (epsilon ridge keeps result in [0, 2])

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_jm_separability.h"
#include "rs_training_data_extraction.h"
#include "qgsgeometry.h"

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

using Catch::Approx;

TEST_CASE( "JM: identical distributions yield ~0", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 100, 3, CV_32F );
  cv::randn( a, 50.0, 10.0 );
  cv::Mat b = a.clone();
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm == Approx( 0.0 ).margin( 0.05 ) );
}

TEST_CASE( "JM: completely separated yields ~2", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 100, 3, CV_32F );
  cv::randn( a, 50.0, 2.0 );
  cv::Mat b( 100, 3, CV_32F );
  cv::randn( b, 200.0, 2.0 );
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm > 1.95 );
}

TEST_CASE( "JM: moderate overlap yields between 0.8 and 1.9", "[classify][jm]" )
{
  cv::theRNG() = cv::RNG( 42 );
  cv::Mat a( 500, 3, CV_32F );
  cv::randn( a, 50.0, 8.0 );
  cv::Mat b( 500, 3, CV_32F );
  cv::randn( b, 65.0, 8.0 );
  double jm = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm > 0.8 );
  REQUIRE( jm < 1.9 );
}

TEST_CASE( "JM: samples-per-class below band count is reported as n/a (#287)", "[classify][jm]" )
{
  // 2 samples in 3 bands: both class covariances are rank-deficient and the
  // determinant-based term would underflow into the 1e-300 floor, spuriously
  // saturating JM to 2.0. The pair now returns -1.0 (rendered as "n/a" by
  // the JM matrix) instead of a confident value.
  cv::Mat a( 2, 3, CV_32F );
  a.setTo( 50.0 );
  cv::Mat b( 2, 3, CV_32F );
  b.setTo( 60.0 );
  REQUIRE( RsJmSeparability::pairJm( a, b ) == -1.0 );
}

namespace
{

/// Write a single-band Float32 GeoTIFF; valueFn(r, c) gives the value.
/// GT is {0,1,0,H,0,-1} so pixel (r,c) center maps to (c+0.5, H-r-0.5).
void createRaster( const QString &path, int W, int H,
                   const std::function<float( int, int )> &valueFn,
                   bool setNodata = false, double nodata = -9999.0 )
{
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(),
                                 W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int r = 0; r < H; ++r )
    for ( int c = 0; c < W; ++c )
      band[static_cast<size_t>( r * W + c )] = valueFn( r, c );
  GDALRasterBand *rb = ds->GetRasterBand( 1 );
  rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
  if ( setNodata )
    rb->SetNoDataValue( nodata );
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

/// Axis-aligned square in map coordinates: cols [c0,c1), rows [r0,r1) of a
/// raster with GT {0,1,0,H,0,-1}.
QgsGeometry squareGeom( double x0, double y0, double x1, double y1 )
{
  QgsPolygonXY polygon;
  polygon.append( { QgsPointXY( x0, y0 ), QgsPointXY( x1, y0 ),
                    QgsPointXY( x1, y1 ), QgsPointXY( x0, y1 ),
                    QgsPointXY( x0, y0 ) } );
  return QgsGeometry::fromPolygonXY( polygon );
}

RsTrainingGeometry roi( int classId, const QgsGeometry &geom )
{
  RsTrainingGeometry tg;
  tg.classId = classId;
  tg.geometry = geom;
  return tg;
}

} // namespace

TEST_CASE( "JM: computeAll splits extraction output into per-class buckets",
           "[classify][jm]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  // Rows 0-3 hold 50, rows 4-7 hold 200 (one band).
  createRaster( raster, 8, 8,
                []( int r, int ) { return r < 4 ? 50.0f : 200.0f; } );

  // Class 1: rows 0-3 (32 px). Class 2: rows 4-7 (32 px). Class 3: single
  // pixel (4,4) inside class 2's block → extraction dedups, last class wins.
  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 8, 8, 4 ) ),
    roi( 2, squareGeom( 0, 4, 8, 0 ) ),
    roi( 3, squareGeom( 4, 4, 5, 3 ) ),
  };

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms );
  REQUIRE( res.ok );
  REQUIRE( res.classCounts.value( 1 ) == 32 );
  REQUIRE( res.classCounts.value( 2 ) == 31 );
  REQUIRE( res.classCounts.value( 3 ) == 1 );

  const QHash<QPair<int, int>, double> jm = RsJmSeparability::computeAll( res.X, res.y );

  // Single-sample class 3 must be skipped; only the {1,2} pair remains.
  REQUIRE( jm.size() == 1 );
  REQUIRE( jm.contains( qMakePair( 1, 2 ) ) );

  // Independent literal: 32 samples of 50 vs 31 samples of 200 → JM ~ 2,
  // and computeAll must agree bit-for-bit with pairJm on the same values.
  cv::Mat a( 32, 1, CV_32F );
  a.setTo( 50.0f );
  cv::Mat b( 31, 1, CV_32F );
  b.setTo( 200.0f );
  const double lit = RsJmSeparability::pairJm( a, b );
  REQUIRE( jm.value( qMakePair( 1, 2 ) ) == Approx( lit ).margin( 1e-9 ) );
  REQUIRE( lit > 1.95 );
}

TEST_CASE( "JM: NoData pixels are filtered before separability (ADR 0055)",
           "[classify][jm]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  // Column 0 is NoData (-9999); every other pixel is 50.
  createRaster( raster, 8, 8,
                []( int, int c ) { return c == 0 ? -9999.0f : 50.0f; },
                true, -9999.0 );

  // Class 1 covers the whole raster, class 2 the 4x4 block rows 4-7 /
  // cols 4-7. Extraction drops the 8 NoData column pixels and dedups the
  // overlap (last class wins) → class 1 keeps 64 - 8 - 16 = 40, class 2
  // keeps 16. Filtered, both classes are all-50 → JM ~ 0; if the -9999
  // column leaked into class 1 the pair would be far from identical
  // (JM → ~2).
  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 8, 8, 0 ) ),
    roi( 2, squareGeom( 4, 4, 8, 0 ) ),
  };

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms );
  REQUIRE( res.ok );
  REQUIRE( res.classCounts.value( 1 ) == 40 );
  REQUIRE( res.classCounts.value( 2 ) == 16 );

  const QHash<QPair<int, int>, double> jm = RsJmSeparability::computeAll( res.X, res.y );
  REQUIRE( jm.size() == 1 );
  REQUIRE( jm.value( qMakePair( 1, 2 ) ) < 0.05 );

  // Contrast: with source NoData honored off, the -9999 column leaks into
  // class 1's bucket and the identical-distribution property is destroyed.
  RsTrainingDataExtraction::Options raw;
  raw.ignore.useSourceNodata = false;
  const RsTrainingDataResult resRaw = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms, raw );
  REQUIRE( resRaw.ok );
  const QHash<QPair<int, int>, double> jmRaw = RsJmSeparability::computeAll( resRaw.X, resRaw.y );
  REQUIRE( jmRaw.value( qMakePair( 1, 2 ) ) > 1.5 );
}

TEST_CASE( "JM: samples-per-class below band count returns -1.0 instead of saturating to 2.0",
           "[classify][jm]" )
{
  // #287 - hyperspectral regime: 12 samples in 200 bands makes both class
  // covariances rank-deficient; the old det-floor arithmetic drove JM to 2.0
  // ("perfectly separable") for overlapping distributions.
  cv::Mat a( 12, 200, CV_32F );
  cv::Mat b( 12, 200, CV_32F );
  cv::randu( a, cv::Scalar( 0.0 ), cv::Scalar( 1.0 ) );
  cv::randu( b, cv::Scalar( 0.0 ), cv::Scalar( 1.0 ) );
  REQUIRE( RsJmSeparability::pairJm( a, b ) == -1.0 );

  // Well-sampled case must be unchanged: 300 samples in 3 bands.
  cv::Mat a2( 300, 3, CV_32F );
  cv::Mat b2( 300, 3, CV_32F );
  cv::randu( a2, cv::Scalar( 0.0 ), cv::Scalar( 1.0 ) );
  cv::randu( b2, cv::Scalar( 0.0 ), cv::Scalar( 1.0 ) );
  const double jm = RsJmSeparability::pairJm( a2, b2 );
  REQUIRE( jm >= 0.0 );
  REQUIRE( jm <= 2.0 );
}
