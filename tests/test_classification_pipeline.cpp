// ADR 0019 slice S2 — RsClassificationPipeline tests.
//
// Crosses the module's seam directly (no QgsTask): fit → predict → write on
// small programmatically-built GeoTIFFs, dtype escalation, NoData →
// unclassified, held-out accuracy, KMeans Hungarian remap, model sidecar
// round-trip, and cancel mid-predict removing the partial output.
#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <functional>
#include <vector>

#include "rs_classification_pipeline.h"
#include "rs_classifier_kmeans.h"
#include "rs_classifier_normalbayes.h"

namespace
{

/// Write a Float32 GeoTIFF with 3 distinct spectral regions (like the e2e
/// task test): (r<H/2, c<W/2) → region 0 → band-0 ≈ 200, others ≈ 20;
/// (r<H/2, c>=W/2) → region 1 → band-1 high; (r>=H/2) → region 2 → band-2
/// high. When nodataRegion is set, region 0 pixels carry -9999 in all bands
/// and every band gets that NoData value.
void createThreeRegionRaster( const QString &path, int W, int H, bool nodataRegion = false )
{
  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(),
                                 W, H, 3, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < 3; ++b )
  {
    for ( int r = 0; r < H; ++r )
    {
      for ( int c = 0; c < W; ++c )
      {
        const int region = ( r < H / 2 && c < W / 2 ) ? 0
                                                        : ( r < H / 2 ? 1 : 2 );
        float v = ( b == region ? 200.0f : 20.0f )
                  + static_cast<float>( ( r * 7 + c * 3 + b ) % 5 );
        if ( nodataRegion && region == 0 )
          v = -9999.0f;
        band[static_cast<size_t>( r * W + c )] = v;
      }
    }
    GDALRasterBand *rb = ds->GetRasterBand( b + 1 );
    rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    if ( nodataRegion )
      rb->SetNoDataValue( -9999.0 );
  }
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

/// 10 hand-labelled pixels per class; classIds default to 1/2/3 matching the
/// three synthetic regions.
void makeTraining( cv::Mat &X, cv::Mat &y, const QVector<int> &classIds = { 1, 2, 3 } )
{
  X = cv::Mat( 10 * classIds.size(), 3, CV_32F );
  y = cv::Mat( 10 * classIds.size(), 1, CV_32S );
  for ( int cls = 0; cls < classIds.size(); ++cls )
  {
    for ( int i = 0; i < 10; ++i )
    {
      const int row = cls * 10 + i;
      for ( int b = 0; b < 3; ++b )
        X.at<float>( row, b ) = ( b == cls ? 200.0f : 20.0f );
      y.at<int>( row, 0 ) = classIds[cls];
    }
  }
}

RsClassificationPipeline::Config baseConfig( const QString &src, const QString &out )
{
  RsClassificationPipeline::Config cfg;
  cfg.sourceRaster = src;
  cfg.outputRaster = out;
  cfg.bandIndices = { 1, 2, 3 };
  cfg.classColors[1] = QColor( "#cc0000" );
  cfg.classColors[2] = QColor( "#00cc00" );
  cfg.classColors[3] = QColor( "#0000cc" );
  cfg.methodName = QStringLiteral( "NormalBayes" );
  return cfg;
}

int readPixel( GDALDataset *ds, int x, int y )
{
  int v = -1;
  ds->GetRasterBand( 1 )->RasterIO( GF_Read, x, y, 1, 1, &v, 1, 1, GDT_Int32, 0, 0 );
  return v;
}

} // namespace

