// R26-science oracles — RED→GREEN regression suite for the correctness
// defects fixed by this track (.planning/zcode-r26-science/ORACLES.md).
//
// Each TEST_CASE maps to one work package (S1..S12) and pins the *scientific*
// contract: input/expected-output pairs derived from an independent reference
// (manual arithmetic, sibling-kernel semantics or documented statistics),
// not from the production implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <gdal_priv.h>
#include <ogr_api.h>
#include <qgsgeometry.h>
#include <qgspolygon.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "rs_classifier_backend.h"
#include "rs_class_raster.h"
#include "rs_cross_validation.h"
#include "rs_feature_scaler.h"
#include "rs_majority_vote.h"
#include "rs_probability_calibration.h"
#include "rs_spatial_cross_validation.h"
#include "rs_training_data_extraction.h"

#include "processing/algorithms/change_detector.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/portable_random.h"
#include "processing/algorithms/trend_analysis.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_change_detection_operator.h"

namespace
{

Catch::Approx approx( double v )
{
  return Catch::Approx( v ).epsilon( 1e-6 );
}

// ---------------------------------------------------------------------------
// Recording mock backend: predicts round(x0) so tests can recover exactly
// which sample rows each CV fold used (row identity is encoded in feature 0).
//
class RecordingBackend final : public RsClassifierBackend
{
  public:
    explicit RecordingBackend( std::shared_ptr<std::vector<std::vector<int>>> sink )
      : mSink( std::move( sink ) )
    {
    }

    bool fit( const cv::Mat &, const cv::Mat & ) override { return true; }

    cv::Mat predict( const cv::Mat &X ) const override
    {
      std::vector<int> rows;
      rows.reserve( X.rows );
      cv::Mat out( X.rows, 1, CV_32S );
      for ( int i = 0; i < X.rows; ++i )
      {
        const int row = static_cast<int>( std::lround( X.at<float>( i, 0 ) ) );
        out.at<int>( i, 0 ) = 1;
        rows.push_back( row );
      }
      mSink->push_back( std::move( rows ) );
      return out;
    }

    QString name() const override { return QStringLiteral( "recording-mock" ); }

  private:
    std::shared_ptr<std::vector<std::vector<int>>> mSink;
};

// Cluster backend that fails its first predict() (the CV remap probe) and
// then emits cluster ids numerically equal to the true class ids (encoded in
// feature 1). With no remap table the CV must NOT credit those raw cluster
// ids as correct class predictions.
class RemapProbeBackend final : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &, const cv::Mat & ) override { return true; }

    cv::Mat predict( const cv::Mat &X ) const override
    {
      if ( !mProbed )
      {
        mProbed = true;
        throw std::runtime_error( "remap probe predict failed" );
      }
      cv::Mat out( X.rows, 1, CV_32S );
      for ( int i = 0; i < X.rows; ++i )
        out.at<int>( i, 0 ) = static_cast<int>( std::lround( X.at<float>( i, 1 ) ) );
      return out;
    }

    bool needsLabelRemap() const override { return true; }
    QString name() const override { return QStringLiteral( "remap-probe-mock" ); }

  private:
    mutable bool mProbed = false;
};

// X-dependent backend for the spatial-CV dtype oracle: predicts class 2 for
// feature0 < 4 and class 1 above — accuracy is only correct when the fold
// matrices genuinely carry the (converted) feature values.
class ConstantBackend final : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &, const cv::Mat & ) override { return true; }
    cv::Mat predict( const cv::Mat &X ) const override
    {
      cv::Mat out( X.rows, 1, CV_32S );
      for ( int i = 0; i < X.rows; ++i )
        out.at<int>( i, 0 ) = X.at<float>( i, 0 ) < 4.0f ? 2 : 1;
      return out;
    }
    QString name() const override { return QStringLiteral( "constant-mock" ); }
};

// Float32 GeoTIFF writer shared by the raster fixtures.
void createRaster( const QString &path, int W, int H, int nBands,
                   const std::function<float( int, int, int )> &valueFn,
                   bool setNodata = false, double nodata = -9999.0 )
{
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, nBands, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < nBands; ++b )
  {
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        band[static_cast<size_t>( r * W + c )] = valueFn( b, r, c );
    GDALRasterBand *rb = ds->GetRasterBand( b + 1 );
    rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    if ( setNodata )
      rb->SetNoDataValue( nodata );
  }
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

} // namespace

