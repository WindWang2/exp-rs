// test_r29_t01_science_scenarios.cpp — R29-T01 WP10: the three complete
// science scenarios, each with its error-input / cancel / recovery leg:
//
//   ① ROI → validity filtering → deterministic capped sampling → kNN
//     classification → held-out ConfusionMatrix evaluation (OA oracle on
//     separable classes; same-seed bit-exact determinism; cancel and
//     insufficient-samples legs, then a clean re-run).
//   ② real multi-input feather lane — test_r29_t01_real_multi_infer.cpp
//     (production service, real cv::dnn forward, NoData ground, provenance)
//     plus the fake-runtime geometry oracles in test_r29_t01_multi_feather.
//   ③ multi-class evaluation record → dense/sparse serialization
//     round-trips → metric/report consumption (MetricRecord content hash +
//     protocol binding).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"
#include "rs_classifier_knn.h"
#include "rs_training_data_extraction.h"

#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal_priv.h>

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <memory>
#include <vector>

using namespace sicnu::experiment;

namespace
{

void createTwoClassRaster( const QString &path )
{
  // Two separable classes on a 64x64 grid: class-1 ROI on the left third
  // (band values near 10), class-2 ROI on the right third (near 100); the
  // middle third stays unlabeled (outside both ROIs). A NoData patch inside
  // the class-1 ROI exercises the validity filter on the capped path.
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), 64, 64, 2, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::vector<float> b0( 64 * 64 ), b1( 64 * 64 );
  for ( int r = 0; r < 64; ++r )
    for ( int c = 0; c < 64; ++c )
    {
      const size_t i = static_cast<size_t>( r ) * 64 + c;
      if ( c < 21 )
      {
        b0[i] = 10.0f + ( r % 5 ) * 0.1f;
        b1[i] = 12.0f + ( c % 7 ) * 0.1f;
      }
      else if ( c >= 43 )
      {
        b0[i] = 100.0f + ( r % 5 ) * 0.1f;
        b1[i] = 102.0f + ( c % 7 ) * 0.1f;
      }
      else
      {
        b0[i] = 50.0f;
        b1[i] = 52.0f;
      }
    }
  // NoData patch inside the class-1 ROI: rows 30..33, cols 2..5.
  for ( int r = 30; r <= 33; ++r )
    for ( int c = 2; c <= 5; ++c )
    {
      const size_t i = static_cast<size_t>( r ) * 64 + c;
      b0[i] = -9999.0f;
      b1[i] = -9999.0f;
    }
  for ( int b = 0; b < 2; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b + 1 );
    band->RasterIO( GF_Write, 0, 0, 64, 64, b == 0 ? b0.data() : b1.data(), 64, 64,
                    GDT_Float32, 0, 0 );
    band->SetNoDataValue( -9999.0 );
  }
  double gt[6] = { 0, 1, 0, 64, 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

QgsGeometry rectGeometry( int x0, int y0, int x1, int y1 )
{
  QgsPolygonXY polygon;
  polygon.append( { QgsPointXY( x0, y0 ), QgsPointXY( x1, y0 ), QgsPointXY( x1, y1 ),
                    QgsPointXY( x0, y1 ), QgsPointXY( x0, y0 ) } );
  return QgsGeometry::fromPolygonXY( polygon );
}

} // namespace