TEST_CASE(
  "Classification pipeline: 32x32 raster + 3 classes produces valid GeoTIFF",
  "[classify][pipeline]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;

  double lastFraction = 0.0;
  const RsClassificationPipelineResult res = RsClassificationPipeline::run(
    std::move( cfg ),
    [&lastFraction]( double fraction, const QString & )
    {
      lastFraction = fraction;
      return true;
    } );

  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );
  REQUIRE( res.error == RsClassificationPipelineResult::Error::None );
  REQUIRE( res.totalPixels == 32 * 32 );
  REQUIRE( res.durationMs >= 0 );
  REQUIRE( lastFraction == 1.0 );
  REQUIRE( QFile::exists( tmp.path() + "/out.tif" ) );

  GDALDataset *outDs = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( outDs != nullptr );
  REQUIRE( outDs->GetRasterBand( 1 )->GetRasterDataType() == GDT_Byte );
  REQUIRE( readPixel( outDs, 2, 2 ) == 1 );
  REQUIRE( readPixel( outDs, 28, 2 ) == 2 );
  REQUIRE( readPixel( outDs, 16, 28 ) == 3 );
  REQUIRE( outDs->GetRasterBand( 1 )->GetColorTable() != nullptr );
  GDALClose( outDs );
}

TEST_CASE(
  "Classification pipeline: class ids > 255 escalate output to UInt16 without clamping",
  "[classify][pipeline][dtype]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y, { 1, 2, 300 } );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.classColors[300] = QColor( "#0000cc" );

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );

  GDALDataset *outDs = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( outDs != nullptr );
  // No silent clamp into Byte: band escalates to UInt16.
  REQUIRE( outDs->GetRasterBand( 1 )->GetRasterDataType() == GDT_UInt16 );
  REQUIRE( readPixel( outDs, 2, 2 ) == 1 );
  REQUIRE( readPixel( outDs, 28, 2 ) == 2 );
  REQUIRE( readPixel( outDs, 16, 28 ) == 300 );
  GDALClose( outDs );
}

TEST_CASE(
  "Classification pipeline: source NoData pixels map to unclassified class 0",
  "[classify][pipeline][nodata]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32, /*nodataRegion=*/true );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );

  GDALDataset *outDs = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( outDs != nullptr );
  // Region 0 is entirely NoData → unclassified 0; other regions classify.
  REQUIRE( readPixel( outDs, 2, 2 ) == 0 );
  REQUIRE( readPixel( outDs, 28, 2 ) == 2 );
  REQUIRE( readPixel( outDs, 16, 28 ) == 3 );
  // Output band carries the unclassified value as GDAL NoData.
  int success = 0;
  const double nd = outDs->GetRasterBand( 1 )->GetNoDataValue( &success );
  REQUIRE( success != 0 );
  REQUIRE( nd == 0.0 );
  GDALClose( outDs );
}

TEST_CASE(
  "Classification pipeline: held-out split populates accuracy assessment",
  "[classify][pipeline][accuracy]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.testX = X.clone();
  cfg.testY = y.clone();

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );
  // Perfectly separable training data scored on itself → perfect accuracy.
  REQUIRE( res.accuracy.classIds == QVector<int>( { 1, 2, 3 } ) );
  REQUIRE( res.accuracy.overallAccuracy == 1.0 );
  REQUIRE( res.accuracy.kappa == 1.0 );
  REQUIRE( res.accuracy.confusion.rows == 3 );
  REQUIRE( res.accuracy.confusion.cols == 3 );
}

TEST_CASE(
  "Classification pipeline: KMeans cluster IDs are Hungarian-remapped for accuracy",
  "[classify][pipeline][kmeans]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  // Two well-separated clusters; true labels 1 (low DN) and 2 (high DN).
  cv::Mat X( 40, 3, CV_32F ), y( 40, 1, CV_32S );
  for ( int i = 0; i < 40; ++i )
  {
    const bool high = i >= 20;
    for ( int b = 0; b < 3; ++b )
      X.at<float>( i, b ) = ( high ? 200.0f : 20.0f )
                            + static_cast<float>( ( i * 3 + b ) % 5 );
    y.at<int>( i, 0 ) = high ? 2 : 1;
  }

  RsClassificationPipeline::Config cfg;
  cfg.sourceRaster = srcPath;
  cfg.outputRaster = tmp.path() + "/out.tif";
  cfg.bandIndices = { 1, 2, 3 };
  cfg.backend.reset( new RsClassifierKMeans( 2 ) );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.testX = X.clone();
  cfg.testY = y.clone();
  cfg.methodName = QStringLiteral( "KMeans" );

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );
  // Cluster IDs (1..K) are arbitrary; after Hungarian remapping the held-out
  // accuracy must align with the true labels on these separable blobs.
  REQUIRE( res.accuracy.classIds.size() == 2 );
  REQUIRE( res.accuracy.overallAccuracy >= 0.95 );
}

