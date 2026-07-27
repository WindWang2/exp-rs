// test_display_stretch.cpp — pure resolve + port apply (no GUI)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/display/display_stretch.h"

using namespace rs::display;
using Catch::Approx;

TEST_CASE( "StretchSpec factories", "[display_stretch]" )
{
  auto p = StretchSpec::percentClip( 2.0 );
  CHECK( p.kind() == StretchKind::PercentClip );
  CHECK( *p.clipPercent() == Approx( 2.0 ) );

  auto s = StretchSpec::stdDev( 2.0 ).withStatsBand( 3 );
  CHECK( s.kind() == StretchKind::StdDev );
  CHECK( *s.statsBand() == 3 );
}

TEST_CASE( "validate rejects bad piecewise", "[display_stretch]" )
{
  auto err = validate( StretchSpec::piecewise( { ControlPoint{ 0, 0 } } ) );
  REQUIRE( err.has_value() );
  CHECK( err->code == StretchErrorCode::InvalidSpec );
}

TEST_CASE( "resolve percent clip", "[display_stretch]" )
{
  BandStats stats;
  stats.min = 0;
  stats.max = 1000;
  stats.hasMinMax = true;

  auto r = resolve( StretchSpec::percentClip( 2.0 ), stats );
  REQUIRE( r );
  // 2% total → 1% each side of 1000
  CHECK( r.value().displayMin == Approx( 10.0 ) );
  CHECK( r.value().displayMax == Approx( 990.0 ) );
  CHECK_FALSE( r.value().approximated );
}

TEST_CASE( "resolve stddev", "[display_stretch]" )
{
  BandStats stats;
  stats.min = 0;
  stats.max = 100;
  stats.mean = 50;
  stats.stdDev = 10;
  stats.hasMinMax = true;
  stats.hasMeanStd = true;

  auto r = resolve( StretchSpec::stdDev( 2.0 ), stats );
  REQUIRE( r );
  CHECK( r.value().displayMin == Approx( 30.0 ) );
  CHECK( r.value().displayMax == Approx( 70.0 ) );
}

TEST_CASE( "resolve levels and force strict range", "[display_stretch]" )
{
  BandStats stats;
  stats.hasMinMax = true;
  stats.min = 0;
  stats.max = 255;

  auto r = resolve( StretchSpec::levels( 10, 10, 1.0 ), stats );
  REQUIRE( r );
  CHECK( r.value().displayMax > r.value().displayMin );
}

TEST_CASE( "resolve histogram eq uses the real data range", "[display_stretch]" )
{
  BandStats stats;
  stats.min = 5;
  stats.max = 95;
  stats.hasMinMax = true;

  auto r = resolve( StretchSpec::histogramEqualize(), stats );
  REQUIRE( r );
  CHECK_FALSE( r.value().approximated );
  CHECK( r.value().displayMin == Approx( 5.0 ) );
  CHECK( r.value().displayMax == Approx( 95.0 ) );
}

TEST_CASE( "resolve piecewise endpoints", "[display_stretch]" )
{
  std::vector<ControlPoint> pts = {
    { 100, 0 },
    { 200, 128 },
    { 400, 255 }
  };
  auto r = resolve( StretchSpec::piecewise( pts ), BandStats{} );
  REQUIRE( r );
  CHECK( r.value().displayMin == Approx( 100.0 ) );
  CHECK( r.value().displayMax == Approx( 400.0 ) );
  CHECK_FALSE( r.value().approximated ); // full curve applied at CE layer
  CHECK( r.value().transferCurve.size() == 3 );
}

TEST_CASE( "piecewise transfer curve mid-point", "[display_stretch]" )
{
  // Mid control point must survive resolve for apply path
  std::vector<ControlPoint> pts = {
    { 0, 0 },
    { 50, 200 },
    { 100, 255 }
  };
  auto r = resolve( StretchSpec::piecewise( pts ), BandStats{} );
  REQUIRE( r );
  REQUIRE( r.value().transferCurve.size() == 3 );
  CHECK( r.value().transferCurve[1].x == Approx( 50.0 ) );
  CHECK( r.value().transferCurve[1].y == Approx( 200.0 ) );
}

TEST_CASE( "apply via recording target", "[display_stretch]" )
{
  RecordingDisplayTarget target;
  target.cannedInfo.valid = true;
  target.cannedInfo.renderer = DisplayTargetInfo::RendererKind::MultiBandColor;
  target.cannedInfo.bandCount = 3;

  RecordingBandStats stats;
  stats.canned.min = 0;
  stats.canned.max = 1000;
  stats.canned.hasMinMax = true;

  int cookie = 1;
  auto result = apply( target, stats, &cookie, StretchSpec::percentClip( 2.0 ), 1 );
  REQUIRE( result );
  CHECK( result.value().applied.displayMin == Approx( 10.0 ) );
  CHECK( result.value().applied.displayMax == Approx( 990.0 ) );

  REQUIRE( target.calls.size() >= 2 );
  CHECK( target.calls.back().op == RecordingDisplayTarget::Call::Op::Apply );
  CHECK( stats.lastBand == 1 );
}

TEST_CASE( "apply null layer is LayerGone", "[display_stretch]" )
{
  RecordingDisplayTarget target;
  target.cannedInfo.valid = true;
  target.cannedInfo.renderer = DisplayTargetInfo::RendererKind::SingleBandGray;

  RecordingBandStats stats;
  stats.canned.hasMinMax = true;
  stats.canned.min = 0;
  stats.canned.max = 1;

  auto result = apply( target, stats, nullptr, StretchSpec::linearMinMax( 0, 255 ) );
  REQUIRE_FALSE( result );
  CHECK( result.error().code == StretchErrorCode::LayerGone );
}

TEST_CASE( "apply unsupported renderer", "[display_stretch]" )
{
  RecordingDisplayTarget target;
  target.cannedInfo.valid = true;
  target.cannedInfo.renderer = DisplayTargetInfo::RendererKind::Unsupported;

  RecordingBandStats stats;
  stats.canned.hasMinMax = true;
  stats.canned.min = 0;
  stats.canned.max = 255;

  int cookie = 1;
  auto result = apply( target, stats, &cookie, StretchSpec::realDataRange() );
  REQUIRE_FALSE( result );
  CHECK( result.error().code == StretchErrorCode::UnsupportedRenderer );
}

TEST_CASE( "invalid piecewise never calls apply", "[display_stretch]" )
{
  RecordingDisplayTarget target;
  target.cannedInfo.valid = true;
  target.cannedInfo.renderer = DisplayTargetInfo::RendererKind::SingleBandGray;

  RecordingBandStats stats;
  int cookie = 1;
  auto result = apply( target, stats, &cookie,
                       StretchSpec::piecewise( { ControlPoint{ 0, 0 } } ) );
  REQUIRE_FALSE( result );
  CHECK( result.error().code == StretchErrorCode::InvalidSpec );
  // validate fails before inspect — no apply
  CHECK( target.calls.empty() );
}
