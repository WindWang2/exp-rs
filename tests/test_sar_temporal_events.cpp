// tests/test_sar_temporal_events.cpp — multi-temporal SAR event dating
// (Advanced SAR / PolSAR / InSAR 10.0, package D; closes ISSUES.md S-1).
//
// Exact anchors: ISO-8601 UTC grammar; day-offset arithmetic across
// irregular revisit intervals; hand-computed event kernels (threshold
// crossing, first/last, argmax days) over linear-power series with missing
// acquisitions; refusal semantics.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/sar/sar_temporal.h"
#include "processing/algorithms/sar/sar_temporal_events.h"

using namespace sicnu::sar;
using Catch::Approx;

TEST_CASE( "acquisition time grammar", "[sar][temporal-events]" )
{
    double seconds = 0.0;
    QString error;

    SECTION( "date only" )
    {
        REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-01-01" ), &seconds, &error ) );
        // 2026-01-01T00:00:00Z = 1767225600.
        REQUIRE( seconds == Approx( 1767225600.0 ) );
    }

    SECTION( "full stamp with fractional seconds" )
    {
        REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-03-05T06:30:15.250Z" ), &seconds,
                                      &error ) );
        // 2026-03-05 is day 64 of 2026 → 63 days after 2026-01-01
        // (1767225600): + 63·86400 = 1772668800.
        REQUIRE( seconds == Approx( 1772668800.0 + 6 * 3600 + 30 * 60 + 15.25 ) );
    }

    SECTION( "minutes precision with optional Z" )
    {
        REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-03-05T06:30Z" ), &seconds, &error ) );
        REQUIRE( seconds == Approx( 1772668800.0 + 6 * 3600 + 30 * 60 ) );
        REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-03-05T06:30" ), &seconds, &error ) );
        REQUIRE( seconds == Approx( 1772668800.0 + 6 * 3600 + 30 * 60 ) );
    }

    SECTION( "refusals" )
    {
        REQUIRE_FALSE( parseAcquisitionUtc( QStringLiteral( "not-a-date" ), &seconds, &error ) );
        REQUIRE_FALSE( parseAcquisitionUtc( QStringLiteral( "2026-13-40" ), &seconds, &error ) );
        REQUIRE_FALSE( parseAcquisitionUtc( QStringLiteral( "2026-03-05T25:00" ), &seconds ) );
        REQUIRE_FALSE( parseAcquisitionUtc( QStringLiteral( "2026-03-05T10:00+01:00" ),
                                            &seconds, &error ) );
        REQUIRE( error.contains( QLatin1String( "UTC" ) ) );
    }
}

TEST_CASE( "day offsets over irregular revisit intervals", "[sar][temporal-events]" )
{
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-01-01" ), &s0 ) );
    REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-01-13" ), &s1 ) ); // 12 days later
    REQUIRE( parseAcquisitionUtc( QStringLiteral( "2026-01-16T12:00:00Z" ), &s2 ) ); // 15.5 days
    REQUIRE( s1 - s0 == Approx( 12.0 * 86400.0 ).margin( 1e-6 ) );
    REQUIRE( daysSince( s1, s0 ) == Approx( 12.0 ) );
    REQUIRE( daysSince( s2, s0 ) == Approx( 15.5 ) );
}