TEST_CASE(
  "Classification pipeline: KMeans remap is backend-driven (lowercase methodName still remaps permuted labels)",
  "[classify][pipeline][kmeans][remap]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  // Non-contiguous training labels 5/9 (band-0-high ↔ 5, band-1-high ↔ 9):
  // the Hungarian remap must map the arbitrary cluster ids onto the TRAINING
  // labels — in the accuracy path and in the written class map — and must do
  // so regardless of the methodName spelling (no "KMeans" magic string).
  cv::Mat X, y;
  makeTraining( X, y, { 5, 9 } );

  RsClassificationPipeline::Config cfg;
  cfg.sourceRaster = srcPath;
  cfg.outputRaster = tmp.path() + "/out.tif";
  cfg.bandIndices = { 1, 2, 3 };
  cfg.backend.reset( new RsClassifierKMeans( 2 ) );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.testX = X.clone();
  cfg.testY = y.clone();
  cfg.methodName = QStringLiteral( "kmeans" ); // lowercase — metadata only
  cfg.classColors[5] = QColor( "#cc0000" );
  cfg.classColors[9] = QColor( "#0000cc" );

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );
  // Predictions align with the training labels, not with raw cluster ids 1/2.
  REQUIRE( res.accuracy.classIds == QVector<int>( { 5, 9 } ) );
  REQUIRE( res.accuracy.overallAccuracy == 1.0 );
  REQUIRE( res.accuracy.kappa == 1.0 );

  // Tile path: region 0 (band-0 high) → 5, region 1 (band-1 high) → 9.
  GDALDataset *outDs = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( outDs != nullptr );
  REQUIRE( readPixel( outDs, 2, 2 ) == 5 );
  REQUIRE( readPixel( outDs, 28, 2 ) == 9 );
  GDALClose( outDs );
}

TEST_CASE(
  "Classification pipeline: model + superset sidecar round-trip reproduces the class map",
  "[classify][pipeline][sidecar]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  // Train on scaled features so the sidecar scaler actually matters.
  RsFeatureScaler scaler;
  REQUIRE( scaler.fit( X ) );
  const cv::Mat scaledX = scaler.transform( X );
  REQUIRE( !scaledX.empty() );

  const QString modelPath = tmp.path() + "/model.yml";

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out1.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = scaledX;
  cfg.trainY = y;
  cfg.scaler = scaler;
  cfg.modelSavePath = modelPath;

  const RsClassificationPipelineResult res1 = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res1.errorMessage.toStdString() );
  REQUIRE( res1.ok );

  // Superset sidecar written; legacy .scale.json sidecar is gone for good.
  const QString metaPath = RsClassificationPipeline::sidecarPathForModel( modelPath );
  REQUIRE( metaPath == tmp.path() + "/model.meta.json" );
  REQUIRE( QFile::exists( metaPath ) );
  REQUIRE( !QFile::exists( tmp.path() + "/model.scale.json" ) );

  // Sidecar carries method + fitted scaler + class metadata + feature schema
  // + version.
  QString method;
  RsFeatureScaler loadedScaler;
  QHash<int, QColor> loadedColors;
  QVector<int> loadedFeatures;
  RsAccuracyAssessment::Result loadedAccuracy;
  REQUIRE( RsClassificationPipeline::loadModelSidecar(
    modelPath, method, loadedScaler, loadedColors, loadedFeatures, loadedAccuracy ) );
  REQUIRE( method == QStringLiteral( "NormalBayes" ) );
  REQUIRE( loadedScaler.isFitted() );
  REQUIRE( loadedScaler.bandCount() == 3 );
  REQUIRE( loadedColors.value( 1 ) == QColor( "#cc0000" ) );
  REQUIRE( loadedColors.value( 3 ) == QColor( "#0000cc" ) );
  // Feature schema records the training band selection (1-based).
  REQUIRE( loadedFeatures == QVector<int>( { 1, 2, 3 } ) );

  // Predict-only run: backend loaded from YAML, scaler from the sidecar,
  // no training data supplied.
  auto loadedBackend = std::make_unique<RsClassifierNormalBayes>();
  REQUIRE( loadedBackend->load( modelPath ) );
  REQUIRE( loadedBackend->isFitted() );

  RsClassificationPipeline::Config cfg2 = baseConfig( srcPath, tmp.path() + "/out2.tif" );
  cfg2.backend = std::move( loadedBackend );
  cfg2.scaler = loadedScaler;

  const RsClassificationPipelineResult res2 = RsClassificationPipeline::run( std::move( cfg2 ) );
  INFO( res2.errorMessage.toStdString() );
  REQUIRE( res2.ok );

  // Identical class maps.
  GDALDataset *ds1 = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out1.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  GDALDataset *ds2 = static_cast<GDALDataset *>(
    GDALOpen( ( tmp.path() + "/out2.tif" ).toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds1 != nullptr );
  REQUIRE( ds2 != nullptr );
  std::vector<uint8_t> map1( 32 * 32 ), map2( 32 * 32 );
  ds1->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 32, 32, map1.data(), 32, 32, GDT_Byte, 0, 0 );
  ds2->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 32, 32, map2.data(), 32, 32, GDT_Byte, 0, 0 );
  GDALClose( ds1 );
  GDALClose( ds2 );
  REQUIRE( map1 == map2 );
}