// ===========================================================================
// S1 — percentClipStretch: pct >= 100 (and negative pct) must degrade to the
// full-range stretch, not read out of bounds. Reference: a clip of 100% per
// side removes every sample, so the only defined behaviour is min..max.
// ===========================================================================
TEST_CASE( "R26-S1 percentClipStretch degenerate clip percent", "[r26][enhancement]" )
{
  std::vector<float> in( 100 ), out( 100 );
  for ( int i = 0; i < 100; ++i )
    in[static_cast<size_t>( i )] = static_cast<float>( i + 1 ); // 1..100

  // Full-range reference: (v-1)/99*255 clamped to [0,255].
  const auto expected = []( float v ) {
    return std::clamp( ( v - 1.0f ) / 99.0f * 255.0f, 0.0f, 255.0f );
  };

  SECTION( "pct = 100 collapses to full range" )
  {
    ImageEnhancement::percentClipStretch( in.data(), out.data(), in.size(), 100.0f, -9999.0f );
    CHECK( out[0] == 0.0f );
    CHECK( out[49] == approx( expected( 50.0f ) ) );
    CHECK( out[99] == 255.0f );
  }
  SECTION( "pct far beyond 100 still full range" )
  {
    ImageEnhancement::percentClipStretch( in.data(), out.data(), in.size(), 1000.0f, -9999.0f );
    CHECK( out[49] == approx( expected( 50.0f ) ) );
    CHECK( out[99] == 255.0f );
  }
  SECTION( "negative pct clips nothing" )
  {
    ImageEnhancement::percentClipStretch( in.data(), out.data(), in.size(), -5.0f, -9999.0f );
    CHECK( out[0] == 0.0f );
    CHECK( out[49] == approx( expected( 50.0f ) ) );
    CHECK( out[99] == 255.0f );
  }
  SECTION( "pct = 2 keeps the documented behaviour (regression guard)" )
  {
    ImageEnhancement::percentClipStretch( in.data(), out.data(), in.size(), 2.0f, -9999.0f );
    // clipCount = 2 → stretch over [3, 98].
    const float lo = 3.0f, hi = 98.0f;
    CHECK( out[49] == approx( std::clamp( ( 50.0f - lo ) / ( hi - lo ) * 255.0f, 0.0f, 255.0f ) ) );
    CHECK( out[0] == 0.0f );
    CHECK( out[99] == 255.0f );
  }
}

// ===========================================================================
// S2 — training extraction: maxSamplesPerClass caps *kept* (valid) samples,
// per the header contract. NoData pixels must not consume the cap.
// ===========================================================================
TEST_CASE( "R26-S2 training cap applies after NoData filtering",
           "[r26][classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // Odd rows are NoData. The class-2 ROI (rows 12..15, cols 0..3) overrides
  // the class-1 ROI, so class 1 keeps 16x16 - 16 = 240 candidate pixels of
  // which 120 are valid; class 2 has 16 candidates of which 8 are valid.
  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 16, 16, 1,
                []( int, int r, int c ) {
                  return ( r % 2 == 1 ) ? -9999.0f : static_cast<float>( r * 16 + c );
                },
                true, -9999.0 );

  // Reuse the module's geometry fixture shape (GT {0,1,0,16,0,-1}).
  const auto square = []( double x0, double y0, double x1, double y1 ) {
    RsTrainingGeometry tg;
    QgsPolygonXY poly;
    poly.append( { QgsPointXY( x0, y0 ), QgsPointXY( x1, y0 ), QgsPointXY( x1, y1 ),
                   QgsPointXY( x0, y1 ), QgsPointXY( x0, y0 ) } );
    tg.classId = 0;
    tg.geometry = QgsGeometry::fromPolygonXY( poly );
    return tg;
  };
  RsTrainingGeometry g1 = square( 0, 16, 16, 0 );
  g1.classId = 1;
  RsTrainingGeometry g2 = square( 0, 4, 4, 0 );
  g2.classId = 2;

  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = 10;

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, { g1, g2 }, options );
  REQUIRE( res.ok );
  // Class 1: 128 valid candidates ≥ cap → exactly 10 kept (pre-fix: ~5,
  // because the cap consumed NoData pixels).
  CHECK( res.classCounts.value( 1 ) == 10 );
  // Class 2: only 8 valid candidates exist → all 8 kept (pre-fix: ~5).
  CHECK( res.classCounts.value( 2 ) == 8 );
  // Nothing kept sits on NoData.
  for ( int s = 0; s < res.X.rows; ++s )
    CHECK( res.X.at<float>( s, 0 ) != -9999.0f );

  // Determinism: identical selection on a second run.
  const RsTrainingDataResult res2 = RsTrainingDataExtraction::extract(
    raster, { 1 }, { g1, g2 }, options );
  REQUIRE( res2.ok );
  REQUIRE( res2.X.rows == res.X.rows );
  for ( int s = 0; s < res.X.rows; ++s )
  {
    CHECK( res2.X.at<float>( s, 0 ) == res.X.at<float>( s, 0 ) );
    CHECK( res2.y.at<int>( s, 0 ) == res.y.at<int>( s, 0 ) );
  }
}

