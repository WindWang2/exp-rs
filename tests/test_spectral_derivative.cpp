// test_spectral_derivative.cpp — Foundation 5.0 Milestone B3: spectral
// derivatives along the wavelength axis (kernel closed forms + operator E2E).
//
// Derivations: bands [1, 2, 4] over λ = [500, 600, 700] nm give
//   d1 = [(2−1)/100, (4−2)/100] = [0.01, 0.02]   (midpoints 550, 650)
//   d2 = [(0.02−0.01)/100] = [1e-4]              (midpoint 600)

#include "processing/algorithms/spectral_derivative.h"

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

using namespace SpectralDerivative;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "First derivative: band pairs over the wavelength axis", "[spectral][derivative]" )
{
  const float values[3] = { 1.0f, 2.0f, 4.0f };
  const double wl[3] = { 500.0, 600.0, 700.0 };
  float out[2] = { 0.0f, 0.0f };

  firstDerivative( values, wl, 3, out );
  REQUIRE( out[0] == Catch::Approx( 0.01 ).margin( 1e-12 ) );
  REQUIRE( out[1] == Catch::Approx( 0.02 ).margin( 1e-12 ) );

  double mid[2] = { 0.0, 0.0 };
  midpointAxis( wl, 3, mid );
  REQUIRE( mid[0] == Catch::Approx( 550.0 ).margin( 1e-12 ) );
  REQUIRE( mid[1] == Catch::Approx( 650.0 ).margin( 1e-12 ) );
}

TEST_CASE( "Second derivative: the first applied twice", "[spectral][derivative]" )
{
  const float values[3] = { 1.0f, 2.0f, 4.0f };
  const double wl[3] = { 500.0, 600.0, 700.0 };
  float out[1] = { 0.0f };

  secondDerivative( values, wl, 3, out );
  REQUIRE( out[0] == Catch::Approx( 1e-4 ).margin( 1e-12 ) );
}

TEST_CASE( "Derivative degenerate cases", "[spectral][derivative]" )
{
  // NaN in any participating band poisons every pair touching it.
  const float dirty[3] = { 1.0f, kNaN, 4.0f };
  const double wl[3] = { 500.0, 600.0, 700.0 };
  float out[2] = { 0.0f, 0.0f };
  firstDerivative( dirty, wl, 3, out );
  REQUIRE( std::isnan( out[0] ) );
  REQUIRE( std::isnan( out[1] ) );

  // Non-ascending axis yields NaN derivatives (the operator refuses earlier).
  const float values[3] = { 1.0f, 2.0f, 4.0f };
  const double bad[3] = { 700.0, 600.0, 500.0 };
  firstDerivative( values, bad, 3, out );
  REQUIRE( std::isnan( out[0] ) );
  REQUIRE( std::isnan( out[1] ) );

  // Tiny guards: degenerate band counts must not write.
  float one[1] = { 99.0f };
  firstDerivative( values, wl, 1, one );
  REQUIRE( one[0] == 99.0f );
}

TEST_CASE( "rs:spectral_derivative E2E over an explicit wavelength axis",
           "[spectral][derivative][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString inPath = dir.filePath( "cube.tif" );
  const QString outPath = dir.filePath( "d1.tif" );

  // 3-band 8×6 cube: band b carries the constant value 2^b (1, 2, 4).
  constexpr int kW = 8;
  constexpr int kH = 6;
  RsSyntheticRasterBuilder builder( kW, kH, 3 );
  builder.withCrs( "EPSG:4326" );
  builder.withGeoTransform( 0.0, 1.0, 6.0, -1.0 );
  builder.withConstantValue( 1, 1.0f );
  builder.withConstantValue( 2, 2.0f );
  builder.withConstantValue( 3, 4.0f );
  REQUIRE( !builder.writeToDisk( inPath ).isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:spectral_derivative" );
  REQUIRE( op != nullptr );

  Json::Value params( Json::objectValue );
  params["input"] = inPath.toStdString();
  params["output"] = outPath.toStdString();
  params["order"] = 1;
  Json::Value wl( Json::arrayValue );
  wl.append( 500.0 );
  wl.append( 600.0 );
  wl.append( 700.0 );
  params["wavelengths"] = wl;

  RSOperatorContext context;
  Json::Value result;
  REQUIRE_NOTHROW( result = op->run( params, context ) );
  REQUIRE( result["bandsOut"].asInt() == 2 );
  REQUIRE( result["outputWavelengthsNm"].size() == 2 );
  REQUIRE( result["outputWavelengthsNm"][0].asDouble() == Catch::Approx( 550.0 ).margin( 1e-9 ) );

  GdalDatasetWrapper outDs;
  REQUIRE( outDs.open( outPath ) );
  REQUIRE( outDs.bandCount() == 2 );
  std::vector<float> b1( static_cast<size_t>( kW ) * kH );
  std::vector<float> b2( static_cast<size_t>( kW ) * kH );
  REQUIRE( outDs.readBandData( 1, b1.data(), kW, kH ) );
  REQUIRE( outDs.readBandData( 2, b2.data(), kW, kH ) );
  for ( const float v : b1 )
    REQUIRE( v == Catch::Approx( 0.01f ).margin( 1e-9 ) );
  for ( const float v : b2 )
    REQUIRE( v == Catch::Approx( 0.02f ).margin( 1e-9 ) );
}

TEST_CASE( "rs:spectral_derivative refuses index-space derivatives",
           "[spectral][derivative][operator][contract]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  RsSyntheticRasterBuilder builder( 4, 3, 3 );
  builder.withCrs( "EPSG:4326" );
  builder.withConstantValue( 1, 1.0f );
  builder.withConstantValue( 2, 2.0f );
  builder.withConstantValue( 3, 4.0f );
  const QString inPath = builder.writeToDisk( dir.filePath( "cube.tif" ) );
  REQUIRE( !inPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:spectral_derivative" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["input"] = inPath.toStdString();
  params["output"] = dir.filePath( "out.tif" ).toStdString();
  params["order"] = 1;

  // No WAVELENGTH metadata on the synthetic bands and no explicit axis:
  // a derivative against the band index is not a spectral derivative.
  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );

  // Bad order is a typed parameter refusal.
  params["wavelengths"] = Json::Value( Json::arrayValue );
  params["wavelengths"].append( 500.0 );
  params["wavelengths"].append( 600.0 );
  params["wavelengths"].append( 700.0 );
  params["order"] = 3;
  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