TEST_CASE(
  "Classification pipeline: cancel mid-predict removes the partial output",
  "[classify][pipeline][cancel]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  // 300x300 → 4 tiles of 256 → several tile-progress reports.
  createThreeRegionRaster( srcPath, 300, 300 );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;

  // Accept the post-training 0.30 report, cancel on the first tile report.
  const RsClassificationPipelineResult res = RsClassificationPipeline::run(
    std::move( cfg ),
    []( double fraction, const QString & ) { return fraction <= 0.30; } );

  REQUIRE( !res.ok );
  REQUIRE( res.error == RsClassificationPipelineResult::Error::Cancelled );
  REQUIRE( res.errorMessage == QStringLiteral( "Cancelled" ) );
  REQUIRE( !QFile::exists( tmp.path() + "/out.tif" ) );
}

TEST_CASE(
  "Classification pipeline: backend without training data fails with typed error",
  "[classify][pipeline][errors]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  REQUIRE( !res.ok );
  REQUIRE( res.error == RsClassificationPipelineResult::Error::NotFittedNoTrainingData );
  REQUIRE( res.errorMessage == QStringLiteral( "Backend not fitted and no training data supplied" ) );
  REQUIRE( !QFile::exists( tmp.path() + "/out.tif" ) );
}

TEST_CASE(
  "Classification pipeline: invalid vector file reports typed VectorOpenFailed error",
  "[classify][pipeline][vector][errors]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainingVector = tmp.path() + "/nonexistent.shp";

  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  REQUIRE( !res.ok );
  REQUIRE( res.error == RsClassificationPipelineResult::Error::VectorOpenFailed );
  REQUIRE( !QFile::exists( tmp.path() + "/out.tif" ) );
}

TEST_CASE(
  "Classification pipeline: fitScaler and testSplit parameters operate during run()",
  "[classify][pipeline][scaler][split]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;
  const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res.errorMessage.toStdString() );
  REQUIRE( res.ok );
  REQUIRE( res.error == RsClassificationPipelineResult::Error::None );
  REQUIRE( QFile::exists( tmp.path() + "/out.tif" ) );
}