// ===========================================================================
// S3 — isotonic calibration: duplicate scores with mixed labels must yield
// knot vectors satisfying the sidecar contract (x strictly ascending) so the
// model can be saved *and* reloaded.
// ===========================================================================
TEST_CASE( "R26-S3 isotonic duplicate scores round-trip", "[r26][classification][calibration]" )
{
  const QVector<int> classIds = { 1, 2 };
  // Column 1 scores {1,1,2} with targets {0,1,1}: the two mixed-label 1.0
  // scores must pool into one knot (rate 0.5), giving knots x=[1,2].
  std::vector<float> rawScores = { 1.0f, 9.0f, 1.0f, 9.0f, 2.0f, 9.0f };
  std::vector<int> labels = { 2, 1, 1 };

  RsCalibrationModel model;
  const bool fitOk = RsProbabilityCalibrator::fitIsotonic(
    rawScores, 3, classIds, labels, model );
  REQUIRE( fitOk );
  REQUIRE( model.isValid() );

  REQUIRE( model.isotonicX.size() == 2 );
  REQUIRE( model.isotonicX[0].size() >= 2 );
  for ( int c = 0; c < 2; ++c )
    for ( int j = 1; j < model.isotonicX[c].size(); ++j )
      CHECK( model.isotonicX[c][j] > model.isotonicX[c][j - 1] );

  // Pooled duplicate knot: column 0 at score 1 calibrates to 0.5, column 1
  // (constant scores) to 1/3; the row normalises to [0.6, 0.4].
  std::vector<float> raw = { 1.0f, 9.0f };
  std::vector<float> probs;
  REQUIRE( RsProbabilityCalibrator::apply( model, raw, 1, probs ) );
  REQUIRE( probs.size() == 2 );
  CHECK( probs[0] == Catch::Approx( 0.6 ).epsilon( 1e-6 ) );
  CHECK( probs[1] == Catch::Approx( 0.4 ).epsilon( 1e-6 ) );

  // Save/load round-trip must succeed (pre-fix: fit produced duplicate knot
  // x, fromJson rejects the sidecar, a saved project becomes unloadable).
  const QJsonObject json = model.toJson();
  RsCalibrationModel reloaded;
  CHECK( reloaded.fromJson( json ) );
  CHECK( reloaded.isValid() );
}

// ===========================================================================
// S4 — Mann-Kendall with tied timestamps: tau must be tau-b (scipy-compatible
// reference values) and var(S) must subtract the time-tie term. No-tie inputs
// keep the classic values (regression guard).
// ===========================================================================
TEST_CASE( "R26-S4 Mann-Kendall tied timestamps", "[r26][temporal][trend]" )
{
  // tau stays tau-a per the header contract (pinned by the Gilbert
  // benchmark); the defect under test is var(S): with duplicate timestamps
  // (temporal operators' keep_all policy) the z/p statistics were computed
  // from an inflated variance that ignored the timestamp tie term.
  // With two time-tied pairs the usable time-ordered pairs number 2 of 3,
  // so |tau-a| can only reach 2/3 on a strictly increasing series.
  SECTION( "duplicate dates: var(S) subtracts the time-tie term" )
  {
    const std::vector<float> y = { 1, 2, 3 };
    const std::vector<double> t = { 0, 0, 1 };
    const auto r = sicnu::temporal::TrendAnalyzer::computeMannKendall( y, t );
    CHECK( r.valid );
    CHECK( r.sampleCount == 3 );
    CHECK( r.tau == approx( 2.0 / 3.0 ) ); // tau-a: S=2 over all n0=3 pairs
    // varS = (n(n-1)(2n+5) - time-tie term) / 18 = (66 - 18)/18.
    CHECK( r.tauVariance == approx( 48.0 / 18.0 ) );
    CHECK( r.senSlope == approx( 1.5 ) );
  }
  SECTION( "value ties keep the Gilbert term (regression guard)" )
  {
    const std::vector<float> y = { 1, 1, 2 };
    const std::vector<double> t = { 0, 1, 2 };
    const auto r = sicnu::temporal::TrendAnalyzer::computeMannKendall( y, t );
    CHECK( r.valid );
    CHECK( r.tau == approx( 2.0 / 3.0 ) );
    // Only the value-tie term applies (2*1*9 = 18).
    CHECK( r.tauVariance == approx( ( 66.0 - 18.0 ) / 18.0 ) );
  }
  SECTION( "no ties keep the classic values (regression guard)" )
  {
    const std::vector<float> y = { 1, 2, 3, 4 };
    const std::vector<double> t = { 0, 1, 2, 3 };
    const auto r = sicnu::temporal::TrendAnalyzer::computeMannKendall( y, t );
    CHECK( r.valid );
    CHECK( r.tau == 1.0 );
    CHECK( r.tauVariance == approx( 4.0 * 3.0 * 13.0 / 18.0 ) ); // 156/18
  }
  SECTION( "one duplicated date in four observations" )
  {
    const std::vector<float> y = { 1, 2, 3, 4 };
    const std::vector<double> t = { 0, 0, 1, 2 };
    const auto r = sicnu::temporal::TrendAnalyzer::computeMannKendall( y, t );
    CHECK( r.valid );
    CHECK( r.tau == approx( 5.0 / 6.0 ) ); // tau-a over all 6 pairs
    CHECK( r.tauVariance == approx( ( 156.0 - 18.0 ) / 18.0 ) );
  }
  SECTION( "doubly-tied input never publishes a negative variance" )
  {
    // Both axes tied (constant y on two dates): the raw algebra gives
    // (156 - 156 - 36)/18 = -2 < 0 — the published var(S) must clamp to 0
    // and the degenerate z=0/p=1 branch must be taken (R1 P2-2).
    // (base 4·3·13=156; value ties 4·3·13=156; time ties 2·1·9+2·1·9=36)
    const std::vector<float> y = { 7, 7, 7, 7 };
    const std::vector<double> t = { 0, 0, 1, 1 };
    const auto r = sicnu::temporal::TrendAnalyzer::computeMannKendall( y, t );
    CHECK( r.valid );
    CHECK( r.tauVariance >= 0.0 );
    CHECK( r.zScore == 0.0 );
    CHECK( r.pValue == 1.0 );
  }
}