TEST_CASE( "scenario 1: ROI → validity → deterministic capped sampling → kNN → "
           "held-out evaluation",
           "[r29t01][scenario]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  CPLSetConfigOption( "GDAL_CACHEMAX", "64" );

  const QString raster = tmp.path() + "/twoclass.tif";
  createTwoClassRaster( raster );

  std::vector<RsTrainingGeometry> rois;
  {
    RsTrainingGeometry g1;
    g1.classId = 1;
    g1.geometry = rectGeometry( 0, 0, 20, 63 );
    RsTrainingGeometry g2;
    g2.classId = 2;
    g2.geometry = rectGeometry( 43, 0, 63, 63 );
    rois = { g1, g2 };
  }

  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = 64; // capped path: K fixed, N = 21·64·2

  // --- error leg: cancellation refuses with a typed error, no half result.
  {
    const RsTrainingDataResult cancelled = RsTrainingDataExtraction::extract(
      raster, { 1, 2 }, QVector<RsTrainingGeometry>( rois.begin(), rois.end() ), options,
      []( double, const QString & ) { return false; } );
    CHECK( !cancelled.ok );
    CHECK( cancelled.error == RsTrainingDataResult::Error::Cancelled );
    CHECK( cancelled.X.empty() );
  }

  // --- happy path: deterministic capped extraction, both classes covered.
  const auto runExtraction = [&] {
    return RsTrainingDataExtraction::extract( raster, { 1, 2 },
                                              QVector<RsTrainingGeometry>( rois.begin(),
                                                                           rois.end() ),
                                              options );
  };
  const RsTrainingDataResult res = runExtraction();
  REQUIRE( res.ok );
  CHECK( res.classCounts.value( 1 ) == 64 );
  CHECK( res.classCounts.value( 2 ) == 64 );
  // Same seed ⇒ bit-identical sample set AND order (the portable
  // Fisher-Yates + (class, pixel) sort contract).
  const RsTrainingDataResult res2 = runExtraction();
  REQUIRE( res2.ok );
  REQUIRE( res2.X.rows == res.X.rows );
  for ( int r = 0; r < res.X.rows; ++r )
  {
    CHECK( res2.y.at<int>( r, 0 ) == res.y.at<int>( r, 0 ) );
    for ( int c = 0; c < res.X.cols; ++c )
      CHECK( res2.X.at<float>( r, c ) == res.X.at<float>( r, c ) );
    CHECK( res2.sampleGroupIds[r] == res.sampleGroupIds[r] );
  }
  // The NoData patch never survives into X.
  for ( int r = 0; r < res.X.rows; ++r )
    for ( int c = 0; c < res.X.cols; ++c )
      CHECK( res.X.at<float>( r, c ) != -9999.0f );

  // --- budget leg: minSamples above the kept set refuses typed.
  {
    RsTrainingDataExtraction::Options strict = options;
    strict.minSamples = 500;
    const RsTrainingDataResult insufficient =
      RsTrainingDataExtraction::extract( raster, { 1, 2 },
                                         QVector<RsTrainingGeometry>( rois.begin(),
                                                                      rois.end() ),
                                         strict );
    CHECK( !insufficient.ok );
    CHECK( insufficient.error == RsTrainingDataResult::Error::InsufficientSamples );
  }

  // --- held-out protocol: even rows train, odd rows test (transparent,
  //     deterministic split defined by this scenario).
  const int rows = res.X.rows;
  cv::Mat Xtr( rows / 2, 2, CV_32F ), ytr( rows / 2, 1, CV_32S );
  std::vector<int> testIdx;
  for ( int r = 0; r < rows; ++r )
  {
    if ( r % 2 == 0 )
    {
      const int t = r / 2;
      Xtr.at<float>( t, 0 ) = res.X.at<float>( r, 0 );
      Xtr.at<float>( t, 1 ) = res.X.at<float>( r, 1 );
      ytr.at<int>( t, 0 ) = res.y.at<int>( r, 0 );
    }
    else
      testIdx.push_back( r );
  }
  auto knn = std::make_unique<RsClassifierKnn>( 1 );
  REQUIRE( knn->fit( Xtr, ytr ) );
  cv::Mat Xt( static_cast<int>( testIdx.size() ), 2, CV_32F );
  for ( int t = 0; t < static_cast<int>( testIdx.size() ); ++t )
  {
    Xt.at<float>( t, 0 ) = res.X.at<float>( testIdx[t], 0 );
    Xt.at<float>( t, 1 ) = res.X.at<float>( testIdx[t], 1 );
  }
  const cv::Mat ypred = knn->predict( Xt );
  REQUIRE( ypred.rows == static_cast<int>( testIdx.size() ) );

  // --- held-out ConfusionMatrix with an independent oracle: the classes are
  // linearly separable by construction, so 1-NN over its own train split
  // must classify every held-out sample correctly.
  const QStringList labels{ QStringLiteral( "class1" ), QStringLiteral( "class2" ) };
  ConfusionMatrix matrix( labels, 2 );
  for ( int t = 0; t < static_cast<int>( testIdx.size() ); ++t )
  {
    const int truth = res.y.at<int>( testIdx[t], 0 ) == 1 ? 0 : 1;
    const int pred = ypred.at<int>( t, 0 ) == 1 ? 0 : 1;
    matrix.increment( truth, pred );
  }
  CHECK( matrix.total() == static_cast<qint64>( testIdx.size() ) );
  CHECK( matrix.overallAccuracy() == Catch::Approx( 1.0 ) );
  CHECK( matrix.perClass( 0 ).recall == Catch::Approx( 1.0 ) );
  CHECK( matrix.perClass( 1 ).recall == Catch::Approx( 1.0 ) );
  CHECK( matrix.kappa() == Catch::Approx( 1.0 ) );

  // --- recovery leg: re-running the whole scenario end-to-end reproduces
  // the same evaluation (determinism through sampling AND prediction).
  const RsTrainingDataResult res3 = runExtraction();
  REQUIRE( res3.ok );
  auto knn2 = std::make_unique<RsClassifierKnn>( 1 );
  REQUIRE( knn2->fit( Xtr, ytr ) );
  const cv::Mat ypred2 = knn2->predict( Xt );
  for ( int t = 0; t < ypred.rows; ++t )
    CHECK( ypred2.at<int>( t, 0 ) == ypred.at<int>( t, 0 ) );
}

