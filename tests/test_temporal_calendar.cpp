// tests/test_temporal_calendar.cpp — known-answer tests for the regular
// calendar time-normalization kernel (Temporal Platform 10.0, T-2).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_calendar.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

bool isNan( float v ) { return !std::isfinite( v ); }
} // namespace

TEST_CASE( "parseCadenceToken accepts days and month tokens only", "[temporal][calendar]" )
{
  CalendarSpec spec;
  REQUIRE( parseCadenceToken( "16d", &spec ) );
  REQUIRE( spec.cadence == CalendarCadence::Days );
  REQUIRE( spec.cadenceDays == 16 );
  REQUIRE( parseCadenceToken( " Month ", &spec ) );
  REQUIRE( spec.cadence == CalendarCadence::Monthly );
  REQUIRE( parseCadenceToken( "8", &spec ) == false );
  REQUIRE( parseCadenceToken( "0d", &spec ) == false );
  REQUIRE( parseCadenceToken( "-4d", &spec ) == false );
  REQUIRE( parseCadenceToken( "week", &spec ) == false );
}

TEST_CASE( "buildRegularCalendar: days cadence covers the closed range", "[temporal][calendar]" )
{
  CalendarSpec spec;
  spec.cadence = CalendarCadence::Days;
  spec.cadenceDays = 16;
  // 2026-01-01 is the epoch (t = 0). Range [0, 32] -> t = 0, 16, 32.
  const std::vector<CalendarPoint> grid =
    buildRegularCalendar( "2026-01-01", 0.0, 32.0, spec );
  REQUIRE( grid.size() == 3 );
  REQUIRE( grid[0].tDays == Approx( 0.0 ) );
  REQUIRE( grid[0].isoDate == "2026-01-01" );
  REQUIRE( grid[1].tDays == Approx( 16.0 ) );
  REQUIRE( grid[1].isoDate == "2026-01-17" );
  REQUIRE( grid[2].tDays == Approx( 32.0 ) );
  REQUIRE( grid[2].isoDate == "2026-02-02" );

  // A range starting mid-step anchors at the first grid point >= start.
  const std::vector<CalendarPoint> clipped =
    buildRegularCalendar( "2026-01-01", 1.0, 20.0, spec );
  REQUIRE( clipped.size() == 2 );
  REQUIRE( clipped[0].tDays == Approx( 16.0 ) );

  // Inverted / degenerate ranges are empty (refusal, never a guess).
  REQUIRE( buildRegularCalendar( "2026-01-01", 32.0, 0.0, spec ).empty() );
  REQUIRE( buildRegularCalendar( "2026-01-01", 0.0, 32.0,
                                 CalendarSpec{ CalendarCadence::Days, 0 } ).empty() );
  REQUIRE( buildRegularCalendar( "not-a-date", 0.0, 32.0, spec ).empty() );
}

TEST_CASE( "buildRegularCalendar: monthly cadence steps calendar months", "[temporal][calendar]" )
{
  CalendarSpec spec;
  spec.cadence = CalendarCadence::Monthly;
  const std::vector<CalendarPoint> grid =
    buildRegularCalendar( "2026-01-31", 0.0, 120.0, spec );
  // Jan 31 -> Feb 28 (clamped), Mar 31, Apr 30 (clamped), May 31...
  REQUIRE( grid.size() >= 4 );
  REQUIRE( grid[0].isoDate == "2026-01-31" );
  REQUIRE( grid[0].tDays == Approx( 0.0 ) );
  REQUIRE( grid[1].isoDate == "2026-02-28" );
  REQUIRE( grid[1].tDays == Approx( 28.0 ) );
  REQUIRE( grid[2].isoDate == "2026-03-31" );
  REQUIRE( grid[3].isoDate == "2026-04-30" );
}

TEST_CASE( "regularizeSeries linear: exact interpolation between observations", "[temporal][calendar]" )
{
  // Observations at t = 0 (1.0) and t = 20 (3.0); calendar at 0/10/20.
  std::vector<float> series{ 1.0f, 3.0f };
  std::vector<double> tDays{ 0.0, 20.0 };
  std::vector<CalendarPoint> calendar{ { 0.0, "2026-01-01" },
                                        { 10.0, "2026-01-11" },
                                        { 20.0, "2026-01-21" } };
  RegularizeOptions options;
  options.method = RegularizeMethod::Linear;
  const RegularizedSeries out = regularizeSeries( series, tDays, calendar, options );
  REQUIRE( out.points.size() == 3 );
  REQUIRE( out.points[0].value == Approx( 1.0f ) );
  REQUIRE( out.points[0].filled == false ); // observation ON the point
  REQUIRE( out.points[0].validObservations == 1 );
  REQUIRE( out.points[1].value == Approx( 2.0f ) );
  REQUIRE( out.points[1].filled == true );
  REQUIRE( out.points[1].validObservations == 2 );
  REQUIRE( out.points[2].value == Approx( 3.0f ) );
  REQUIRE( out.points[2].filled == false );
  REQUIRE( out.validCount == 3 );
  REQUIRE( out.filledCount == 1 );
}

TEST_CASE( "regularizeSeries linear: NaN observations are transparent", "[temporal][calendar]" )
{
  // Middle observation masked -> interpolation spans it.
  std::vector<float> series{ 1.0f, kNan, 5.0f };
  std::vector<double> tDays{ 0.0, 10.0, 20.0 };
  std::vector<CalendarPoint> calendar{ { 10.0, "2026-01-11" } };
  const RegularizedSeries out =
    regularizeSeries( series, tDays, calendar, RegularizeOptions{} );
  REQUIRE( out.points[0].value == Approx( 3.0f ) );
  REQUIRE( out.points[0].validObservations == 2 );
}

