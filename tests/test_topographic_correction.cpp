// test_topographic_correction.cpp — Foundation 5.0 Milestone B.1: illumination
// topographic correction (kernel closed forms + synthetic-scene E2E).
//
// Derivations (see topographic_correction.h for the contracts):
//  * Horn gradient of a unit x-ramp is dzdx = 1, dzdy = 0 (column weights
//    (c+2f+i)-(a+2d+g) = 8·dx over 8·cellSize).
//  * Platform aspect = atan2(−dzdx, dzdy) in degrees, +360 wrapped: a surface
//    rising eastward (dzdx > 0) faces downhill west → 270°.
//  * Illumination: cos_i = cosθz·coss + sinθz·sins·cos(φs−φa); a slope facing
//    the sun exactly (φa = φs) gives cos_i = cos(θz − s); flat cells give
//    cos_i = cosθz.
//  * C-correction removes a scene that was generated as L = a + b·cos_i
//    exactly: c = a/b ⇒ L' = (a + b·cosi)(cosθz + c)/(cosi + c)
//    = b·cosθz + a for every pixel (the flat-illumination value).

#include "processing/algorithms/topographic_correction.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace TopographicCorrection;

namespace
{
constexpr double kDeg = 3.14159265358979323846 / 180.0;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

double cosd( double deg ) { return std::cos( deg * kDeg ); }
double sind( double deg ) { return std::sin( deg * kDeg ); }
} // namespace