// ===========================================================================
// S5 — kFold reproducibility: the seed contract must be order-stable — folds
// depend only on (samples, classes, seed), with classes consumed in ascending
// id order (reference simulation), invariant under order-preserving relabel.
// ===========================================================================
TEST_CASE( "R26-S5 kFold seed contract is class-order independent",
           "[r26][classification][cv]" )
{
  constexpr int k = 3;
  constexpr unsigned seed = 7u;
  constexpr int perClass = 4;

  const auto runCv = [&]( const QVector<int> &classIdsOrdered ) {
    cv::Mat X( perClass * 3, 2, CV_32F );
    cv::Mat y( perClass * 3, 1, CV_32S );
    int row = 0;
    for ( int ci = 0; ci < 3; ++ci )
      for ( int i = 0; i < perClass; ++i, ++row )
      {
        X.at<float>( row, 0 ) = static_cast<float>( row ); // row identity
        X.at<float>( row, 1 ) = 0.0f;
        y.at<int>( row, 0 ) = classIdsOrdered[ci];
      }
    auto sink = std::make_shared<std::vector<std::vector<int>>>();
    auto factory = [sink]() -> std::unique_ptr<RsClassifierBackend> {
      return std::make_unique<RecordingBackend>( sink );
    };
    const auto r = RsCrossValidation::kFold( X, y, factory, k,
                                             /*scaleFeatures=*/false,
                                             /*isCanceled=*/nullptr, seed );
    REQUIRE( r.ok() );
    REQUIRE( sink->size() == static_cast<size_t>( k ) );
    return *sink;
  };

  // Reference simulation of the documented algorithm: buckets in first-appearance
  // order, classes shuffled in ascending class-id order with one shared
  // mt19937(seed), portable Fisher-Yates, round-robin fold assignment.
  const auto referenceFolds = [&]( const std::vector<int> &firstAppearance ) {
    std::map<int, std::vector<int>> byClass;
    for ( int i = 0; i < perClass * 3; ++i )
      byClass[firstAppearance[static_cast<size_t>( i )]].push_back( i );
    std::mt19937 rng( seed );
    std::array<std::vector<int>, k> foldTest{};
    for ( auto &kv : byClass ) // std::map → ascending class id
    {
      auto &bucket = kv.second;
      rs::algorithms::portable::fisherYatesShuffle( rng, bucket );
      for ( int i = 0; i < static_cast<int>( bucket.size() ); ++i )
        foldTest[static_cast<size_t>( i % k )].push_back( bucket[static_cast<size_t>( i )] );
    }
    return foldTest;
  };

  // Class ids deliberately hash out of ascending bucket order in QHash
  // (rows 0..3 → 27, 4..7 → 16, 8..11 → 5).
  std::vector<int> ySequence;
  for ( const int cls : { 27, 16, 5 } )
    for ( int i = 0; i < perClass; ++i )
      ySequence.push_back( cls );
  const auto folds1 = runCv( { 27, 16, 5 } );
  const auto ref = referenceFolds( ySequence );
  for ( int f = 0; f < k; ++f )
  {
    CAPTURE( f );
    REQUIRE( folds1[static_cast<size_t>( f )].size() == ref[static_cast<size_t>( f )].size() );
    for ( size_t i = 0; i < folds1[static_cast<size_t>( f )].size(); ++i )
      CHECK( folds1[static_cast<size_t>( f )][i] == ref[static_cast<size_t>( f )][i] );
  }

  // Order-preserving relabel (27,16,5) → (127,116,105): same seed must give
  // the identical sample→fold assignment (metric semantics unchanged).
  const auto folds2 = runCv( { 127, 116, 105 } );
  for ( int f = 0; f < k; ++f )
  {
    REQUIRE( folds2[static_cast<size_t>( f )].size() == folds1[static_cast<size_t>( f )].size() );
    for ( size_t i = 0; i < folds1[static_cast<size_t>( f )].size(); ++i )
      CHECK( folds2[static_cast<size_t>( f )][i] == folds1[static_cast<size_t>( f )][i] );
  }
}