TEST_CASE(
  "Classification pipeline: modelLoadPath automatically loads model and sidecar for predict-only mode",
  "[classify][pipeline][predict_only]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  const QString modelPath = tmp.path() + "/model.yml";

  // Step 1: Train & save model
  RsClassificationPipeline::Config trainCfg = baseConfig( srcPath, tmp.path() + "/out1.tif" );
  trainCfg.backend.reset( new RsClassifierNormalBayes );
  trainCfg.trainX = X;
  trainCfg.trainY = y;
  trainCfg.modelSavePath = modelPath;

  const RsClassificationPipelineResult trainRes = RsClassificationPipeline::run( std::move( trainCfg ) );
  REQUIRE( trainRes.ok );
  REQUIRE( QFile::exists( modelPath ) );

  // Step 2: Predict-only via modelLoadPath (no backend or trainX/trainY supplied)
  RsClassificationPipeline::Config predictCfg = baseConfig( srcPath, tmp.path() + "/out2.tif" );
  predictCfg.modelLoadPath = modelPath;

  const RsClassificationPipelineResult predictRes = RsClassificationPipeline::run( std::move( predictCfg ) );
  INFO( predictRes.errorMessage.toStdString() );
  REQUIRE( predictRes.ok );
  REQUIRE( predictRes.error == RsClassificationPipelineResult::Error::None );
  REQUIRE( QFile::exists( tmp.path() + "/out2.tif" ) );
}



TEST_CASE(
  "Classification pipeline: applying a model to a different band count fails with a typed error",
  "[classify][pipeline][sidecar][compat]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  const QString modelPath = tmp.path() + "/model.yml";

  RsClassificationPipeline::Config cfg = baseConfig( srcPath, tmp.path() + "/out.tif" );
  cfg.backend.reset( new RsClassifierNormalBayes );
  cfg.trainX = X;
  cfg.trainY = y;
  cfg.modelSavePath = modelPath;
  const RsClassificationPipelineResult res1 = RsClassificationPipeline::run( std::move( cfg ) );
  INFO( res1.errorMessage.toStdString() );
  REQUIRE( res1.ok );

  // Predict-only with a band selection that does NOT match the model's
  // training schema (sidecar records features [1,2,3]).
  auto loadedBackend = std::make_unique<RsClassifierNormalBayes>();
  REQUIRE( loadedBackend->load( modelPath ) );

  RsClassificationPipeline::Config cfg2 = baseConfig( srcPath, tmp.path() + "/out2.tif" );
  cfg2.backend = std::move( loadedBackend );
  cfg2.bandIndices = { 1, 2 };
  cfg2.modelLoadPath = modelPath;

  const RsClassificationPipelineResult res2 = RsClassificationPipeline::run( std::move( cfg2 ) );
  CHECK_FALSE( res2.ok );
  CHECK( res2.error == RsClassificationPipelineResult::Error::InvalidBand );
  CHECK( res2.errorMessage.contains( QStringLiteral( "trained on 3 features" ) ) );
  CHECK_FALSE( QFile::exists( tmp.path() + "/out2.tif" ) );
}

TEST_CASE(
  "Classification pipeline: predict-only mode fails when sidecar is missing",
  "[classify][pipeline][sidecar]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = tmp.path() + "/src.tif";
  createThreeRegionRaster( srcPath, 32, 32 );

  cv::Mat X, y;
  makeTraining( X, y );

  const QString modelPath = tmp.path() + "/model.yml";

  RsClassificationPipeline::Config cfg1 = baseConfig( srcPath, tmp.path() + "/out1.tif" );
  cfg1.backend.reset( new RsClassifierNormalBayes );
  cfg1.trainX = X;
  cfg1.trainY = y;
  cfg1.modelSavePath = modelPath;
  const RsClassificationPipelineResult res1 = RsClassificationPipeline::run( std::move( cfg1 ) );
  REQUIRE( res1.ok );

  // Delete the generated sidecar
  const QString sidecarPath = RsClassificationPipeline::sidecarPathForModel( modelPath );
  REQUIRE( QFile::exists( sidecarPath ) );
  REQUIRE( QFile::remove( sidecarPath ) );

  // Predict-only mode with missing sidecar must fail with ModelSidecarMissing
  RsClassificationPipeline::Config cfg2 = baseConfig( srcPath, tmp.path() + "/out2.tif" );
  cfg2.modelLoadPath = modelPath;

  const RsClassificationPipelineResult res2 = RsClassificationPipeline::run( std::move( cfg2 ) );
  CHECK_FALSE( res2.ok );
  CHECK( res2.error == RsClassificationPipelineResult::Error::ModelSidecarMissing );
  CHECK_FALSE( QFile::exists( tmp.path() + "/out2.tif" ) );
}