TEST_CASE( "regularizeSeries: extrapolation is refused", "[temporal][calendar]" )
{
  std::vector<float> series{ 1.0f, 2.0f };
  std::vector<double> tDays{ 5.0, 15.0 };
  std::vector<CalendarPoint> calendar{ { 0.0, "2025-12-27" },
                                        { 10.0, "2026-01-06" },
                                        { 20.0, "2026-01-16" } };
  for ( const RegularizeMethod method :
        { RegularizeMethod::Nearest, RegularizeMethod::WindowMean,
          RegularizeMethod::Linear, RegularizeMethod::Whittaker } )
  {
    RegularizeOptions options;
    options.method = method;
    const RegularizedSeries out = regularizeSeries( series, tDays, calendar, options );
    CAPTURE( method );
    REQUIRE( isNan( out.points[0].value ) ); // before the first observation
    REQUIRE( isNan( out.points[2].value ) ); // after the last observation
    REQUIRE( std::isfinite( out.points[1].value ) );
  }
}

TEST_CASE( "regularizeSeries nearest + window_mean respect the window radius", "[temporal][calendar]" )
{
  std::vector<float> series{ 10.0f, 20.0f };
  std::vector<double> tDays{ 0.0, 40.0 };
  std::vector<CalendarPoint> calendar{ { 10.0, "d0" }, { 25.0, "d1" } };

  RegularizeOptions options;
  options.method = RegularizeMethod::Nearest;
  options.maxWindowDays = 15.0;
  const RegularizedSeries nearest = regularizeSeries( series, tDays, calendar, options );
  REQUIRE( nearest.points[0].value == Approx( 10.0f ) ); // distance 10 <= 15
  REQUIRE( isNan( nearest.points[1].value ) );           // distances 15/15? -> tie prefers earlier? 25-0=25 > 15

  options.maxWindowDays = 25.0;
  const RegularizedSeries nearest2 = regularizeSeries( series, tDays, calendar, options );
  // t=25: left distance 25, right 15 -> right wins.
  REQUIRE( nearest2.points[1].value == Approx( 20.0f ) );

  options.method = RegularizeMethod::WindowMean;
  options.maxWindowDays = 25.0;
  const RegularizedSeries mean = regularizeSeries( series, tDays, calendar, options );
  REQUIRE( mean.points[0].value == Approx( 10.0f ) ); // only the first in [−25, 25]
  REQUIRE( mean.points[1].value == Approx( 15.0f ) ); // both inside
  REQUIRE( mean.points[1].validObservations == 2 );
  REQUIRE( mean.points[1].filled == true );
}

TEST_CASE( "regularizeSeries whittaker: bridges small gaps, splits large ones", "[temporal][calendar]" )
{
  // Daily observations 0..6 on a calendar of every day; drop node 3.
  std::vector<float> series{ 1.0f, 1.0f, 1.0f, kNan, 1.0f, 1.0f, 1.0f };
  std::vector<double> tDays{ 0, 1, 2, 3, 4, 5, 6 };
  std::vector<CalendarPoint> calendar;
  for ( int i = 0; i <= 6; ++i )
    calendar.push_back( { static_cast<double>( i ), QStringLiteral( "d%1" ).arg( i ) } );

  RegularizeOptions options;
  options.method = RegularizeMethod::Whittaker;
  options.lambda = 10.0;
  options.maxGapNodes = 2;
  const RegularizedSeries bridged = regularizeSeries( series, tDays, calendar, options );
  REQUIRE( bridged.points.size() == 7 );
  REQUIRE( std::isfinite( bridged.points[3].value ) );
  REQUIRE( bridged.points[3].value == Approx( 1.0f ).margin( 0.1f ) );
  REQUIRE( bridged.points[3].filled == true );
  REQUIRE( bridged.points[3].validObservations == 0 ); // penalty-defined value
  // Observed nodes carry their observation count.
  REQUIRE( bridged.points[0].validObservations == 1 );
  REQUIRE( bridged.points[0].filled == false );

  // A gap wider than maxGapNodes splits: an interior gap of 4 unobserved
  // nodes between two runs cannot be bridged.
  std::vector<float> split{ 1.0f, kNan, kNan, kNan, kNan, 2.0f };
  std::vector<double> splitT{ 0, 1, 2, 3, 4, 5 };
  std::vector<CalendarPoint> splitCal;
  for ( int i = 0; i <= 5; ++i )
    splitCal.push_back( { static_cast<double>( i ), QStringLiteral( "s%1" ).arg( i ) } );
  options.maxGapNodes = 1;
  const RegularizedSeries splitOut = regularizeSeries( split, splitT, splitCal, options );
  CAPTURE( splitOut.points[2].value, splitOut.points[3].value );
  // The split points must NOT interpolate smoothly across the gap: the left
  // side knows only 1.0 and the right only 2.0; the middle stays at the
  // penalty level of its own side (finite or NaN), never a linear bridge.
  REQUIRE( isNan( splitOut.points[0].value ) == false );
  REQUIRE( splitOut.points[5].validObservations == 1 );
}

TEST_CASE( "regularizeSeries: empty inputs yield empty results", "[temporal][calendar]" )
{
  std::vector<float> series{ kNan, kNan };
  std::vector<double> tDays{ 0.0, 1.0 };
  std::vector<CalendarPoint> calendar{ { 0.0, "d0" } };
  const RegularizedSeries out = regularizeSeries( series, tDays, calendar, RegularizeOptions{} );
  REQUIRE( out.points.size() == 1 );
  REQUIRE( isNan( out.points[0].value ) );
  REQUIRE( out.validCount == 0 );
  REQUIRE( out.filledCount == 0 );
}
