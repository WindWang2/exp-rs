// test_spectral_detection.cpp — Foundation 5.0 Milestone C: matched filter
// and ACE detectors (kernel closed forms + synthetic-scene E2E).
//
// Derivations: identity background (μ = 0, Σ = I ⇒ Σ⁻¹ = I) gives the
// textbook forms — MF = tᵀx (signed dot product), ACE = (tᵀx)²/(‖t‖²‖x‖²),
// the squared cosine of the spectral angle (1 = parallel, 0 = orthogonal).

#include "processing/algorithms/spectral_detection.h"

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

using namespace SpectralDetection;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
const std::vector<double> kZeroMean{ 0.0, 0.0 };
const std::vector<double> kIdentityInv{ 1.0, 0.0, 0.0, 1.0 };
} // namespace

TEST_CASE( "Target model build: closed forms and refusals", "[spectral][detection]" )
{
  const float t[2] = { 1.0f, 0.0f };
  TargetModel model;
  REQUIRE( buildTargetModel( t, 2, kZeroMean, kIdentityInv, &model ) );
  REQUIRE( model.invCovT[0] == Catch::Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( model.invCovT[1] == Catch::Approx( 0.0 ).margin( 1e-12 ) );
  REQUIRE( model.tWhitenedNorm2 == Catch::Approx( 1.0 ).margin( 1e-12 ) );

  // Zero target against zero mean → zero whitened norm → refused.
  const float zero[2] = { 0.0f, 0.0f };
  TargetModel bad;
  REQUIRE_FALSE( buildTargetModel( zero, 2, kZeroMean, kIdentityInv, &bad ) );

  // Size mismatches are refused.
  REQUIRE_FALSE( buildTargetModel( t, 2, { 0.0 }, kIdentityInv, &bad ) );
  REQUIRE_FALSE( buildTargetModel( t, 2, kZeroMean, { 1.0 }, &bad ) );
}

TEST_CASE( "Matched filter: signed projection on the identity background", "[spectral][detection]" )
{
  const float t[2] = { 1.0f, 0.0f };
  TargetModel model;
  REQUIRE( buildTargetModel( t, 2, kZeroMean, kIdentityInv, &model ) );

  std::vector<double> scratch( 2, 0.0 );
  const float along[2] = { 2.0f, 0.0f };
  REQUIRE( matchedFilterScore( along, model, kZeroMean, 2, &scratch ) ==
           Catch::Approx( 2.0 ).margin( 1e-9 ) );

  const float opposite[2] = { -1.0f, 0.0f };
  REQUIRE( matchedFilterScore( opposite, model, kZeroMean, 2, &scratch ) ==
           Catch::Approx( -1.0 ).margin( 1e-9 ) ); // signed: opposite direction

  const float orthogonal[2] = { 0.0f, 5.0f };
  REQUIRE( matchedFilterScore( orthogonal, model, kZeroMean, 2, &scratch ) ==
           Catch::Approx( 0.0 ).margin( 1e-9 ) );

  const float dirty[2] = { 1.0f, kNaN };
  REQUIRE( std::isnan( matchedFilterScore( dirty, model, kZeroMean, 2, &scratch ) ) );
}

TEST_CASE( "ACE: squared cosine on the identity background", "[spectral][detection]" )
{
  const float t[2] = { 1.0f, 0.0f };
  TargetModel model;
  REQUIRE( buildTargetModel( t, 2, kZeroMean, kIdentityInv, &model ) );

  std::vector<double> scratch( 2, 0.0 );
  const float parallel[2] = { 3.0f, 0.0f };
  REQUIRE( aceScore( parallel, model, kZeroMean, kIdentityInv, 2, &scratch ) ==
           Catch::Approx( 1.0 ).margin( 1e-9 ) );

  const float orthogonal[2] = { 0.0f, 2.0f };
  REQUIRE( aceScore( orthogonal, model, kZeroMean, kIdentityInv, 2, &scratch ) ==
           Catch::Approx( 0.0 ).margin( 1e-9 ) );

  // 45°: cos² = 0.5.
  const float diag[2] = { 1.0f, 1.0f };
  REQUIRE( aceScore( diag, model, kZeroMean, kIdentityInv, 2, &scratch ) ==
           Catch::Approx( 0.5 ).margin( 1e-9 ) );

  // Degenerate whitened norm (pixel exactly at the mean) → NaN.
  const float atMean[2] = { 0.0f, 0.0f };
  REQUIRE( std::isnan( aceScore( atMean, model, kZeroMean, kIdentityInv, 2, &scratch ) ) );

  const float dirty[2] = { kNaN, 1.0f };
  REQUIRE( std::isnan( aceScore( dirty, model, kZeroMean, kIdentityInv, 2, &scratch ) ) );
}

TEST_CASE( "rs:matched_filter / rs:ace E2E: planted target dominates", "[spectral][detection][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 16×16, 3 bands: checkerboard background of two colors (non-degenerate
  // covariance), a planted target pixel and a reflected "anti-target".
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
  // Anti-target: background mean reflected through the first band.
  b.withPixel( 1, 4, 3, 1.0f ); // far from both colors in band 1
  b.withPixel( 2, 4, 3, 23.0f );
  b.withPixel( 3, 4, 3, 33.0f );
  REQUIRE( !b.writeToDisk( dir.filePath( "scene.tif" ) ).isEmpty() );

  for ( const char *opName : { "rs:matched_filter", "rs:ace" } )
  {
    DYNAMIC_SECTION( "operator " << opName )
    {
      auto op = RSOperatorRegistry::instance().create( opName );
      REQUIRE( op != nullptr );

      Json::Value params( Json::objectValue );
      params["input"] = dir.filePath( "scene.tif" ).toStdString();
      params["output"] = QString::fromStdString( std::string( opName ) + ".tif" ).toStdString();
      Json::Value t( Json::arrayValue );
      t.append( target[0] );
      t.append( target[1] );
      t.append( target[2] );
      params["target"] = t;

      RSOperatorContext context;
      Json::Value result;
      REQUIRE_NOTHROW( result = op->run( params, context ) );
      REQUIRE( result["output"].isString() );

      GdalDatasetWrapper outDs;
      REQUIRE( outDs.open( QString::fromStdString( result["output"].asString() ) ) );
      std::vector<float> scores( static_cast<size_t>( kW ) * kH );
      REQUIRE( outDs.readBandData( 1, scores.data(), kW, kH ) );

      const float targetScore = scores[static_cast<size_t>( 3 ) * kW + 3];
      const float antiScore = scores[static_cast<size_t>( 3 ) * kW + 4];
      REQUIRE( std::isfinite( targetScore ) );
      REQUIRE( std::isfinite( antiScore ) );

      if ( std::string( opName ) == "rs:ace" )
      {
        // Target is exactly parallel to itself: score 1.
        REQUIRE( targetScore == Catch::Approx( 1.0f ).margin( 1e-5f ) );
        REQUIRE( targetScore > antiScore );
        for ( const float s : scores )
        {
          if ( std::isnan( s ) )
            continue;
          REQUIRE( s >= -1e-6f );
          REQUIRE( s <= 1.0f + 1e-6f );
        }
      }
      else
      {
        // MF is signed: the target direction scores high, the reflected
        // anti-target (partially opposite) scores lower.
        REQUIRE( targetScore > 0.0f );
        REQUIRE( targetScore > antiScore );
      }
    }
  }
}

TEST_CASE( "rs:matched_filter refuses wrong-length targets", "[spectral][detection][operator][contract]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  RsSyntheticRasterBuilder b( 4, 3, 3 );
  b.withCrs( "EPSG:32650" );
  b.withConstantValue( 1, 1.0f );
  b.withConstantValue( 2, 2.0f );
  b.withConstantValue( 3, 3.0f );
  const QString inPath = b.writeToDisk( dir.filePath( "in.tif" ) );
  REQUIRE( !inPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:matched_filter" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["input"] = inPath.toStdString();
  params["output"] = dir.filePath( "out.tif" ).toStdString();
  Json::Value t( Json::arrayValue );
  t.append( 1.0 );
  t.append( 2.0 ); // only 2 of 3 bands
  params["target"] = t;

  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
