// test_r30_t01_science_e2e.cpp — R30-T01 WP12: the three mandated end-to-end
// chains, executed against real offline GeoTIFF files and the public APIs:
//   A. ROI → sampling → train/save/load → predict → evaluate (cached AND
//      uncached ROIs, rotated geotransform, cancel + budget paths, and a
//      controlled large-bbox sparse-ROI proving peak RSS does not follow
//      the bbox when the burn window is budgeted).
//   B. labels → majority/recode (streaming operators) + clump/sieve
//      (kernels) → GDAL reopen verification (pixels, dtype, geotransform).
//   C. evaluation inputs → metrics → JSON → re-read/consume, including the
//      historical-record compatibility shapes.
#include "experiment/evaluation.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "rs_pixel_rasterizer.h"
#include "rs_post_process.h"
#include "rs_training_data_extraction.h"
#include "qgsgeometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <opencv2/ml.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::experiment;

namespace
{

qint64 currentVmHwmKb()
{
  std::ifstream status( "/proc/self/status" );
  std::string key;
  std::string unit;
  qint64 value = 0;
  while ( status >> key )
  {
    if ( key == "VmHWM:" )
    {
      status >> value >> unit;
      return value;
    }
    status.ignore( 4096, '\n' );
  }
  return -1;
}

/// Two-band float GeoTIFF with an affine (optionally rotated) geotransform.
/// Band1/band2 values come from the callbacks; the class map comes from
/// `classOf` (also written as a separate single-band Int32 truth raster).
bool writeFixtureRaster( const QString &path, const QString &truthPath, int W, int H,
                         const double gt[6], const std::function<int( int, int )> &classOf )
{
  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return false;
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  ds->SetGeoTransform( const_cast<double *>( gt ) );
  ds->SetProjection(
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
    "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  std::vector<float> band1( W ), band2( W );
  std::vector<int> truth( W );
  for ( int y = 0; y < H; ++y )
  {
    for ( int x = 0; x < W; ++x )
    {
      const int klass = classOf( x, y );
      // Perfectly separable per class: class c ↦ (10c, 10c + 1).
      band1[x] = float( 10 * klass );
      band2[x] = float( 10 * klass + 1 );
      truth[x] = klass;
    }
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, W, 1, band1.data(), W, 1,
                                      GDT_Float32, 0, 0 );
    ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, y, W, 1, band2.data(), W, 1,
                                      GDT_Float32, 0, 0 );
  }
  GDALClose( ds );

  GDALDataset *tds = drv->Create( truthPath.toUtf8().constData(), W, H, 1, GDT_Int32, nullptr );
  if ( !tds )
    return false;
  tds->SetGeoTransform( const_cast<double *>( gt ) );
  for ( int y = 0; y < H; ++y )
  {
    for ( int x = 0; x < W; ++x )
      truth[x] = classOf( x, y );
    tds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, W, 1, truth.data(), W, 1,
                                       GDT_Int32, 0, 0 );
  }
  GDALClose( tds );
  return true;
}

/// Map-space coordinate of a pixel-space point under an affine GDAL GT.
/// ROIs must live in the raster's map frame — under a rotated GT they are
/// rotated rectangles.
QString pixelCorner( const double gt[6], int c, int r )
{
  return QStringLiteral( "%1 %2" )
    .arg( gt[0] + gt[1] * c + gt[2] * r, 0, 'f', 6 )
    .arg( gt[3] + gt[4] * c + gt[5] * r, 0, 'f', 6 );
}

QString pixelRectWkt( const double gt[6], int col0, int row0, int col1, int row1 )
{
  return QStringLiteral( "POLYGON((%1,%2,%3,%4,%1))" )
    .arg( pixelCorner( gt, col0, row0 ), pixelCorner( gt, col1, row0 ),
          pixelCorner( gt, col1, row1 ), pixelCorner( gt, col0, row1 ) );
}

QVector<quint64> burnToIndices( const QgsGeometry &geom, const double gt[6], int W, int H )
{
  bool ok = false;
  const QSet<quint64> px = RsPixelRasterizer::rasterize( geom, gt, W, H, &ok );
  if ( !ok )
    return {};
  return QVector<quint64>( px.begin(), px.end() );
}

} // namespace