// ===========================================================================
// S6 — CV label remap discipline: when the Hungarian remap table is not
// available, raw cluster ids must never be credited as class predictions
// (they live in a different id space and can coincide by accident).
// ===========================================================================
TEST_CASE( "R26-S6 CV unmapped cluster ids are not correct predictions",
           "[r26][classification][cv]" )
{
  constexpr int perClass = 4;
  cv::Mat X( perClass * 2, 2, CV_32F );
  cv::Mat y( perClass * 2, 1, CV_32S );
  int row = 0;
  for ( const int cls : { 1, 2 } )
    for ( int i = 0; i < perClass; ++i, ++row )
    {
      X.at<float>( row, 0 ) = static_cast<float>( row );
      X.at<float>( row, 1 ) = static_cast<float>( cls ); // cluster = class id
      y.at<int>( row, 0 ) = cls;
    }

  auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
    return std::make_unique<RemapProbeBackend>();
  };
  const auto r = RsCrossValidation::kFold( X, y, factory, 2,
                                           /*scaleFeatures=*/false );
  REQUIRE( r.ok() );
  REQUIRE( r.foldAccuracies.size() == 2 );
  // With the remap probe failing, every prediction is unclassified (0):
  // accuracy must be 0.0 — pre-fix the raw cluster id (numerically equal to
  // the class id by coincidence) scored a fake 1.0.
  for ( const double acc : r.foldAccuracies )
    CHECK( acc == 0.0 );
  CHECK( r.meanAccuracy == 0.0 );
}

// ===========================================================================
// S7 — majority tie-break kernel: "ties → smaller id" must hold when id 0
// participates in the tie (0 is a vote key, not an "unset" sentinel).
// ===========================================================================
TEST_CASE( "R26-S7 majority tie-break with key zero", "[r26][segmentation][majority]" )
{
  QHash<int, int> votes;
  votes.insert( 0, 3 );
  votes.insert( 5, 3 );
  CHECK( majorityKeyWithTieBreak( votes ) == 0 );

  QHash<int, int> threeWay;
  threeWay.insert( 0, 2 );
  threeWay.insert( 5, 2 );
  threeWay.insert( 9, 2 );
  CHECK( majorityKeyWithTieBreak( threeWay ) == 0 );

  QHash<int, int> clearWin;
  clearWin.insert( 0, 2 );
  clearWin.insert( 7, 5 );
  CHECK( majorityKeyWithTieBreak( clearWin ) == 7 );

  QHash<int, int> single;
  single.insert( 0, 3 );
  CHECK( majorityKeyWithTieBreak( single ) == 0 );

  QHash<int, int> empty;
  CHECK( majorityKeyWithTieBreak( empty ) == 0 );
}

