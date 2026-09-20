// test_spectral_cem.cpp — Spectral Intelligence 12.0 work package A: CEM
// target detector kernel (closed forms, streaming correlation accumulation,
// scaled loading, condition proxy, fail-closed refusals).
//
// Derivations: for a diagonal correlation the filter has the closed form
// w = R⁻¹t/(tᵀR⁻¹t); e.g. R = diag(4,1), t = (1,1) ⇒ R⁻¹t = (1/4, 1),
// tᵀR⁻¹t = 5/4 ⇒ w = (1/5, 4/5). With unit loading the loaded matrix is
// R' = diag(6.5, 3.5) ⇒ w = (7/20, 13/20). Every score of the target itself
// is exactly 1 by the constraint wᵀt = 1.

#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/spectral_cem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace SpectralCem;
using SpectralAnomaly::conditionProxy;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "CEM filter: closed form on a diagonal correlation", "[spectral][detection][cem]" )
{
  // R = diag(4,1), t = (1,1) ⇒ w = (1/5, 4/5).
  const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
  const float t[2] = { 1.0f, 1.0f };
  Filter filter;
  REQUIRE( buildFilter( t, 2, corr, 0.0, &filter ) );
  REQUIRE( filter.weight[0] == Catch::Approx( 0.2 ).margin( 1e-12 ) );
  REQUIRE( filter.weight[1] == Catch::Approx( 0.8 ).margin( 1e-12 ) );

  std::vector<double> scratch( 2, 0.0 );
  // The target scores exactly 1 (the distortionless constraint).
  REQUIRE( cemScore( t, filter, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
  // The zero spectrum scores 0.
  const float zero[2] = { 0.0f, 0.0f };
  REQUIRE( cemScore( zero, filter, 2, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
  // Closed-form score: 0.2·3 + 0.8·1 = 1.4.
  const float x[2] = { 3.0f, 1.0f };
  REQUIRE( cemScore( x, filter, 2, &scratch ) == Catch::Approx( 1.4 ).margin( 1e-12 ) );
  // CEM is signed (unlike the squared ACE): spectrum (1,-1) ⊥ t scores
  // 0.2·1 + 0.8·(−1) = −0.6.
  const float anti[2] = { 1.0f, -1.0f };
  REQUIRE( cemScore( anti, filter, 2, &scratch ) == Catch::Approx( -0.6 ).margin( 1e-12 ) );
}

TEST_CASE( "CEM constraint is invariant to target scaling", "[spectral][detection][cem]" )
{
  const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
  const float t[2] = { 1.0f, 1.0f };
  const float tScaled[2] = { 2.0f, 2.0f };
  Filter f1;
  Filter f2;
  REQUIRE( buildFilter( t, 2, corr, 0.0, &f1 ) );
  REQUIRE( buildFilter( tScaled, 2, corr, 0.0, &f2 ) );
  // w' = w/2 and t' = 2t satisfy the same constraint; the TARGET still scores 1.
  REQUIRE( f2.weight[0] == Catch::Approx( f1.weight[0] / 2.0 ).margin( 1e-12 ) );

  std::vector<double> scratch( 2, 0.0 );
  REQUIRE( cemScore( tScaled, f2, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "CEM scaled loading: closed form with unit loading", "[spectral][detection][cem]" )
{
  // R = diag(4,1), loading = 1 ⇒ load = 1·(tr/B) = 2.5 ⇒ R' = diag(6.5,3.5)
  // ⇒ w = (7/20, 13/20).
  const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
  const float t[2] = { 1.0f, 1.0f };
  Filter filter;
  REQUIRE( buildFilter( t, 2, corr, 1.0, &filter ) );
  REQUIRE( filter.weight[0] == Catch::Approx( 0.35 ).margin( 1e-12 ) );
  REQUIRE( filter.weight[1] == Catch::Approx( 0.65 ).margin( 1e-12 ) );

  std::vector<double> scratch( 2, 0.0 );
  REQUIRE( cemScore( t, filter, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "CEM correlation accumulation: exact second moments, validity predicate", "[spectral][detection][cem]" )
{
  // Two 2-band pixels: (1,2), (3,4). Σxxᵀ = [[10,10],[10,20]] ⇒ R = [[5,5],[5,10]].
  {
    const float pixels[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    CorrelationStats stats;
    accumulateCorrelation( pixels, 2, 2, &stats, true );
    finalizeCorrelation( &stats );
    REQUIRE( stats.count == 2 );
    REQUIRE( stats.bands == 2 );
    // (1,2),(3,4): Σ = [[10,14],[14,20]] ⇒ R = [[5,7],[7,10]].
    REQUIRE( stats.correlation[0] == Catch::Approx( 5.0 ).margin( 1e-12 ) );
    REQUIRE( stats.correlation[1] == Catch::Approx( 7.0 ).margin( 1e-12 ) );
    REQUIRE( stats.correlation[2] == Catch::Approx( 7.0 ).margin( 1e-12 ) );
    REQUIRE( stats.correlation[3] == Catch::Approx( 10.0 ).margin( 1e-12 ) );
  }
  // A pixel with a non-finite band is excluded as a whole.
  {
    const float pixels[8] = { 1.0f, 2.0f, 3.0f, kNaN, 5.0f, 6.0f, 7.0f, 8.0f };
    CorrelationStats stats;
    accumulateCorrelation( pixels, 4, 2, &stats, true );
    finalizeCorrelation( &stats );
    REQUIRE( stats.count == 3 );
    // Valid (1,2), (5,6), (7,8): Σ = [[75,88],[88,104]] ⇒ /3.
    REQUIRE( stats.correlation[0] == Catch::Approx( 25.0 ).margin( 1e-12 ) );
    REQUIRE( stats.correlation[1] == Catch::Approx( 88.0 / 3.0 ).margin( 1e-12 ) );
    REQUIRE( stats.correlation[3] == Catch::Approx( 104.0 / 3.0 ).margin( 1e-12 ) );
  }
  // Declared NoData on band 0 excludes (0, 7).
  {
    const float pixels[6] = { 1.0f, 2.0f, 0.0f, 7.0f, 3.0f, 4.0f };
    const float noData[2] = { 0.0f, 0.0f };
    const uint8_t hasNoData[2] = { 1, 0 };
    CorrelationStats stats;
    accumulateCorrelation( pixels, 3, 2, &stats, true, noData, hasNoData );
    finalizeCorrelation( &stats );
    REQUIRE( stats.count == 2 );
    // (1,2) and (3,4): Σ = [[10,14],[14,20]] ⇒ /2 = [[5,7],[7,10]].
    REQUIRE( stats.correlation[1] == Catch::Approx( 7.0 ).margin( 1e-12 ) );
  }
  // Without skipNonFinite nothing is filtered.
  {
    const float pixels[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    CorrelationStats stats;
    accumulateCorrelation( pixels, 2, 2, &stats, false );
    finalizeCorrelation( &stats );
    REQUIRE( stats.count == 2 );
  }
}

TEST_CASE( "CEM full pipeline: accumulated scene reproduces the constraint", "[spectral][detection][cem]" )
{
  // Scene whose exact second-moment matrix is R = diag(4,1): 2×(2,1) and
  // 2×(2,−1) give Σx²=16, Σy²=4, Σxy=0 over N=4 — a full-rank diagonal R.
  // (Identical pixels would be rank-1 and must be REFUSED, not scored.)
  const float pixels[8] = { 2.0f, 1.0f, 2.0f, -1.0f, 2.0f, 1.0f, 2.0f, -1.0f };
  CorrelationStats stats;
  accumulateCorrelation( pixels, 4, 2, &stats, true );
  finalizeCorrelation( &stats );
  REQUIRE( stats.count == 4 );
  REQUIRE( stats.correlation[0] == Catch::Approx( 4.0 ).margin( 1e-12 ) );
  REQUIRE( stats.correlation[1] == Catch::Approx( 0.0 ).margin( 1e-12 ) );
  REQUIRE( stats.correlation[3] == Catch::Approx( 1.0 ).margin( 1e-12 ) );

  const float t[2] = { 1.0f, 1.0f };
  Filter filter;
  REQUIRE( buildFilter( t, 2, stats.correlation, 0.0, &filter ) );
  std::vector<double> scratch( 2, 0.0 );
  REQUIRE( cemScore( t, filter, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
  // Background copies of (2,1) score wᵀ(2,1) = 0.2·2 + 0.8·1 = 1.2.
  const float bg[2] = { 2.0f, 1.0f };
  REQUIRE( cemScore( bg, filter, 2, &scratch ) == Catch::Approx( 1.2 ).margin( 1e-12 ) );
}

TEST_CASE( "CEM min-samples floor is fail-closed and loading is the escape hatch", "[spectral][detection][cem]" )
{
  REQUIRE( minSamplesRequired( 5, false ) == 12 ); // 2B+2, same as local RX Full
  REQUIRE( minSamplesRequired( 5, true ) == 6 );   // B+1 with explicit loading
  REQUIRE( minSamplesRequired( 1, false ) == 4 );
  REQUIRE( minSamplesRequired( 0, false ) == 0 );
  REQUIRE( minSamplesRequired( -3, true ) == 0 );
}

TEST_CASE( "CEM buildFilter refusals", "[spectral][detection][cem]" )
{
  const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
  const float t[2] = { 1.0f, 1.0f };
  Filter filter;

  // Structurally invalid calls.
  REQUIRE_FALSE( buildFilter( nullptr, 2, corr, 0.0, &filter ) );
  REQUIRE_FALSE( buildFilter( t, 2, corr, 0.0, nullptr ) );
  REQUIRE_FALSE( buildFilter( t, 0, corr, 0.0, &filter ) );
  REQUIRE_FALSE( buildFilter( t, 2, { 4.0, 0.0, 0.0 }, 0.0, &filter ) );

  // Bad loading.
  REQUIRE_FALSE( buildFilter( t, 2, corr, -1e-9, &filter ) );
  REQUIRE_FALSE( buildFilter( t, 2, corr, kNaN, &filter ) );
  REQUIRE_FALSE( buildFilter( t, 2, corr, std::numeric_limits<double>::infinity(), &filter ) );

  // Non-finite correlation / target, zero trace.
  REQUIRE_FALSE( buildFilter( t, 2, { 4.0, kNaN, 0.0, 1.0 }, 0.0, &filter ) );
  REQUIRE_FALSE( buildFilter( t, 2, { 0.0, 0.0, 0.0, 0.0 }, 0.0, &filter ) );
  const float badT[2] = { 1.0f, kNaN };
  REQUIRE_FALSE( buildFilter( badT, 2, corr, 0.0, &filter ) );

  // Singular correlation refuses at loading=0...
  const std::vector<double> rank1{ 1.0, 1.0, 1.0, 1.0 };
  REQUIRE_FALSE( buildFilter( t, 2, rank1, 0.0, &filter ) );
  // ...and the explicitly loaded matrix is the documented escape hatch.
  REQUIRE( buildFilter( t, 2, rank1, 1e-3, &filter ) );

  // An invertible but indefinite matrix must NOT pass silently: the
  // constraint denominator tᵀR'⁻¹t has to be strictly positive.
  const std::vector<double> indefinite{ 1.0, 0.0, 0.0, -1.0 };
  REQUIRE_FALSE( buildFilter( t, 2, indefinite, 0.0, &filter ) );
}

TEST_CASE( "CEM score NaN contracts", "[spectral][detection][cem]" )
{
  const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
  const float t[2] = { 1.0f, 1.0f };
  Filter filter;
  REQUIRE( buildFilter( t, 2, corr, 0.0, &filter ) );

  std::vector<double> scratch( 2, 0.0 );
  const float dirty[2] = { kNaN, 1.0f };
  REQUIRE( std::isnan( cemScore( dirty, filter, 2, &scratch ) ) );
  // Guard contracts.
  REQUIRE( std::isnan( cemScore( nullptr, filter, 2, &scratch ) ) );
  std::vector<double> tiny( 1, 0.0 );
  const float x[2] = { 1.0f, 1.0f };
  REQUIRE( std::isnan( cemScore( x, filter, 2, &tiny ) ) );
  Filter wrongSize;
  wrongSize.weight = { 1.0 };
  REQUIRE( std::isnan( cemScore( x, wrongSize, 2, &scratch ) ) );
}

TEST_CASE( "CEM condition proxy: closed forms on diagonal fixtures", "[spectral][detection][cem]" )
{
  // diag(4,1): λmax = 4, tr = 5, B = 2 ⇒ proxy = 8/5 = 1.6 (a true cond₂ is
  // 4; the proxy is the documented lower bound λmax·B/tr).
  REQUIRE( conditionProxy( { 4.0, 0.0, 0.0, 1.0 }, 2 ) ==
           Catch::Approx( 1.6 ).margin( 1e-9 ) );
  // diag(100,1,1): λmax = 100, tr = 102, B = 3 ⇒ 300/102.
  REQUIRE( conditionProxy( { 100.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 }, 3 ) ==
           Catch::Approx( 300.0 / 102.0 ).margin( 1e-9 ) );
  // Isotropic matrix: proxy = 1 exactly (λmax = tr/B).
  REQUIRE( conditionProxy( { 2.0, 0.0, 0.0, 2.0 }, 2 ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );

  // Invalid inputs.
  REQUIRE( conditionProxy( { 4.0, 0.0, 0.0 }, 2 ) == -1.0 );
  REQUIRE( conditionProxy( { 4.0, 0.0, 0.0, 1.0 }, 0 ) == -1.0 );
  REQUIRE( conditionProxy( { 0.0, 0.0, 0.0, 0.0 }, 2 ) == -1.0 );
  REQUIRE( conditionProxy( { 4.0, kNaN, 0.0, 1.0 }, 2 ) == -1.0 );
}

TEST_CASE( "rs:cem_detection E2E: planted target scores exactly 1", "[spectral][detection][cem][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Same scene family as the MF/ACE E2E: 16×16, 3 bands, checkerboard
  // background, planted target pixel and a reflected anti-target.
  constexpr int kW = 16;
  constexpr int kH = 16;
  const float c2[3] = { 14.0f, 22.0f, 34.0f };
  const float c3[3] = { 10.0f, 24.0f, 32.0f };
  const float target[3] = { 30.0f, 10.0f, 40.0f };

  RsSyntheticRasterBuilder b( kW, kH, 3 );
  b.withCrs( "EPSG:32650" );
  b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
    {
      const float *c = ( ( ( x / 2 ) + ( y / 2 ) ) % 2 == 0 ) ? c2 : c3;
      b.withPixel( 1, x, y, c[0] );
      b.withPixel( 2, x, y, c[1] );
      b.withPixel( 3, x, y, c[2] );
    }
  b.withPixel( 1, 3, 3, target[0] );
  b.withPixel( 2, 3, 3, target[1] );
  b.withPixel( 3, 3, 3, target[2] );
  b.withPixel( 1, 4, 3, 1.0f );
  b.withPixel( 2, 4, 3, 23.0f );
  b.withPixel( 3, 4, 3, 33.0f );
  REQUIRE( !b.writeToDisk( dir.filePath( "scene.tif" ) ).isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:cem_detection" );
  REQUIRE( op != nullptr );

  Json::Value params( Json::objectValue );
  params["input"] = dir.filePath( "scene.tif" ).toStdString();
  params["output"] = dir.filePath( "cem.tif" ).toStdString();
  Json::Value t( Json::arrayValue );
  t.append( target[0] );
  t.append( target[1] );
  t.append( target[2] );
  params["target"] = t;

  RSOperatorContext context;
  Json::Value result;
  REQUIRE_NOTHROW( result = op->run( params, context ) );
  REQUIRE( result["output"].isString() );
  // Diagnostics: every pixel of the synthetic scene is valid.
  REQUIRE( result["backgroundSamples"].asUInt64() ==
           static_cast<Json::UInt64>( kW ) * kH );
  REQUIRE( result["detector"].asString() == "cem" );

  GdalDatasetWrapper outDs;
  REQUIRE( outDs.open( QString::fromStdString( result["output"].asString() ) ) );
  std::vector<float> scores( static_cast<size_t>( kW ) * kH );
  REQUIRE( outDs.readBandData( 1, scores.data(), kW, kH ) );

  const float targetScore = scores[static_cast<size_t>( 3 ) * kW + 3];
  const float antiScore = scores[static_cast<size_t>( 3 ) * kW + 4];
  // The distortionless constraint makes the target score exactly 1.
  REQUIRE( targetScore == Catch::Approx( 1.0f ).margin( 1e-5f ) );
  REQUIRE( std::isfinite( antiScore ) );
  REQUIRE( targetScore > antiScore );
}

TEST_CASE( "rs:cem_detection refuses under-sampled scenes; loading is the escape hatch",
           "[spectral][detection][cem][operator][contract]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 2×3 pixels, 3 bands: 6 valid pixels. Floor without loading is 2B+2 = 8 →
  // refused; with loading > 0 the floor drops to B+1 = 4 → accepted.
  RsSyntheticRasterBuilder b( 2, 3, 3 );
  b.withCrs( "EPSG:32650" );
  b.withConstantValue( 1, 1.0f );
  b.withConstantValue( 2, 2.0f );
  b.withConstantValue( 3, 3.0f );
  const QString inPath = b.writeToDisk( dir.filePath( "tiny.tif" ) );
  REQUIRE( !inPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:cem_detection" );
  REQUIRE( op != nullptr );

  auto makeParams = [&]( Json::Value *params, const std::string &outName ) {
    ( *params )["input"] = inPath.toStdString();
    ( *params )["output"] = dir.filePath( QString::fromStdString( outName ) ).toStdString();
    Json::Value t( Json::arrayValue );
    t.append( 1.0 );
    t.append( 2.0 );
    t.append( 3.0 );
    ( *params )["target"] = t;
  };

  {
    RSOperatorContext context;
    Json::Value params;
    makeParams( &params, "out_refused.tif" );
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
  }
  {
    RSOperatorContext context;
    Json::Value params;
    makeParams( &params, "out_loaded.tif" );
    params["loading"] = 1e-3;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    REQUIRE( result["backgroundSamples"].asUInt64() == 6 );
    REQUIRE( result["loading"].asDouble() == Catch::Approx( 1e-3 ).margin( 1e-12 ) );
  }
  {
    // Negative loading is a parameter error, not a silent clamp.
    RSOperatorContext context;
    Json::Value params;
    makeParams( &params, "out_bad_loading.tif" );
    params["loading"] = -1.0;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
  }
}