TEST_CASE( "Horn gradients match analytic derivatives", "[topo][kernel]" )
{
  const float rampX[9] = { 0, 1, 2, 0, 1, 2, 0, 1, 2 }; // dz/dx = 1
  double dzdx = 0.0;
  double dzdy = 0.0;
  hornGradient( rampX, 1.0, 1.0, &dzdx, &dzdy );
  REQUIRE( dzdx == Catch::Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( dzdy == Catch::Approx( 0.0 ).margin( 1e-12 ) );

  const float rampY[9] = { 0, 0, 0, 1, 1, 1, 2, 2, 2 }; // dz/dy = 1
  hornGradient( rampY, 1.0, 1.0, &dzdx, &dzdy );
  REQUIRE( dzdx == Catch::Approx( 0.0 ).margin( 1e-12 ) );
  REQUIRE( dzdy == Catch::Approx( 1.0 ).margin( 1e-12 ) );

  const float flat[9] = { 5, 5, 5, 5, 5, 5, 5, 5, 5 };
  hornGradient( flat, 2.0, 3.0, &dzdx, &dzdy );
  REQUIRE( dzdx == 0.0 );
  REQUIRE( dzdy == 0.0 );

  // Cell-size scaling: the same ramp over 2 m cells halves the gradient.
  hornGradient( rampX, 2.0, 2.0, &dzdx, &dzdy );
  REQUIRE( dzdx == Catch::Approx( 0.5 ).margin( 1e-12 ) );
}

TEST_CASE( "Slope/aspect follow the platform conventions", "[topo][kernel]" )
{
  double slopeDeg = 0.0;
  double aspectDeg = 0.0;
  slopeAspectDeg( 1.0, 0.0, &slopeDeg, &aspectDeg );
  REQUIRE( slopeDeg == Catch::Approx( 45.0 ).margin( 1e-9 ) );
  REQUIRE( aspectDeg == Catch::Approx( 270.0 ).margin( 1e-9 ) ); // faces west

  slopeAspectDeg( -1.0, 0.0, &slopeDeg, &aspectDeg );
  REQUIRE( aspectDeg == Catch::Approx( 90.0 ).margin( 1e-9 ) ); // faces east

  slopeAspectDeg( 0.0, 0.0, &slopeDeg, &aspectDeg );
  REQUIRE( slopeDeg == 0.0 );
  REQUIRE( aspectDeg == -1.0 ); // platform flat marker
}

TEST_CASE( "Illumination cosine: closed forms", "[topo][kernel]" )
{
  // Flat cells: cos_i = cosθz regardless of azimuths.
  REQUIRE( illuminationCosine( 0.0, -1.0, 30.0, 150.0 ) == Catch::Approx( cosd( 30.0 ) ).margin( 1e-12 ) );

  // Slope facing the sun exactly: cos_i = cos(θz − s).
  REQUIRE( illuminationCosine( 20.0, 150.0, 30.0, 150.0 ) == Catch::Approx( cosd( 10.0 ) ).margin( 1e-12 ) );

  // Slope facing away: cos_i = cos(θz + s).
  REQUIRE( illuminationCosine( 20.0, 330.0, 30.0, 150.0 ) == Catch::Approx( cosd( 50.0 ) ).margin( 1e-12 ) );
}

TEST_CASE( "OLS and Minnaert regressions: exact fits and refusals", "[topo][kernel]" )
{
  OlsRegression ols;
  for ( int i = 0; i < 10; ++i )
    ols.add( 0.1 * i, 1.0 + 2.0 * ( 0.1 * i ) );
  double a = 0.0;
  double b = 0.0;
  REQUIRE( ols.fit( &a, &b ) );
  REQUIRE( a == Catch::Approx( 1.0 ).margin( 1e-9 ) );
  REQUIRE( b == Catch::Approx( 2.0 ).margin( 1e-9 ) );
  REQUIRE( ols.count() == 10 );

  // Non-finite pairs are refused (count does not move).
  ols.add( std::numeric_limits<double>::quiet_NaN(), 1.0 );
  REQUIRE( ols.count() == 10 );

  // Fewer than two points, or no x spread, refuse to fit.
  OlsRegression one;
  one.add( 0.5, 1.0 );
  REQUIRE_FALSE( one.fit( &a, &b ) );
  OlsRegression flatX;
  flatX.add( 0.5, 1.0 );
  flatX.add( 0.5, 2.0 );
  REQUIRE_FALSE( flatX.fit( &a, &b ) );

  // L = cosi^k with k = 0.5 → the log-log fit recovers k.
  MinnaertRegression mr;
  for ( double ci : { 0.2, 0.4, 0.6, 0.8 } )
    mr.add( ci, std::pow( ci, 0.5 ) );
  double k = 0.0;
  REQUIRE( mr.fit( &k ) );
  REQUIRE( k == Catch::Approx( 0.5 ).margin( 1e-9 ) );

  // Domain: non-positive illumination or radiance pairs are refused.
  MinnaertRegression domain;
  domain.add( -0.5, 1.0 );
  domain.add( 0.5, -1.0 );
  REQUIRE( domain.count() == 0 );
}

TEST_CASE( "fitBand and correctPixel semantics", "[topo][kernel]" )
{
  const double zen = 30.0;

  // Cosine needs no fit and is always usable.
  BandFit cosFit = fitBand( Method::Cosine, zen, OlsRegression{}, MinnaertRegression{} );
  REQUIRE( cosFit.usable );
  REQUIRE( cosFit.cosZenith == Catch::Approx( cosd( 30.0 ) ).margin( 1e-12 ) );

  // C-correction from a perfect linear scene: c = a/b.
  OlsRegression ols;
  for ( int i = 1; i <= 20; ++i )
    ols.add( 0.02 * i, 0.1 + 0.5 * ( 0.02 * i ) );
  BandFit cFit = fitBand( Method::CCorrection, zen, ols, MinnaertRegression{} );
  REQUIRE( cFit.usable );
  REQUIRE( cFit.a == Catch::Approx( 0.1 ).margin( 1e-9 ) );
  REQUIRE( cFit.b == Catch::Approx( 0.5 ).margin( 1e-9 ) );
  REQUIRE( cFit.c == Catch::Approx( 0.2 ).margin( 1e-9 ) );

  // C-correction of that scene maps every pixel to a + b·cosθz exactly
  // (L = b(cosi + c) ⇒ L' = b(cosθz + c) = a + b·cosθz).
  const float value = static_cast<float>( 0.1 + 0.5 * 0.3 ); // a scene pixel at cosi = 0.3
  const float corrected = correctPixel( Method::CCorrection, value, 0.3, cFit );
  REQUIRE( corrected == Catch::Approx( 0.1 + 0.5 * cosd( 30.0 ) ).margin( 1e-6 ) );

  // Self-shadowed denominator (cosi + c <= 0) → NaN.
  REQUIRE( std::isnan( correctPixel( Method::CCorrection, value, -0.3, cFit ) ) );

  // Flat regression (b ≈ 0) is unusable — the operator must refuse.
  OlsRegression flat;
  flat.add( 0.3, 1.0 );
  flat.add( 0.6, 1.0 );
  REQUIRE_FALSE( fitBand( Method::CCorrection, zen, flat, MinnaertRegression{} ).usable );

  // Minnaert: k = 1 reproduces the cosine form; domain violations → NaN.
  MinnaertRegression one;
  for ( double ci : { 0.25, 0.5, 0.75 } )
    one.add( ci, 0.4 * ci ); // L = 0.4 * cosi^1 ⇒ k = 1
  BandFit mFit = fitBand( Method::Minnaert, zen, OlsRegression{}, one );
  REQUIRE( mFit.usable );
  REQUIRE( mFit.k == Catch::Approx( 1.0 ).margin( 1e-9 ) );
  REQUIRE( correctPixel( Method::Minnaert, 0.4f, 0.5, mFit ) ==
           Catch::Approx( 0.4 * cosd( 30.0 ) / 0.5 ).margin( 1e-6 ) );
  REQUIRE( std::isnan( correctPixel( Method::Minnaert, 0.4f, -0.5, mFit ) ) );
  REQUIRE( std::isnan( correctPixel( Method::Minnaert, -0.4f, 0.5, mFit ) ) );

  // Cosine self-shadow → NaN; non-finite input passes through as NaN.
  REQUIRE( std::isnan( correctPixel( Method::Cosine, 0.5f, 0.0, cosFit ) ) );
  REQUIRE( std::isnan( correctPixel( Method::Cosine, kNaN, 0.5, cosFit ) ) );

  // Method token round-trip.
  Method parsed = Method::Cosine;
  REQUIRE( parseMethod( "c_correction", &parsed ) );
  REQUIRE( parsed == Method::CCorrection );
  REQUIRE_FALSE( parseMethod( "nope", &parsed ) );
}

TEST_CASE( "rs:topographic_correction E2E: known-answer C-correction over a varying-slope DEM",
           "[topo][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString demPath = dir.filePath( "dem.tif" );
  const QString reflPath = dir.filePath( "refl.tif" );
  const QString outPath = dir.filePath( "corrected.tif" );

  // 24×18 DEM: z = 0.5·k·x² with k = 0.05 → dzdx = 0.05·x varies per column,
  // dzdy = 0 (aspect 270° everywhere); illumination varies with x.
  constexpr int kW = 24;
  constexpr int kH = 18;
  constexpr double kK = 0.05;
  RsSyntheticRasterBuilder demBuilder( kW, kH, 1 );
  demBuilder.withCrs( "EPSG:32650" );
  demBuilder.withGeoTransform( 0.0, 30.0, 540.0, -30.0 ); // 30 m cells
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
      demBuilder.withPixel( 1, x, y, static_cast<float>( 0.5 * kK * x * x ) );
  REQUIRE( !demBuilder.writeToDisk( demPath ).isEmpty() );

  // Ground-truth illumination: replicate-filled 3×3 Horn gradients under the
  // operator's cell-size contract (metres; pixels are 30 m) — computed with
  // the pure primitives so the scene satisfies L = a + b·cos_i exactly under
  // the operator's illumination definition. (The primitives' math is pinned
  // separately by the closed-form cases above.)
  const double zen = 30.0;
  const double az = 90.0;
  std::vector<double> truthCosi( static_cast<size_t>( kW ) * kH, 0.0 );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
    {
      float k9[9];
      for ( int dy = -1; dy <= 1; ++dy )
        for ( int dx = -1; dx <= 1; ++dx )
        {
          const int xx = std::min( kW - 1, std::max( 0, x + dx ) );
          const int yy = std::min( kH - 1, std::max( 0, y + dy ) );
          k9[( dy + 1 ) * 3 + ( dx + 1 )] =
            static_cast<float>( 0.5 * kK * xx * xx ); // DEM surface values
        }
      double dzdx = 0.0;
      double dzdy = 0.0;
      hornGradient( k9, 30.0, 30.0, &dzdx, &dzdy );
      double slopeDeg = 0.0;
      double aspectDeg = 0.0;
      slopeAspectDeg( dzdx, dzdy, &slopeDeg, &aspectDeg );
      truthCosi[static_cast<size_t>( y ) * kW + x] = illuminationCosine( slopeDeg, aspectDeg, zen, az );
    }

  // Scene generated exactly as L = 0.1 + 0.5·cosi (with a NoData hole) — the
  // C-correction must map every valid pixel to a + b·cosθz = 0.1 + 0.5·cos30°.
  RsSyntheticRasterBuilder reflBuilder( kW, kH, 1 );
  reflBuilder.withCrs( "EPSG:32650" );
  reflBuilder.withGeoTransform( 0.0, 30.0, 540.0, -30.0 );
  reflBuilder.withNoData( -9999.0 );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
    {
      const float v = static_cast<float>( 0.1 + 0.5 * truthCosi[static_cast<size_t>( y ) * kW + x] );
      reflBuilder.withPixel( 1, x, y, v );
    }
  reflBuilder.withPixel( 1, 5, 5, -9999.0f ); // declared sentinel → NaN NoData out
  REQUIRE( !reflBuilder.writeToDisk( reflPath ).isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:topographic_correction" );
  REQUIRE( op != nullptr );

  Json::Value params( Json::objectValue );
  params["input"] = reflPath.toStdString();
  params["dem"] = demPath.toStdString();
  params["output"] = outPath.toStdString();
  params["method"] = "c_correction";
  params["solar_zenith"] = zen;
  params["solar_azimuth"] = az;

  RSOperatorContext context;
  Json::Value result;
  REQUIRE_NOTHROW( result = op->run( params, context ) );
  REQUIRE( result["output"].asString() == outPath.toStdString() );
  REQUIRE( result["bandCount"].asInt() == 1 );

  // The fit must recover the generating line exactly.
  const Json::Value &fit = result["fit"]["band_1"];
  REQUIRE( fit["usable"].asBool() );
  REQUIRE( fit["a"].asDouble() == Catch::Approx( 0.1 ).margin( 1e-6 ) );
  REQUIRE( fit["b"].asDouble() == Catch::Approx( 0.5 ).margin( 1e-6 ) );

  GdalDatasetWrapper outDs;
  REQUIRE( outDs.open( outPath ) );
  REQUIRE( outDs.bandCount() == 1 );
  std::vector<float> pixels( static_cast<size_t>( kW ) * kH );
  REQUIRE( outDs.readBandWindow( 1, 0, 0, kW, kH, pixels.data() ) );

  const float expected = static_cast<float>( 0.1 + 0.5 * cosd( 30.0 ) );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
    {
      const float v = pixels[static_cast<size_t>( y ) * kW + x];
      if ( x == 5 && y == 5 )
        REQUIRE( std::isnan( v ) ); // sentinel pixel → NaN NoData
      else
        REQUIRE( v == Catch::Approx( expected ).margin( 2e-4f ) );
    }
}

TEST_CASE( "rs:topographic_correction refuses mismatched grids and bad geometry",
           "[topo][operator][contract]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  RsSyntheticRasterBuilder refl( 8, 6, 1 );
  refl.withCrs( "EPSG:4326" );
  refl.withGeoTransform( 0.0, 30.0, 180.0, -30.0 );
  refl.withConstantValue( 1, 0.4f );
  const QString reflPath = refl.writeToDisk( dir.filePath( "refl.tif" ) );
  REQUIRE( !reflPath.isEmpty() );

  // Different size → blocking grid mismatch.
  RsSyntheticRasterBuilder dem( 9, 6, 1 );
  dem.withCrs( "EPSG:4326" );
  dem.withGeoTransform( 0.0, 30.0, 180.0, -30.0 );
  dem.withConstantValue( 1, 100.0f );
  const QString demPath = dem.writeToDisk( dir.filePath( "dem.tif" ) );
  REQUIRE( !demPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:topographic_correction" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["input"] = reflPath.toStdString();
  params["dem"] = demPath.toStdString();
  params["output"] = dir.filePath( "out.tif" ).toStdString();
  params["method"] = "c_correction";
  params["solar_zenith"] = 30.0;
  params["solar_azimuth"] = 90.0;

  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );

  // Out-of-range sun geometry is a typed parameter refusal.
  RsSyntheticRasterBuilder demSame( 8, 6, 1 );
  demSame.withCrs( "EPSG:4326" );
  demSame.withGeoTransform( 0.0, 30.0, 180.0, -30.0 );
  demSame.withConstantValue( 1, 100.0f );
  const QString demSamePath = demSame.writeToDisk( dir.filePath( "dem_same.tif" ) );
  params["dem"] = demSamePath.toStdString();
  params["solar_zenith"] = 95.0;
  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