// ===========================================================================
// S8 — polygonize connectivity: the segmenter defines objects as 8-connected
// regions; the vector export must not split a diagonal-connected object.
// ===========================================================================
TEST_CASE( "R26-S8 polygonize keeps 8-connected objects whole",
           "[r26][segmentation][polygonize]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const auto writeLabelRaster = [&]( const QString &path,
                                     const std::vector<GByte> &labels ) {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), 2, 2, 1, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 2, 2,
                           const_cast<GByte *>( labels.data() ), 2, 2, GDT_Byte, 0, 0 )
             == CE_None );
    double gt[6] = { 0, 1, 0, 2, 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
  };

  const auto countFeatures = [&]( const QString &path ) {
    GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_VECTOR,
                                 nullptr, nullptr, nullptr );
    REQUIRE( ds != nullptr );
    OGRLayerH lyr = GDALDatasetGetLayer( ds, 0 );
    REQUIRE( lyr != nullptr );
    const int n = static_cast<int>( OGR_L_GetFeatureCount( lyr, true ) );
    GDALClose( ds );
    return n;
  };

  SECTION( "diagonal-only contact is one object" )
  {
    const QString raster = tmp.path() + "/diag.tif";
    writeLabelRaster( raster, { 1, 0, 0, 1 } ); // (0,0) and (1,1)
    const RsClassRasterResult res =
      RsClassRaster::polygonize( raster, tmp.path() + "/diag.shp" );
    REQUIRE( res.ok );
    CHECK( countFeatures( tmp.path() + "/diag.shp" ) == 1 );
    // The single feature is one polygon whose ring touches itself at the
    // shared corner — that self-touch is the TRUE topology of an object
    // whose pixels connect only diagonally, and OGR_G_IsValid() (strict OGC
    // rules, GEOS-backed) reports it invalid. This is pinned deliberately:
    // object identity (one object = one feature) outranks OGC strictness,
    // and a consumer that needs strictly valid geometry must post-process
    // (GEOS makeValid) rather than split the object. Splitting it — the old
    // 4-connected behaviour — silently changed the object count, which is
    // the defect S8 fixes.
    {
      GDALDatasetH ds = GDALOpenEx( ( tmp.path() + "/diag.shp" ).toUtf8().constData(),
                                    GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr );
      REQUIRE( ds != nullptr );
      OGRLayerH lyr = GDALDatasetGetLayer( ds, 0 );
      REQUIRE( lyr != nullptr );
      OGRFeatureH feat = OGR_L_GetNextFeature( lyr );
      REQUIRE( feat != nullptr );
      OGRGeometryH geom = OGR_F_GetGeometryRef( feat );
      REQUIRE( geom != nullptr );
      const OGRwkbGeometryType gt = OGR_G_GetGeometryType( geom );
      CHECK( ( gt == wkbPolygon || gt == wkbPolygon25D ) );
      CHECK( !OGR_G_IsValid( geom ) ); // single-corner self-touch: documented
      CHECK( OGR_G_GetGeometryCount( geom ) == 1 ); // one ring, not two parts
      OGR_F_Destroy( feat );
      GDALClose( ds );
    }
  }
  SECTION( "4-connected pair stays one object (regression guard)" )
  {
    const QString raster = tmp.path() + "/conn4.tif";
    writeLabelRaster( raster, { 1, 1, 0, 0 } );
    const RsClassRasterResult res =
      RsClassRaster::polygonize( raster, tmp.path() + "/conn4.shp" );
    REQUIRE( res.ok );
    CHECK( countFeatures( tmp.path() + "/conn4.shp" ) == 1 );
  }
}

// ===========================================================================
// S9 — ChangeDetector::computeLogRatio must follow the library's log-ratio
// semantics (change_detection.cpp): non-finite input → NaN, negative input
// clamped to 0 before the epsilon shift — never a silent "no change" 0.
// ===========================================================================
TEST_CASE( "R26-S9 computeLogRatio non-finite handling", "[r26][change]" )
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float eps = 1e-4f;
  const std::vector<float> t1 = { nan, 1.0f, -5.0f, 0.0f };
  const std::vector<float> t2 = { 2.0f, 2.0f, 2.0f, 2.0f };
  const auto out = rs::processing::ChangeDetector::computeLogRatio( t1, t2, eps );

  REQUIRE( out.size() == 4 );
  CHECK( std::isnan( out[0] ) );                     // NaN in → NaN out
  CHECK( out[1] == Catch::Approx( std::log( ( 2.0 + eps ) / ( 1.0 + eps ) ) ).epsilon( 1e-5 ) );
  CHECK( out[2] == Catch::Approx( std::log( ( 2.0 + eps ) / eps ) ).epsilon( 1e-5 ) ); // clamp(-5)=0
  CHECK( out[3] == Catch::Approx( std::log( ( 2.0 + eps ) / eps ) ).epsilon( 1e-5 ) );
}