// --- E2E A: ROI → sampling → train → save/load → predict → evaluate --------------

TEST_CASE( "E2E-A: rotated-geotransform ROI chain trains, saves, reloads and scores perfectly",
           "[r30t01][e2e][science-chain]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const int W = 48, H = 48;
  // 20°-rotated affine geotransform with a shear term (GT is [c0..c5]).
  const double rad = 20.0 * M_PI / 180.0;
  const std::array<double, 6> gt = { 100.0, 1.0 * std::cos( rad ), 1.0 * std::sin( rad ) + 0.02,
                                     200.0, 1.0 * std::sin( rad ), -1.0 * std::cos( rad ) };
  const QString raster = tmp.path() + "/feat.tif";
  const QString truth = tmp.path() + "/truth.tif";
  const auto classOf = []( int x, int y ) {
    if ( x < 24 && y < 24 )
      return 1;
    if ( x >= 24 && y < 24 )
      return 2;
    if ( x < 24 )
      return 3;
    return 4;
  };
  REQUIRE( writeFixtureRaster( raster, truth, W, H, gt.data(), classOf ) );

  // ROIs: two overlapping rectangles straddling the class-1/3 boundary
  // (dedup: last geometry wins), one donut (hole), one edge-clipped box.
  RsTrainingGeometry roiA; // overlaps roiB in the 8..15 column band
  roiA.classId = 1;
  roiA.geometry = QgsGeometry::fromWkt( pixelRectWkt( gt.data(), 0, 0, 16, 20 ) );
  RsTrainingGeometry roiB; // same pixels claimed as class 1 again (dedup ok)
  roiB.classId = 1;
  roiB.geometry = QgsGeometry::fromWkt( pixelRectWkt( gt.data(), 8, 0, 16, 20 ) );
  RsTrainingGeometry roiDonut; // class 2 with a hole (map frame via GT)
  roiDonut.classId = 2;
  roiDonut.geometry = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((%1,%2,%3,%4,%1),(%5,%6,%7,%8,%5))" )
      .arg( pixelCorner( gt.data(), 26, 2 ), pixelCorner( gt.data(), 44, 2 ),
            pixelCorner( gt.data(), 44, 20 ), pixelCorner( gt.data(), 26, 20 ),
            pixelCorner( gt.data(), 31, 7 ), pixelCorner( gt.data(), 39, 7 ),
            pixelCorner( gt.data(), 39, 15 ), pixelCorner( gt.data(), 31, 15 ) ) );
  REQUIRE( !roiDonut.geometry.isNull() );
  RsTrainingGeometry roiC; // class 3, left-bottom quadrant
  roiC.classId = 3;
  roiC.geometry = QgsGeometry::fromWkt( pixelRectWkt( gt.data(), 2, 30, 20, 44 ) );
  RsTrainingGeometry roiEdge; // class 4, clipped at the raster edge
  roiEdge.classId = 4;
  roiEdge.geometry = QgsGeometry::fromWkt( pixelRectWkt( gt.data(), 20, 40, 60, 60 ) );
  REQUIRE( !roiEdge.geometry.isNull() );

  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = 100; // cap keeps the matrix small; seed fixed

  const QVector<RsTrainingGeometry> uncached{ roiA, roiB, roiDonut, roiC, roiEdge };
  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, uncached, options );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows > 0 );
  REQUIRE( res.X.cols == 2 );

  // Same chain through the CACHED pixel-index path: identical training
  // matrix and sample order (bit-for-bit).
  QVector<RsTrainingGeometry> cached;
  cached.reserve( uncached.size() );
  for ( const RsTrainingGeometry &roi : uncached )
  {
    RsTrainingGeometry withCache;
    withCache.classId = roi.classId;
    withCache.pixelIndices = burnToIndices( roi.geometry, gt.data(), W, H );
    REQUIRE( !withCache.pixelIndices.isEmpty() );
    cached.push_back( withCache );
  }
  const RsTrainingDataResult resCached = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, cached, options );
  REQUIRE( resCached.ok );
  REQUIRE( resCached.X.rows == res.X.rows );
  for ( int s = 0; s < res.X.rows; ++s )
  {
    CHECK( resCached.X.at<float>( s, 0 ) == res.X.at<float>( s, 0 ) );
    CHECK( resCached.X.at<float>( s, 1 ) == res.X.at<float>( s, 1 ) );
    CHECK( resCached.y.at<int>( s, 0 ) == res.y.at<int>( s, 0 ) );
  }

  // Train kNN on the extracted samples, save, reload, verify predictions.
  cv::Ptr<cv::ml::KNearest> knn = cv::ml::KNearest::create();
  knn->setDefaultK( 1 );
  knn->setIsClassifier( true );
  {
    cv::Ptr<cv::ml::TrainData> tdata =
      cv::ml::TrainData::create( res.X, cv::ml::ROW_SAMPLE, res.y );
    REQUIRE( knn->train( tdata ) );
  }
  const QString modelPath = tmp.path() + "/knn.yaml";
  knn->save( modelPath.toStdString() );
  cv::Ptr<cv::ml::KNearest> reloaded =
    cv::Algorithm::load<cv::ml::KNearest>( modelPath.toStdString() );
  REQUIRE( reloaded != nullptr );

  // Predict the full raster with the RELOADED model and evaluate against
  // the truth raster through the public ConfusionMatrix.
  cv::Mat predicted( 1, 2, CV_32F );
  // Five labels: c1..c4 carry the four classes, c5 is a schema class that
  // no pixel ever carries — its zero-support marker is part of the chain.
  ConfusionMatrix matrix(
    QStringList{ QStringLiteral( "c1" ), QStringLiteral( "c2" ),
                 QStringLiteral( "c3" ), QStringLiteral( "c4" ),
                 QStringLiteral( "c5" ) },
    5 );
  GDALDataset *truthDs = static_cast<GDALDataset *>(
    GDALOpen( truth.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( truthDs != nullptr );
  std::vector<int> truthRow( W );
  std::vector<float> b1( W ), b2( W );
  GDALDataset *featDs = static_cast<GDALDataset *>(
    GDALOpen( raster.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( featDs != nullptr );
  for ( int y = 0; y < H; ++y )
  {
    truthDs->GetRasterBand( 1 )->RasterIO( GF_Read, 0, y, W, 1, truthRow.data(), W, 1,
                                           GDT_Int32, 0, 0 );
    featDs->GetRasterBand( 1 )->RasterIO( GF_Read, 0, y, W, 1, b1.data(), W, 1,
                                          GDT_Float32, 0, 0 );
    featDs->GetRasterBand( 2 )->RasterIO( GF_Read, 0, y, W, 1, b2.data(), W, 1,
                                          GDT_Float32, 0, 0 );
    for ( int x = 0; x < W; ++x )
    {
      predicted.at<float>( 0, 0 ) = b1[x];
      predicted.at<float>( 0, 1 ) = b2[x];
      cv::Mat outLabels;
      reloaded->findNearest( predicted, 1, outLabels );
      matrix.increment( truthRow[x] - 1, int( outLabels.at<float>( 0, 0 ) ) - 1 );
    }
  }
  GDALClose( truthDs );
  GDALClose( featDs );

  // Separable one-value-per-class features + 1-NN memorization over all
  // four classes → every pixel predicted exactly right.
  const qint64 total = matrix.total();
  CHECK( total == qint64( W ) * H );
  CHECK( matrix.overallAccuracy() == Catch::Approx( 1.0 ) );
  const auto perClass = matrix.perClassAll();
  REQUIRE( perClass.size() == 5 );
  CHECK_FALSE( perClass[0].zeroSupport );
  CHECK_FALSE( perClass[1].zeroSupport );
  CHECK_FALSE( perClass[2].zeroSupport );
  CHECK_FALSE( perClass[3].zeroSupport );
  REQUIRE( perClass[4].zeroSupport ); // c5: no truth sample ever carried it

  // Evaluate → JSON → re-read → identical document (the consumer chain).
  const QJsonObject json = matrix.toJson();
  const auto reparsed = ConfusionMatrix::fromJson( json );
  REQUIRE( reparsed.has_value() );
  CHECK( *reparsed == matrix );
  CHECK( reparsed->toJson().value( QStringLiteral( "overall_accuracy" ) ).toDouble()
         == Catch::Approx( 1.0 ) );

  // Budget + cancel paths of the same chain.
  RsTrainingDataExtraction::Options tiny = options;
  tiny.maxCandidates = 5;
  const RsTrainingDataResult refused = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, uncached, tiny );
  CHECK( refused.error == RsTrainingDataResult::Error::BudgetExceeded );
  const RsTrainingDataResult cancelled = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, uncached, options,
    []( double, const QString & ) { return false; } );
  CHECK( cancelled.error == RsTrainingDataResult::Error::Cancelled );
  CHECK( cancelled.X.empty() );
  CHECK( cancelled.sampleGroupIds.empty() );
}

TEST_CASE( "E2E-A/mem: a budgeted burn window decouples peak RSS from the ROI bbox",
           "[r30t01][e2e][SLOW]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // A thin diagonal line geometry crossing a 12000×12000 raster: the
  // geometry's bbox covers the whole raster (legacy MEM burn = ~138 MB),
  // but only ~12k pixels actually burn.
  const int W = 12000, H = 12000;
  const double gt[6] = { 0, 1, 0, double( H ), 0, -1 };
  const QgsGeometry line = QgsGeometry::fromWkt(
    QStringLiteral( "POLYGON((0 11900, 100 12000, 12000 0, 11900 0, 0 11900))" ) );
  REQUIRE( !line.isNull() );

  // Chunked with a 1M-pixel window budget: the MEM band stays ≤ ~1 MB and
  // the result must equal the legacy single-window burn bit-for-bit.
  bool okChunked = false;
  qint64 hwmBefore = currentVmHwmKb();
  const QSet<quint64> chunked = RsPixelRasterizer::rasterize(
    line, gt, W, H, &okChunked, std::function<bool()> {}, nullptr,
    qint64 { 1000 } * 1000 );
  const qint64 hwmChunked = currentVmHwmKb() - hwmBefore;
  REQUIRE( okChunked );
  REQUIRE( chunked.size() > 10000 );

  bool okLegacy = false;
  hwmBefore = currentVmHwmKb();
  const QSet<quint64> legacy = RsPixelRasterizer::rasterize( line, gt, W, H, &okLegacy );
  const qint64 hwmLegacy = currentVmHwmKb() - hwmBefore;
  REQUIRE( okLegacy );
  CHECK( legacy == chunked );

  // Observable memory statement: the budgeted burn's peak stays far below
  // the legacy bbox-sized band (138 MB), while outputs stay identical.
  INFO( "VmHWM chunked burn: " << hwmChunked << " KB; legacy burn: " << hwmLegacy
                               << " KB; bbox band: " << ( qint64( W ) * H / 1024 ) << " KB" );
  CHECK( hwmChunked < ( qint64( W ) * H / 1024 ) / 4 );
}

// --- E2E B: labels → post-process operators → GDAL reopen -------------------------

TEST_CASE( "E2E-B: streaming operators match the in-memory kernels and reopen identically",
           "[r30t01][e2e][post-chain]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // 36×24 label map with: a cross-block diagonal object (8-conn merges),
  // NoData holes, per-class ties at borders and edge specks.
  const int W = 36, H = 24;
  cv::Mat labels( H, W, CV_32S, cv::Scalar( 1 ) );
  for ( int y = 0; y < H; ++y )
    for ( int x = 0; x < W; ++x )
    {
      if ( x / 12 == 1 && y / 8 == 1 )
        labels.at<int>( y, x ) = 0; // NoData block
      if ( x / 12 == 2 )
        labels.at<int>( y, x ) = 3;
    }
  for ( int i = 0; i < 20; ++i )
    labels.at<int>( i, i ) = 7; // diagonal object crossing 3 column blocks
  labels.at<int>( 0, W - 1 ) = 5; // edge speck
  labels.at<int>( H - 1, 0 ) = 5; // edge speck

  const double gt[6] = { 500.5, 0.5, 0.0, 800.25, 0.0, -0.5 };
  const QString srcPath = tmp.path() + "/labels.tif";
  QString err;
  REQUIRE( RsPostProcess::saveLabelRaster( srcPath, labels, gt,
                                           QStringLiteral( "EPSG:32650" ),
                                           QVector<QRgb>(), QStringList(),
                                           std::numeric_limits<double>::quiet_NaN(),
                                           &err ) );

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.hasOperator( "rs:majority_filter" ) );
  REQUIRE( registry.hasOperator( "rs:recode" ) );
  sicnu::operators::RSOperatorContext ctx;

  // majority(3) via the streaming operator vs the in-memory kernel: the
  // per-pixel outputs must be IDENTICAL (halo-block streaming contract).
  cv::Mat kernelMajority;
  REQUIRE( RsPostProcess::majorityFilter( labels, kernelMajority, 3, &err ) );
  {
    const QString majPath = tmp.path() + "/maj.tif";
    Json::Value params( Json::objectValue );
    params["input"] = srcPath.toStdString();
    params["output"] = majPath.toStdString();
    params["kernel"] = 3;
    auto op = registry.create( "rs:majority_filter" );
    REQUIRE( op != nullptr );
    const Json::Value result = op->run( params, ctx );
    REQUIRE( result.isMember( "output" ) );

    cv::Mat produced;
    double outGt[6];
    QString outWkt;
    double nodata = 0;
    REQUIRE( RsPostProcess::loadLabelRaster( majPath, produced, outGt, outWkt, &nodata,
                                             &err ) );
    REQUIRE( produced.rows == H );
    REQUIRE( produced.cols == W );
    // GDAL reopen verifies pixels, dtype-able range, geotransform, CRS.
    for ( int y = 0; y < H; ++y )
      for ( int x = 0; x < W; ++x )
        if ( produced.at<int>( y, x ) != kernelMajority.at<int>( y, x ) )
          FAIL( "streaming majority diverged from the kernel at "
                << y << "," << x );
    for ( int i = 0; i < 6; ++i )
      CHECK( outGt[i] == Catch::Approx( gt[i] ) );
    CHECK( outWkt.contains( "32650" ) );
  }

  // recode via the streaming operator: mapped labels change, unmapped stay.
  {
    const QString recodePath = tmp.path() + "/recode.tif";
    Json::Value params( Json::objectValue );
    params["input"] = srcPath.toStdString();
    params["output"] = recodePath.toStdString();
    Json::Value map( Json::objectValue );
    map["3"] = 30;
    map["7"] = 70;
    params["recode_map"] = map;
    auto op = registry.create( "rs:recode" );
    REQUIRE( op != nullptr );
    const Json::Value result = op->run( params, ctx );
    REQUIRE( result.isMember( "output" ) );

    cv::Mat produced;
    double outGt[6];
    QString outWkt;
    REQUIRE( RsPostProcess::loadLabelRaster( recodePath, produced, outGt, outWkt, &err ) );
    CHECK( produced.at<int>( 10, 13 ) == 0 ); // NoData block unmapped
    CHECK( produced.at<int>( 0, 24 ) == 30 ); // class 3 → 30
    CHECK( produced.at<int>( 5, 5 ) == 70 );  // diagonal 7 → 70
    CHECK( produced.at<int>( 0, 1 ) == 1 );   // unmapped label unchanged
    CHECK( produced.at<int>( 0, 0 ) == 70 );  // (0,0) is ON the 7-diagonal
  }

  // clump + sieve kernels on the same map: deterministic across window
  // shapes is not applicable (single pass), but cross-RUN determinism and
  // the 8-conn diagonal-merge semantics are observable user results.
  cv::Mat clumped1, clumped2;
  REQUIRE( RsPostProcess::clump( labels, clumped1, 8, &err ) );
  REQUIRE( RsPostProcess::clump( labels, clumped2, 8, &err ) );
  CHECK( cv::norm( clumped1, clumped2, cv::NORM_INF ) == 0.0 );
  // The diagonal 7s form ONE component under 8-conn, TWO under 4-conn.
  const int diagId = clumped1.at<int>( 5, 5 );
  CHECK( clumped1.at<int>( 10, 10 ) == diagId );
  cv::Mat clumped4;
  REQUIRE( RsPostProcess::clump( labels, clumped4, 4, &err ) );
  CHECK( clumped4.at<int>( 10, 10 ) != clumped4.at<int>( 5, 5 ) );

  cv::Mat sieved;
  REQUIRE( RsPostProcess::sieve( labels, sieved, 3, 8, &err ) );
  // The 5-specks (area 1 < 3) take their border majority: the top-right
  // speck sits in class-3 territory → 3; the bottom-left speck sits among
  // class 1 → 1 (sequential-majority user-visible result).
  CHECK( sieved.at<int>( 0, W - 1 ) == 3 );
  CHECK( sieved.at<int>( H - 1, 0 ) == 1 );
}