TEST_CASE( "event kernel — hand-computed series", "[sar][temporal-events]" )
{
    // Series (linear power): baseline 1, a 10 dB event (×10) at scene 2.
    // [1, 1, 10, 1, 1] — median = 1 (upper median of 5).
    const std::vector<double> values = { 1.0, 1.0, 10.0, 1.0, 1.0 };
    // Irregular revisit: days [0, 12, 15.5, 27.5, 40].
    const std::vector<double> days = { 0.0, 12.0, 15.5, 27.5, 40.0 };

    TemporalEventResult r;
    REQUIRE( sarTemporalEvents( values.data(), days.data(), 5, 6.0, &r ) );
    REQUIRE( r.validCount == 5 );
    REQUIRE( r.baselineDb == Approx( 0.0 ).margin( 1e-12 ) );
    // Deviations: [0, 0, 10, 0, 0] dB.
    REQUIRE( r.maxDeviationDb == Approx( 10.0 ).margin( 1e-12 ) );
    REQUIRE( r.event );
    REQUIRE( r.eventCount == 1 );
    REQUIRE( r.firstEventIndex == 2 );
    REQUIRE( r.lastEventIndex == 2 );
    REQUIRE( r.firstEventDays == Approx( 15.5 ) );
    REQUIRE( r.lastEventDays == Approx( 15.5 ) );
    REQUIRE( r.argmaxIndex == 2 );
    REQUIRE( r.argmaxDays == Approx( 15.5 ) );

    SECTION( "lowering the threshold catches everything but keeps dates" )
    {
        // Threshold 0: every valid date is an event.
        REQUIRE( sarTemporalEvents( values.data(), days.data(), 5, 0.0, &r ) );
        REQUIRE( r.eventCount == 5 );
        REQUIRE( r.firstEventDays == Approx( 0.0 ) );
        REQUIRE( r.lastEventDays == Approx( 40.0 ) );
    }

    SECTION( "raising the threshold: no events" )
    {
        REQUIRE( sarTemporalEvents( values.data(), days.data(), 5, 11.0, &r ) );
        REQUIRE_FALSE( r.event );
        REQUIRE( r.eventCount == 0 );
        REQUIRE( r.firstEventIndex == -1 );
        REQUIRE( std::isnan( r.firstEventDays ) );
        REQUIRE( r.argmaxDays == Approx( 15.5 ) ); // argmax dating unaffected
    }

    SECTION( "missing acquisition drops out without shifting dates" )
    {
        // Scene 2 (the event) is missing (NaN); deviation of the rest vs the
        // new median (still 1) is 0 → no events; argmax keeps the FIRST max
        // occurrence (scene 0, day 0 — dates never shift with holes).
        const std::vector<double> withHole = { 1.0, 1.0,
                                               std::numeric_limits<double>::quiet_NaN(),
                                               1.0, 1.0 };
        REQUIRE( sarTemporalEvents( withHole.data(), days.data(), 5, 6.0, &r ) );
        REQUIRE( r.validCount == 4 );
        REQUIRE_FALSE( r.event );
        REQUIRE( r.argmaxIndex == 0 );
        REQUIRE( r.argmaxDays == Approx( 0.0 ) );
    }

    SECTION( "nonpositive power is invalid (domain rule)" )
    {
        const std::vector<double> zeros = { 1.0, 0.0, 10.0, 1.0, 1.0 };
        REQUIRE( sarTemporalEvents( zeros.data(), days.data(), 5, 6.0, &r ) );
        REQUIRE( r.validCount == 4 );
        REQUIRE( r.eventCount == 1 );
    }
}

TEST_CASE( "event kernel refusals", "[sar][temporal-events]" )
{
    TemporalEventResult r;
    const std::vector<double> values = { 1.0, 2.0 };
    const std::vector<double> days = { 0.0, 1.0 };

    REQUIRE_FALSE( sarTemporalEvents( nullptr, days.data(), 2, 6.0, &r ) );
    REQUIRE_FALSE( sarTemporalEvents( values.data(), days.data(), 0, 6.0, &r ) );
    REQUIRE_FALSE( sarTemporalEvents( values.data(), days.data(), 2, -1.0, &r ) );

    const std::vector<double> invalid = { std::numeric_limits<double>::quiet_NaN(),
                                          std::numeric_limits<double>::quiet_NaN() };
    REQUIRE_FALSE( sarTemporalEvents( invalid.data(), days.data(), 2, 6.0, &r ) );
}

TEST_CASE( "shared upper-median convention with sar_temporal", "[sar][temporal-events]" )
{
    std::vector<double> samples = { 1.0, 2.0, 3.0, 4.0 };
    // Even count: upper median = 3 (the n/2-th element, not interpolated).
    REQUIRE( sarUpperMedianLinear( samples ) == Approx( 3.0 ) );
}
