// tests/test_pixel_ignore_options.cpp — NoData / ignore-value edge rules
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>

#include "rs_pixel_ignore_options.h"

TEST_CASE( "PixelIgnore: parse ignore values CSV", "[classify][ignore]" )
{
  RsPixelIgnoreOptions opt;
  opt.setIgnoreValuesFromText( QStringLiteral( "0, -9999, 255.5" ) );
  REQUIRE( opt.ignoreValues.size() == 3 );
  REQUIRE( opt.ignoreValues[0] == 0.0 );
  REQUIRE( opt.ignoreValues[1] == -9999.0 );
  REQUIRE( opt.ignoreValues[2] == 255.5 );
  REQUIRE( opt.ignoreValuesText().contains( QLatin1String( "0" ) ) );
  REQUIRE( opt.ignoreValuesText().contains( QLatin1String( "-9999" ) ) );
}

TEST_CASE( "PixelIgnore: scalar NoData and user ignore", "[classify][ignore]" )
{
  RsPixelIgnoreOptions opt;
  opt.useSourceNodata = true;
  opt.ignoreValues = { 0.0 };

  REQUIRE( opt.isIgnoreScalar( 0.0, false, 0.0 ) );
  REQUIRE( opt.isIgnoreScalar( -9999.0, true, -9999.0 ) );
  REQUIRE_FALSE( opt.isIgnoreScalar( 100.0, true, -9999.0 ) );
  REQUIRE( opt.isIgnoreScalar( std::numeric_limits<double>::quiet_NaN(), false, 0.0 ) );

  opt.useSourceNodata = false;
  REQUIRE_FALSE( opt.isIgnoreScalar( -9999.0, true, -9999.0 ) );
}

TEST_CASE( "PixelIgnore: AnyBand vs AllBands", "[classify][ignore]" )
{
  RsPixelIgnoreOptions opt;
  opt.useSourceNodata = false;
  opt.ignoreNaN = false;
  opt.ignoreValues = { 0.0 };
  opt.mode = RsPixelIgnoreOptions::Mode::AnyBand;

  const float anyBand[] = { 10.f, 0.f, 20.f };
  const float allBand[] = { 0.f, 0.f, 0.f };
  const float noneBand[] = { 1.f, 2.f, 3.f };
  std::vector<bool> hasNd( 3, false );
  std::vector<float> nd( 3, 0.f );

  REQUIRE( opt.isIgnorePixel( anyBand, 3, hasNd, nd ) );
  REQUIRE( opt.isIgnorePixel( allBand, 3, hasNd, nd ) );
  REQUIRE_FALSE( opt.isIgnorePixel( noneBand, 3, hasNd, nd ) );

  opt.mode = RsPixelIgnoreOptions::Mode::AllBands;
  REQUIRE_FALSE( opt.isIgnorePixel( anyBand, 3, hasNd, nd ) );
  REQUIRE( opt.isIgnorePixel( allBand, 3, hasNd, nd ) );
  REQUIRE_FALSE( opt.isIgnorePixel( noneBand, 3, hasNd, nd ) );
}

TEST_CASE( "PixelIgnore: non-finite +/-Inf is ignored like NaN (Inf governance, R10-A/AN-1)", "[classify][ignore][r10a]" )
{
  // Same !isfinite discipline as rs_multires_segmenter / rs_feature_scaler:
  // a +/-Inf sample (band-math divide-by-zero residue) must never reach
  // training and must never be classified as a real class. Default options
  // (ignoreNaN = true) govern — Config.fitScaler is false by default, so
  // this check is the only finite gate on the default path.
  RsPixelIgnoreOptions opt;
  REQUIRE( opt.ignoreNaN );
  REQUIRE( opt.isIgnoreScalar( std::numeric_limits<double>::infinity(), false, 0.0 ) );
  REQUIRE( opt.isIgnoreScalar( -std::numeric_limits<double>::infinity(), false, 0.0 ) );
  REQUIRE_FALSE( opt.isIgnoreScalar( 100.0, false, 0.0 ) );

  // Pixel-level form must agree for a multi-band pixel carrying an Inf band.
  const float infBand[] = { std::numeric_limits<float>::infinity(), 1.f };
  std::vector<bool> hasNd( 2, false );
  std::vector<float> nd( 2, 0.f );
  REQUIRE( opt.isIgnorePixel( infBand, 2, hasNd, nd ) );
}