// ===========================================================================
// S10 — changedArea for a projected CRS with non-metre linear units must
// still be reported in m² (US survey foot: 0.30480060960121924 m/ft).
// ===========================================================================
TEST_CASE( "R26-S10 changedArea converts non-metre projected CRS to m2",
           "[r26][change][operators]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );

  constexpr int W = 4, H = 4;
  const char *ftProj = "+proj=lcc +lat_0=40.1666666666667 +lon_0=-74 "
                       "+lat_1=41.0333333333333 +lat_2=40.6666666666667 "
                       "+x_0=300000 +y_0=0 +datum=NAD83 +units=us-ft +no_defs";
  const std::array<double, 6> gt = { 1000000.0, 2.0, 0.0, 200000.0, 0.0, -2.0 };

  const auto makeRaster = [&]( const QString &path, float value, float hot ) {
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
    REQUIRE( GDALSetProjection( ds, ftProj ) == CE_None );
    std::vector<float> data( W * H, value );
    data[0] = hot;
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           data.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  };
  const QString before = tmp.path() + "/ft_before.tif";
  const QString after = tmp.path() + "/ft_after.tif";
  makeRaster( before, 10.0f, 10.0f );
  makeRaster( after, 10.0f, 50.0f );

  sicnu::operators::rs::RsChangeDetectionOperator op;
  sicnu::operators::RSOperatorContext ctx;
  Json::Value params( Json::objectValue );
  params["before"] = before.toStdString();
  params["after"] = after.toStdString();
  params["output"] = ( tmp.path() + "/ft_mask.tif" ).toStdString();
  params["method"] = "difference";
  params["makeMask"] = true;
  params["thresholdMethod"] = "manual";
  params["threshold"] = 10.0;
  const Json::Value res = op.run( params, ctx );

  REQUIRE( res.isMember( "changedArea" ) );
  REQUIRE( res.isMember( "changedAreaUnit" ) );
  CHECK( res["changedAreaUnit"].asString() == "m2" );

  const double changed = res["changedPixels"].asDouble();
  REQUIRE( changed == 1.0 );
  const double ftPerMetre = 0.30480060960121924;
  const double expectedM2 = 4.0 * ftPerMetre * ftPerMetre; // 2 ft × 2 ft pixel
  CHECK( res["changedArea"].asDouble() == Catch::Approx( expectedM2 ).epsilon( 1e-6 ) );
}

// ===========================================================================
// S11 — RsSpatialCrossValidation::evaluate must accept CV_64F feature
// matrices (converting per fold) instead of throwing on the copy.
// ===========================================================================
TEST_CASE( "R26-S11 spatial CV accepts CV_64F features", "[r26][classification][spatial-cv]" )
{
  cv::Mat X( 8, 2, CV_64F );
  cv::Mat y( 8, 1, CV_32S );
  for ( int i = 0; i < 8; ++i )
  {
    X.at<double>( i, 0 ) = i;
    X.at<double>( i, 1 ) = 0.0;
    y.at<int>( i, 0 ) = ( i < 4 ) ? 2 : 1;
  }

  QVector<RsSpatialCrossValidation::Fold> folds;
  RsSpatialCrossValidation::Fold f1;
  f1.testIndices = { 0, 1, 2, 3 };
  f1.trainIndices = { 4, 5, 6, 7 };
  RsSpatialCrossValidation::Fold f2;
  f2.testIndices = { 4, 5, 6, 7 };
  f2.trainIndices = { 0, 1, 2, 3 };
  folds.append( f1 );
  folds.append( f2 );

  auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
    return std::make_unique<ConstantBackend>();
  };
  const std::vector<double> coords;   // audit vacuous: no coordinates
  const std::vector<int> groupIds;
  const auto r = RsSpatialCrossValidation::evaluate( X, y, coords, groupIds,
                                                     folds, factory,
                                                     /*scaleFeatures=*/false );
  CHECK( r.ok() );
  CHECK( r.foldAccuracies.size() == 2 );
  // Predictions read the (converted) features: both folds classify exactly.
  // Pre-fix the CV_64F→CV_32F row copy silently detaches the destination
  // buffer, the fold matrices stay uninitialised and accuracy collapses.
  CHECK( r.foldAccuracies[0] == 1.0 );
  CHECK( r.foldAccuracies[1] == 1.0 );
}