// --- E2E C: evaluation inputs → metrics → JSON → consume --------------------------

TEST_CASE( "E2E-C: evaluation document chain keeps historical records readable",
           "[r30t01][e2e][evaluation-chain]" )
{
  const QStringList labels{ QStringLiteral( "water" ), QStringLiteral( "forest" ) };

  SECTION( "zero total" )
  {
    ConfusionMatrix matrix( labels, 2 );
    const QJsonObject json = matrix.toJson();
    CHECK( json.value( QStringLiteral( "overall_accuracy" ) ).toDouble() == 0.0 );
    const auto reparsed = ConfusionMatrix::fromJson( json );
    REQUIRE( reparsed.has_value() );
    CHECK( reparsed->total() == 0 );
  }

  SECTION( "33 labels × 1025 big cells round-trips with the saturation marker" )
  {
    QStringList wide;
    for ( int i = 0; i < 33; ++i )
      wide << QStringLiteral( "c%1" ).arg( i );
    ConfusionMatrix matrix( wide, 33 );
    const qint64 big = 9007199254740991; // 2^53 - 1
    // DISTINCT cells: 33 diagonal + 992 off-diagonal (row-major).
    int placed = 0;
    for ( int i = 0; i < 33; ++i, ++placed )
      matrix.setCount( i, i, big );
    for ( int r = 0; r < 33 && placed < 1025; ++r )
      for ( int c = 0; c < 33 && placed < 1025; ++c )
        if ( r != c )
        {
          matrix.setCount( r, c, big );
          ++placed;
        }
    const QJsonObject json = matrix.toJson();
    CHECK( json.value( QStringLiteral( "totals_saturated" ) ).toBool() == true );
    const auto reparsed = ConfusionMatrix::fromJson( json );
    REQUIRE( reparsed.has_value() );
    CHECK( *reparsed == matrix );
    CHECK( reparsed->totalsSaturated() );
    // The document the store persists re-reads to the same score.
    CHECK( reparsed->overallAccuracy()
           == Catch::Approx( matrix.overallAccuracy() ) );
  }

  SECTION( "historical record without the new markers still reads" )
  {
    // A pre-R29-style document: dense counts, no markers at all.
    QJsonObject legacy;
    legacy.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
    QJsonArray rows;
    rows.append( QJsonArray{ 3.0, 1.0 } );
    rows.append( QJsonArray{ 2.0, 4.0 } );
    legacy.insert( QStringLiteral( "counts" ), rows );
    const auto parsed = ConfusionMatrix::fromJson( legacy );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->total() == 10 );
    CHECK_FALSE( parsed->totalsSaturated() );
  }

  SECTION( "regression metrics JSON consumes as before, plus the new marker only when needed" )
  {
    const auto result = regressionMetrics( { 1.0, 2.0, 3.0 }, { 1.5, 2.5, 3.5 } );
    REQUIRE( result.has_value() );
    const QJsonObject json = result->toJson();
    CHECK_FALSE( json.contains( QStringLiteral( "representable" ) ) );
    CHECK( json.value( QStringLiteral( "mae" ) ).toDouble() == Catch::Approx( 0.5 ) );
    // SSE = 3·0.25, SST = 2 → R² = 1 − 0.75/2 = 0.625 (exact oracle).
    CHECK( json.value( QStringLiteral( "r2" ) ).toDouble()
           == Catch::Approx( 0.625 ) );

    const auto extreme = regressionMetrics( { 1e308 }, { -1e308 } );
    REQUIRE( extreme.has_value() );
    const QJsonObject extremeJson = extreme->toJson();
    CHECK( extremeJson.value( QStringLiteral( "representable" ) ).toBool() == false );
    CHECK( extremeJson.value( QStringLiteral( "mae" ) ).isNull() );
  }
}