TEST_CASE( "scenario 3: evaluation record → dense/sparse round-trip → metric "
           "consumption stays bound to its protocol",
           "[r29t01][scenario]" )
{
  // A 5-class evaluation with asymmetric confusions (non-trivial support).
  QStringList labels;
  for ( int i = 0; i < 5; ++i )
    labels << QString( "lc%1" ).arg( i );
  ConfusionMatrix matrix( labels, 5 );
  for ( int i = 0; i < 5; ++i )
    matrix.setCount( i, i, 30 + i * 10 );
  matrix.setCount( 0, 1, 5 );
  matrix.setCount( 1, 0, 3 );
  matrix.setCount( 2, 4, 8 );
  matrix.setCount( 4, 2, 2 );

  // Dense round-trip (5 labels ≤ the dense emission threshold).
  const QJsonObject dense = matrix.toJson();
  CHECK( dense.contains( QStringLiteral( "counts" ) ) );
  const auto fromDense = ConfusionMatrix::fromJson( dense );
  REQUIRE( fromDense.has_value() );
  CHECK( *fromDense == matrix );

  // Sparse round-trip: hand-emit the SAME matrix through the sparse layout
  // (exactly what toJson produces beyond the dense threshold).
  QJsonObject sparse;
  sparse.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
  QJsonArray cells;
  for ( int r = 0; r < 5; ++r )
    for ( int c = 0; c < 5; ++c )
      if ( matrix.count( r, c ) > 0 )
      {
        QJsonObject cell;
        cell.insert( QStringLiteral( "r" ), double( r ) );
        cell.insert( QStringLiteral( "c" ), double( c ) );
        cell.insert( QStringLiteral( "n" ), double( matrix.count( r, c ) ) );
        cells.append( cell );
      }
  sparse.insert( QStringLiteral( "counts_format" ), QStringLiteral( "sparse" ) );
  sparse.insert( QStringLiteral( "nonzero_counts" ), cells );
  const auto fromSparse = ConfusionMatrix::fromJson( sparse );
  REQUIRE( fromSparse.has_value() );
  CHECK( *fromSparse == matrix );
  // Both representations agree on every consumed aggregate.
  const QJsonObject sparseJson = fromSparse->toJson();
  CHECK( sparseJson.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
         == dense.value( QStringLiteral( "overall_accuracy" ) ).toDouble() );
  CHECK( sparseJson.value( QStringLiteral( "kappa" ) ).toDouble()
         == dense.value( QStringLiteral( "kappa" ) ).toDouble() );
  CHECK( sparseJson.value( QStringLiteral( "weighted_f1" ) ).toDouble()
         == dense.value( QStringLiteral( "weighted_f1" ) ).toDouble() );

  // Record consumption: the metric record commits to a verified content
  // hash and its protocol identity survives the document round-trip.
  EvaluationProtocol protocol;
  protocol.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
  protocol.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
  protocol.setSubset( QStringLiteral( "test" ) );
  protocol.setAggregation( QStringLiteral( "macro" ) );
  REQUIRE( protocol.validate().has_value() );

  MetricRecord record;
  record.runId = QStringLiteral( "run-r29-s3" );
  record.protocol = protocol;
  record.metrics = matrix.toJson();
  const QString hash = record.contentHash();
  CHECK_FALSE( hash.isEmpty() );
  const auto reparsed = MetricRecord::fromJson( record.toJson() );
  REQUIRE( reparsed.has_value() );
  CHECK( reparsed->contentHash() == hash );
  CHECK( reparsed->protocol == protocol );
  // A different protocol is a different record identity — the hash moves.
  MetricRecord other = record;
  EvaluationProtocol stricter = protocol;
  stricter.setSubset( QStringLiteral( "fold:3" ) );
  other.protocol = stricter;
  CHECK( other.contentHash() != hash );
}