// ===========================================================================
// S12 — RsFeatureScaler::fromJson must fail closed on an unknown method
// discriminator or a future sidecar version, mirroring fit's rejection
// discipline (a foreign sidecar must not silently rescale as ZScore).
// ===========================================================================
TEST_CASE( "R26-S12 feature scaler fromJson rejects unknown method/version",
           "[r26][classification][scaler]" )
{
  RsFeatureScaler fitted;
  cv::Mat X( 4, 2, CV_32F );
  X.at<float>( 0, 0 ) = 0.0f;  X.at<float>( 0, 1 ) = 10.0f;
  X.at<float>( 1, 0 ) = 1.0f;  X.at<float>( 1, 1 ) = 20.0f;
  X.at<float>( 2, 0 ) = 2.0f;  X.at<float>( 2, 1 ) = 30.0f;
  X.at<float>( 3, 0 ) = 3.0f;  X.at<float>( 3, 1 ) = 40.0f;
  REQUIRE( fitted.fit( X, RsFeatureScaler::Method::MinMax ) );

  SECTION( "round-trip of a valid sidecar still loads" )
  {
    RsFeatureScaler reloaded;
    CHECK( reloaded.fromJson( fitted.toJson() ) );
    CHECK( reloaded.isFitted() );
  }
  SECTION( "unknown method discriminator fails closed" )
  {
    QJsonObject obj = fitted.toJson();
    obj["method"] = 7;
    RsFeatureScaler reloaded;
    CHECK( !reloaded.fromJson( obj ) );
    CHECK( !reloaded.isFitted() );
  }
  SECTION( "future sidecar version fails closed" )
  {
    QJsonObject obj = fitted.toJson();
    obj["version"] = 2;
    RsFeatureScaler reloaded;
    CHECK( !reloaded.fromJson( obj ) );
    CHECK( !reloaded.isFitted() );
  }
}

// ===========================================================================
// E2E — production registration surface: resolve rs:change_detection through
// RSOperatorRegistry (the same source the CLI/GUI/MCP surfaces use), run the
// streaming kernel on real GeoTIFFs and read the artifact back with GDAL.
// The feet-CRS fixture doubles as the S10 unit-of-measure regression.
// ===========================================================================
TEST_CASE( "R26-E2E registry-resolved change chain produces verifiable artifact",
           "[r26][e2e][change]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.create( "rs:change_detection" ) != nullptr );

  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 64, H = 64;
  // UTM-like metre CRS: area contract checked in the projected-metre path.
  const char *utmWkt =
      "PROJCS[\"WGS 84 / UTM zone 50N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
      "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
      "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",117],"
      "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
      "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";
  const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4400000.0, 0.0, -30.0 };

  const auto makeRaster = [&]( const QString &path, float base ) {
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
    REQUIRE( GDALSetProjection( ds, utmWkt ) == CE_None );
    std::vector<float> data( W * H );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        data[static_cast<size_t>( r * W + c )] =
            base + 5.0f * std::sin( 0.3f * r ) + static_cast<float>( c % 7 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           data.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  };
  const QString before = tmp.path() + "/e2e_before.tif";
  const QString after = tmp.path() + "/e2e_after.tif";
  const QString output = tmp.path() + "/e2e_mask.tif";
  makeRaster( before, 100.0f );
  makeRaster( after, 100.0f );
  // Strong, localised change: first row of the after image.
  {
    GDALDatasetH ds = GDALOpen( after.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    std::vector<float> row( W, 500.0f );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, 1,
                           row.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  }

  auto op = registry.create( "rs:change_detection" );
  REQUIRE( op != nullptr );
  sicnu::operators::RSOperatorContext ctx;
  Json::Value params( Json::objectValue );
  params["before"] = before.toStdString();
  params["after"] = after.toStdString();
  params["output"] = output.toStdString();
  params["method"] = "difference";
  params["makeMask"] = true;
  params["thresholdMethod"] = "manual";
  params["threshold"] = 50.0;
  const Json::Value res = op->run( params, ctx );

  REQUIRE( res.isMember( "changedPixels" ) );
  REQUIRE( res["changedPixels"].asUInt64() == static_cast<Json::UInt64>( W ) );
  REQUIRE( res.isMember( "changedArea" ) );
  REQUIRE( res["changedAreaUnit"].asString() == "m2" );
  // 30 m x 30 m pixels: one row of 64 changed pixels = 64 * 900 m².
  CHECK( res["changedArea"].asDouble() == Catch::Approx( 64.0 * 900.0 ).epsilon( 1e-9 ) );

  // The artifact exists and is readable through GDAL with the input grid.
  GDALDatasetH out = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( out != nullptr );
  CHECK( GDALGetRasterXSize( out ) == W );
  CHECK( GDALGetRasterYSize( out ) == H );
  std::vector<float> row0( W ), row1( W );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( out, 1 ), GF_Read, 0, 0, W, 1,
                         row0.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( out, 1 ), GF_Read, 0, 1, W, 1,
                         row1.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( out );
  // Mask semantics: changed row flagged, stable row clear.
  for ( int c = 0; c < W; ++c )
  {
    CHECK( row0[static_cast<size_t>( c )] == 1.0f );
    CHECK( row1[static_cast<size_t>( c )] == 0.0f );
  }
}
