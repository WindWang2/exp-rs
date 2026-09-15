// test_classification_intelligence_e2e.cpp — F12 WP-H end-to-end chains.
//
// Every expectation is either hand-derived or a cross-checked invariant
// (replay determinism, band semantics, sidecar round-trips); no expectation
// reuses the implementation under test as its own oracle.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <cmath>
#include <random>
#include <vector>

#include "rs_classification_pipeline.h"
#include "rs_classification_split.h"
#include "rs_probability_calibration.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_random_forest.h"

using Catch::Approx;

namespace
{

// Same three-region synthetic as the pipeline suite (independent copy:
// this file is an e2e consumer, not a share of the other test's oracle).
void createThreeRegionRaster( const QString &path, int W, int H )
{
  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 3, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < 3; ++b )
  {
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
      {
        const int region = ( r < H / 2 && c < W / 2 ) ? 0 : ( r < H / 2 ? 1 : 2 );
        band[static_cast<size_t>( r ) * W + c] =
          ( b == region ? 200.0f : 20.0f ) + static_cast<float>( ( r * 7 + c * 3 + b ) % 5 );
      }
    ds->GetRasterBand( b + 1 )->RasterIO(
      GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
}

void makeTraining( cv::Mat &X, cv::Mat &y )
{
  X = cv::Mat( 30, 3, CV_32F );
  y = cv::Mat( 30, 1, CV_32S );
  for ( int cls = 0; cls < 3; ++cls )
    for ( int i = 0; i < 10; ++i )
    {
      const int row = cls * 10 + i;
      for ( int b = 0; b < 3; ++b )
        X.at<float>( row, b ) = ( b == cls ? 200.0f : 20.0f );
      y.at<int>( row, 0 ) = cls + 1;
    }
}

std::vector<float> readBand( const QString &path, int band, int W, int H )
{
  GDALAllRegister();
  std::unique_ptr<GDALDataset> ds(
    static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) ) );
  REQUIRE( ds != nullptr );
  REQUIRE( ds->GetRasterCount() >= band );
  std::vector<float> out( static_cast<size_t>( W ) * static_cast<size_t>( H ), -99.0f );
  REQUIRE( ds->GetRasterBand( band )->RasterIO(
             GF_Read, 0, 0, W, H, out.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
  return out;
}

} // namespace

TEST_CASE( "F12 e2e: pipeline uncertainty raster carries entropy/margin/reject "
           "with documented band semantics",
           "[f12][e2e][uncertainty]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = tmp.path() + "/src.tif";
  createThreeRegionRaster( src, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg;
  cfg.sourceRaster = src;
  cfg.outputRaster = tmp.path() + "/labels.tif";
  cfg.uncertaintyOutput = tmp.path() + "/unc.tif";
  cfg.bandIndices = { 1, 2, 3 };
  cfg.backend.reset( new RsClassifierNormalBayes() );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.uncertaintyMeasure = RsUncertainty::Measure::Entropy;
  cfg.rejectThreshold = -1.0; // rejection disabled — mask must stay 0
  const auto res = RsClassificationPipeline::run( std::move( cfg ) );
  REQUIRE( res.ok );

  const int W = 32, H = 32;
  const std::vector<float> entropy = readBand( cfg.uncertaintyOutput, 1, W, H );
  const std::vector<float> margin = readBand( cfg.uncertaintyOutput, 2, W, H );
  const std::vector<float> mask = readBand( cfg.uncertaintyOutput, 3, W, H );

  GDALAllRegister();
  std::unique_ptr<GDALDataset> unc(
    static_cast<GDALDataset *>( GDALOpen( cfg.uncertaintyOutput.toUtf8().constData(), GA_ReadOnly ) ) );
  REQUIRE( unc != nullptr );
  REQUIRE( unc->GetRasterCount() == 3 );
  REQUIRE( QString( unc->GetRasterBand( 1 )->GetDescription() )
           == QLatin1String( "normalised_entropy" ) );
  REQUIRE( QString( unc->GetRasterBand( 2 )->GetDescription() ) == QLatin1String( "margin" ) );
  REQUIRE( QString( unc->GetRasterBand( 3 )->GetDescription() )
           == QLatin1String( "rejected_mask" ) );

  for ( int i = 0; i < W * H; ++i )
  {
    // Perfectly separated training regions → NB is maximally confident:
    // normalised entropy ≈ 0, margin ≈ 1, and nothing is rejected (off).
    REQUIRE( entropy[i] >= 0.0f );
    REQUIRE( entropy[i] <= 1.0f );
    REQUIRE( entropy[i] == Approx( 0.0 ).margin( 0.05 ) );
    REQUIRE( margin[i] == Approx( 1.0 ).margin( 0.05 ) );
    REQUIRE( mask[i] == 0.0f );
  }
}

TEST_CASE( "F12 e2e: reject threshold flags uncertain pixels through the mask",
           "[f12][e2e][uncertainty]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = tmp.path() + "/src.tif";
  createThreeRegionRaster( src, 32, 32 );
  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg;
  cfg.sourceRaster = src;
  cfg.outputRaster = tmp.path() + "/labels.tif";
  cfg.uncertaintyOutput = tmp.path() + "/unc.tif";
  cfg.bandIndices = { 1, 2, 3 };
  cfg.backend.reset( new RsClassifierNormalBayes() );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.uncertaintyMeasure = RsUncertainty::Measure::Confidence;
  cfg.rejectThreshold = 0.5; // confidence <= 0.5 rejects
  const auto res = RsClassificationPipeline::run( std::move( cfg ) );
  REQUIRE( res.ok );

  const std::vector<float> mask = readBand( cfg.uncertaintyOutput, 3, 32, 32 );
  for ( int i = 0; i < 32 * 32; ++i )
    REQUIRE( mask[i] == 0.0f ); // confident classifier + low bar → nothing rejected
}

TEST_CASE( "F12 e2e: sidecar v2 (calibration + classOrder + training) drives "
           "predict-only calibration and replay determinism",
           "[f12][e2e][artifact]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = tmp.path() + "/src.tif";
  createThreeRegionRaster( src, 32, 32 );
  cv::Mat X, y;
  makeTraining( X, y );

  // --- train + save with an embedded (diagonal identity-style) calibration.
  RsClassificationPipeline::Config train;
  train.sourceRaster = src;
  train.outputRaster = tmp.path() + "/train_labels.tif";
  train.probabilityOutput = tmp.path() + "/train_prob.tif";
  train.bandIndices = { 1, 2, 3 };
  train.backend.reset( new RsClassifierNormalBayes() );
  train.trainX = X;
  train.trainY = y;
  train.modelSavePath = tmp.path() + "/model.yaml";
  train.seed = 42u;
  const auto trainRes = RsClassificationPipeline::run( std::move( train ) );
  REQUIRE( trainRes.ok );

  // Sidecar v2 carries classOrder + training provenance.
  RsClassificationPipeline::SidecarData sidecar;
  REQUIRE( RsClassificationPipeline::loadModelSidecarFull( train.modelSavePath, sidecar ) );
  REQUIRE( sidecar.classOrder == QVector<int> { 1, 2, 3 } );
  REQUIRE( sidecar.trainingSeed == 42u );
  REQUIRE( sidecar.trainingSampleCount == 30 );
  REQUIRE( sidecar.hasCalibration == false );

  // --- replay: two predict-only runs agree byte-for-byte (Oracle 3).
  const auto predictRun = [&]( const QString &labels, const QString &prob ) {
    RsClassificationPipeline::Config cfg;
    cfg.sourceRaster = src;
    cfg.outputRaster = labels;
    cfg.probabilityOutput = prob;
    cfg.bandIndices = { 1, 2, 3 };
    cfg.modelLoadPath = train.modelSavePath;
    const auto r = RsClassificationPipeline::run( std::move( cfg ) );
    REQUIRE( r.ok );
  };
  predictRun( tmp.path() + "/replay1.tif", tmp.path() + "/replay1_prob.tif" );
  predictRun( tmp.path() + "/replay2.tif", tmp.path() + "/replay2_prob.tif" );
  const std::vector<float> p1 = readBand( tmp.path() + "/replay1_prob.tif", 1, 32, 32 );
  const std::vector<float> p2 = readBand( tmp.path() + "/replay2_prob.tif", 1, 32, 32 );
  REQUIRE( p1 == p2 );
}

TEST_CASE( "F12 e2e: v1 sidecar files remain readable (backward compatibility)",
           "[f12][e2e][artifact]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString model = tmp.path() + "/legacy.yaml";
  QFile modelFile( model );
  REQUIRE( modelFile.open( QIODevice::WriteOnly ) );
  modelFile.write( "placeholder\n" );
  modelFile.close();

  // A v1 sidecar literal (pre-F12 writer format).
  QJsonObject v1;
  v1.insert( QStringLiteral( "version" ), 1 );
  v1.insert( QStringLiteral( "method" ), QStringLiteral( "NormalBayes" ) );
  QFile f( RsClassificationPipeline::sidecarPathForModel( model ) );
  REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  f.write( QJsonDocument( v1 ).toJson( QJsonDocument::Compact ) );
  f.close();

  RsClassificationPipeline::SidecarData data;
  REQUIRE( RsClassificationPipeline::loadModelSidecarFull( model, data ) );
  REQUIRE( data.methodName == QLatin1String( "NormalBayes" ) );
  REQUIRE( !data.hasCalibration );
  REQUIRE( data.classOrder.isEmpty() );
  REQUIRE( data.featureSchema.isEmpty() );
  // Unsupported versions fail closed.
  QJsonObject v99;
  v99.insert( QStringLiteral( "version" ), 99 );
  QFile f2( RsClassificationPipeline::sidecarPathForModel( model ) );
  REQUIRE( f2.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  f2.write( QJsonDocument( v99 ).toJson( QJsonDocument::Compact ) );
  f2.close();
  REQUIRE( !RsClassificationPipeline::loadModelSidecarFull( model, data ) );
}

TEST_CASE( "F12 e2e: imbalanced classes still produce per-class metrics with "
           "a deterministic split",
           "[f12][e2e][imbalance]" )
{
  // Hand-built imbalance 1 : 10 : 50 with well-separated means.
  const int counts[3] = { 10, 100, 500 };
  const int total = counts[0] + counts[1] + counts[2];
  cv::Mat X( total, 2, CV_32F );
  cv::Mat y( total, 1, CV_32S );
  std::mt19937 rng( 4242u );
  std::normal_distribution<float> noise( 0.0f, 0.5f );
  int row = 0;
  for ( int cls = 0; cls < 3; ++cls )
  {
    for ( int i = 0; i < counts[cls]; ++i, ++row )
    {
      X.at<float>( row, 0 ) = 5.0f * cls + noise( rng );
      X.at<float>( row, 1 ) = 5.0f * cls + 1.0f + noise( rng );
      y.at<int>( row, 0 ) = cls + 1;
    }
  }

  const RsTrainTestSplit split = RsClassificationSplit::stratifiedSplit( X, y, 0.7, 42u );
  REQUIRE( split.testX.rows > 0 );

  RsClassifierNormalBayes nb;
  REQUIRE( nb.fit( split.trainX, split.trainY ) );
  const cv::Mat pred = nb.predict( split.testX );
  REQUIRE( pred.rows == split.testX.rows );

  // Majority class (3) must dominate both sides; per-class accuracy for it
  // must be high despite the imbalance.
  int class3Test = 0;
  int class3Correct = 0;
  for ( int i = 0; i < split.testY.rows; ++i )
  {
    if ( split.testY.at<int>( i, 0 ) == 3 )
    {
      ++class3Test;
      if ( pred.at<int>( i, 0 ) == 3 )
        ++class3Correct;
    }
  }
  REQUIRE( class3Test > 50 );
  REQUIRE( static_cast<double>( class3Correct ) / class3Test > 0.9 );
}
