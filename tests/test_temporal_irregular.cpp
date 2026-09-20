// tests/test_temporal_irregular.cpp — Temporal Phenology 12.0 (WP2) numeric
// references: day-axis smoothing kernels (movingAverageDays,
// savitzkyGolayDays, whittakerSmoothTime[Robust]) and gap-fill provenance.
//
// Oracle independence: expectations come from analytic identities
// (polynomial reproduction, unit-spacing equivalence with the existing
// position-based Whittaker, hand-computed window means), never from the
// implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QDate>

#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_fusion.h"
#include "processing/algorithms/temporal/temporal_gapfill.h"
#include "processing/algorithms/temporal/temporal_irregular.h"
#include "processing/algorithms/temporal/temporal_stream.h"

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace sicnu::temporal;

namespace
{
int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_irregular";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

bool allNan( const std::vector<float> &v )
{
    for ( float f : v )
        if ( std::isfinite( f ) )
            return false;
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// gapFillProvenance
// ---------------------------------------------------------------------------

TEST_CASE( "gapFillProvenance labels observed, interpolated and unavailable",
           "[temporal][irregular][provenance]" )
{
    ensureApp();
    const std::vector<float> in  = { 1.0f, kNan, 3.0f, kNan, kNan };
    const std::vector<float> out = { 1.0f, 2.0f, 3.0f, kNan, kNan };
    const auto prov = gapFillProvenance( in, out );
    REQUIRE( prov.size() == in.size() );
    REQUIRE( prov[0] == static_cast<std::uint8_t>( SampleProvenance::Observed ) );
    REQUIRE( prov[1] == static_cast<std::uint8_t>( SampleProvenance::Interpolated ) );
    REQUIRE( prov[2] == static_cast<std::uint8_t>( SampleProvenance::Observed ) );
    REQUIRE( prov[3] == static_cast<std::uint8_t>( SampleProvenance::Unavailable ) );
    REQUIRE( prov[4] == static_cast<std::uint8_t>( SampleProvenance::Unavailable ) );

    // Real kernel pair: linear fill inside a short gap.
    const std::vector<float> series = { 10.0f, kNan, kNan, 40.0f, kNan, 60.0f };
    const std::vector<double> tDays = { 0, 10, 20, 30, 40, 50 };
    GapFillCounts counts;
    std::vector<float> filled( series.size(), kNan );
    gapFillSeries( series.data(), static_cast<int>( series.size() ),
                   tDays.data(), GapFillMethod::Linear, 45.0,
                   filled.data(), &counts );
    const auto prov2 = gapFillProvenance( series, filled );
    REQUIRE( counts.fillable == 3 );
    REQUIRE( counts.filled == 3 );
    REQUIRE( prov2[1] == static_cast<std::uint8_t>( SampleProvenance::Interpolated ) );
    REQUIRE( prov2[2] == static_cast<std::uint8_t>( SampleProvenance::Interpolated ) );
    REQUIRE( prov2[4] == static_cast<std::uint8_t>( SampleProvenance::Interpolated ) );
    // Linear fill: 10→40 over 30 days gives 20, 30; 40→60 over 20 gives 50.
    REQUIRE( filled[1] == Approx( 20.0f ).margin( 1e-4 ) );
    REQUIRE( filled[4] == Approx( 50.0f ).margin( 1e-4 ) );

    // Unfillable gap (beyond maxGapDays): stays Unavailable, value NaN.
    const auto unfilled = gapFillSeries( series, tDays, GapFillMethod::Linear, 15.0 );
    REQUIRE( unfilled[1] != unfilled[1] ); // NaN
    const auto prov3 = gapFillProvenance( series, unfilled );
    REQUIRE( prov3[1] == static_cast<std::uint8_t>( SampleProvenance::Unavailable ) );

    // Size mismatch → empty (typed refusal, no partial answer).
    REQUIRE( gapFillProvenance( in, { 1.0f } ).empty() );
}

// ---------------------------------------------------------------------------
// movingAverageDays
// ---------------------------------------------------------------------------

TEST_CASE( "movingAverageDays windows by days, not positions",
           "[temporal][irregular]" )
{
    ensureApp();
    // Irregular axis: samples at 0, 5, 6, 40, 90 days.
    const std::vector<float> y = { 10.f, 20.f, 30.f, 100.f, 200.f };
    const std::vector<double> t = { 0, 5, 6, 40, 90 };

    // windowDays = 13 (half = 6.5): centered day windows —
    //   t=0: [−6.5,6.5] → {0,5,6} → 20
    //   t=5: [−1.5,11.5] → {0,5,6} → 20
    //   t=6: [−0.5,12.5] → {0,5,6} → 20
    //   t=40: [33.5,46.5] → {40} → 100
    //   t=90: [83.5,96.5] → {90} → 200
    // The position-based mean would treat {0,5,6,40,90} as equally spaced.
    const auto sm = movingAverageDays( y, t, 13.0 );
    REQUIRE( sm.size() == y.size() );
    REQUIRE( sm[0] == Approx( 20.0f ).margin( 1e-5 ) );  // (10+20+30)/3
    REQUIRE( sm[1] == Approx( 20.0f ).margin( 1e-5 ) );
    REQUIRE( sm[2] == Approx( 20.0f ).margin( 1e-5 ) );
    REQUIRE( sm[3] == Approx( 100.0f ).margin( 1e-5 ) );
    REQUIRE( sm[4] == Approx( 200.0f ).margin( 1e-5 ) );

    // windowDays = 6.0 → {0,5}, {5,6}, {5,6}, {40}, {90}
    //   i=0: t∈[−3,3] → {10}        → 10
    //   i=1: t∈[2,8] → {20,30}      → 25
    const auto sm6 = movingAverageDays( y, t, 6.0 );
    REQUIRE( sm6[0] == Approx( 10.0f ).margin( 1e-5 ) );
    REQUIRE( sm6[1] == Approx( 25.0f ).margin( 1e-5 ) );

    // Uniform daily axis equals a centered position-window mean.
    const std::vector<float> daily = { 1, 2, 3, 4, 5 };
    const std::vector<double> dt = { 0, 1, 2, 3, 4 };
    const auto smU = movingAverageDays( daily, dt, 3.0 );
    REQUIRE( smU[1] == Approx( 2.0f ).margin( 1e-5 ) );
    REQUIRE( smU[2] == Approx( 3.0f ).margin( 1e-5 ) );
}

TEST_CASE( "movingAverageDays skips NaN samples and refuses invalid input",
           "[temporal][irregular]" )
{
    ensureApp();
    const std::vector<float> y = { 10.f, kNan, 30.f };
    const std::vector<double> t = { 0, 5, 10 };
    // windowDays = 21 (half = 10.5): every window covers all three instants;
    // the NaN sample is excluded → mean of {10, 30} = 20 at every position.
    const auto sm = movingAverageDays( y, t, 21.0 );
    for ( float v : sm )
        REQUIRE( v == Approx( 20.0f ).margin( 1e-5 ) );

    // Position with an empty finite window → NaN (gap not fabricated).
    const std::vector<float> y2 = { kNan, kNan, kNan };
    const auto sm2 = movingAverageDays( y2, t, 21.0 );
    REQUIRE( allNan( sm2 ) );

    // Refusals: duplicate instant, non-finite tDays, non-positive window,
    // size mismatch.
    const std::vector<double> dup = { 0, 5, 5 };
    REQUIRE( allNan( movingAverageDays( y, dup, 21.0 ) ) );
    const std::vector<double> bad = { 0, 5, std::numeric_limits<double>::quiet_NaN() };
    REQUIRE( allNan( movingAverageDays( y, bad, 21.0 ) ) );
    REQUIRE( allNan( movingAverageDays( y, t, 0.0 ) ) );
    REQUIRE( allNan( movingAverageDays( y, t, -3.0 ) ) );
    REQUIRE( allNan( movingAverageDays( { 1.f, 2.f }, t, 21.0 ) ) );
}

// ---------------------------------------------------------------------------
// savitzkyGolayDays
// ---------------------------------------------------------------------------

TEST_CASE( "savitzkyGolayDays reproduces polynomials on irregular axes",
           "[temporal][irregular]" )
{
    ensureApp();
    // Irregular axis incl. a large gap — position-based SG cannot reproduce
    // a polynomial in days here because y(i) is nonlinear in index space.
    const std::vector<double> t = { 0, 3, 4, 9, 15, 16, 23, 40, 55, 90 };
    std::vector<float> lin( t.size() ), quad( t.size() );
    for ( size_t i = 0; i < t.size(); ++i )
    {
        lin[i] = static_cast<float>( 1.0 + 0.1 * t[i] );
        quad[i] = static_cast<float>( 2.0 - 0.05 * t[i] + 0.001 * t[i] * t[i] );
    }
    // Degree-1 fit over a wide window: linear is reproduced exactly.
    const auto fLin = savitzkyGolayDays( lin, t, 120.0, 1 );
    for ( size_t i = 0; i < t.size(); ++i )
        REQUIRE( fLin[i] == Approx( lin[i] ).margin( 1e-4 ) );

    // Degree-2 fit reproduces the quadratic.
    const auto fQuad = savitzkyGolayDays( quad, t, 120.0, 2 );
    for ( size_t i = 0; i < t.size(); ++i )
        REQUIRE( fQuad[i] == Approx( quad[i] ).margin( 2e-2 ) );

    // Large day ordinals (days since a 1970 epoch) stay conditioned.
    std::vector<double> epoch = t;
    for ( double &d : epoch )
        d += 19000.0;
    const auto fEpoch = savitzkyGolayDays( quad, epoch, 120.0, 2 );
    for ( size_t i = 0; i < epoch.size(); ++i )
        REQUIRE( fEpoch[i] == Approx( quad[i] ).margin( 2e-2 ) );
}

TEST_CASE( "savitzkyGolayDays honors the day window and refuses bad input",
           "[temporal][irregular]" )
{
    ensureApp();
    const std::vector<double> t = { 0, 50, 100, 200, 400 };
    // Quadratic: only the three closest samples should enter a 120-day window
    // centered at t=100 (covers 40..160 → {50,100} + nothing at 0/200/400:
    // |0-100|=100 ≤ 60? no. |200-100|=100 > 60. |400-100|=300 > 60).
    // Window at t=100 contains {50, 100}: degree-1 fit = exact line through 2 pts.
    std::vector<float> y( t.size() );
    for ( size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( 3.0 + 0.5 * t[i] );
    const auto f = savitzkyGolayDays( y, t, 120.0, 1 );
    // Center: linear through {50,100} evaluated at 100 → exact 53.
    REQUIRE( f[2] == Approx( 53.0f ).margin( 1e-3 ) );
    // Edge t=0: window [−60,60] contains {0,50} → exact value 3.
    REQUIRE( f[0] == Approx( 3.0f ).margin( 1e-3 ) );
    // Isolated point t=400: window [340,460] contains only itself → degree-1
    // underdetermined → NaN, not fabricated.
    REQUIRE( !( f[4] == f[4] ) );

    // Refusals.
    const std::vector<double> dup = { 0, 50, 50, 200, 400 };
    REQUIRE( allNan( savitzkyGolayDays( y, dup, 120.0, 1 ) ) );
    REQUIRE( allNan( savitzkyGolayDays( y, t, -1.0, 1 ) ) );
    REQUIRE( allNan( savitzkyGolayDays( y, t, 120.0, 0 ) ) );
    REQUIRE( allNan( savitzkyGolayDays( y, t, 120.0, 5 ) ) );
    REQUIRE( allNan( savitzkyGolayDays( { 1.f }, t, 120.0, 1 ) ) );
}

// ---------------------------------------------------------------------------
// whittakerSmoothTime
// ---------------------------------------------------------------------------

TEST_CASE( "whittakerSmoothTime matches whittakerSmooth at unit spacing",
           "[temporal][irregular]" )
{
    ensureApp();
    // Independent reference: on a unit-spaced axis the divided-difference
    // penalty reduces exactly to Σ(Δ²z)² — the same Gram as whittakerSmooth.
    const int n = 12;
    std::vector<float> y( n );
    std::vector<double> t( n );
    for ( int i = 0; i < n; ++i )
    {
        t[i] = static_cast<double>( i );
        y[i] = static_cast<float>( 5.0 + std::sin( i * 0.7 ) );
    }
    y[4] = kNan; // include a gap — Whittaker fills it smoothly
    const auto expected = whittakerSmooth( y, {}, 10.0 );
    const auto actual = whittakerSmoothTime( y, t, {}, 10.0 );
    REQUIRE( actual.size() == expected.size() );
    for ( int i = 0; i < n; ++i )
        REQUIRE( actual[i] == Approx( expected[i] ).margin( 1e-4 ) );
}

TEST_CASE( "whittakerSmoothTime is exact for linear-in-days signals at any λ",
           "[temporal][irregular]" )
{
    ensureApp();
    // A linear-in-days series has zero second divided difference on ANY grid,
    // so the roughness penalty vanishes and the fit is the data itself —
    // even across a 200-day sampling gap. The position-based Whittaker sees
    // a kinked series here and would smooth across the gap.
    const std::vector<double> t = { 0, 10, 20, 30, 230, 240, 250 };
    std::vector<float> y( t.size() );
    for ( size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( 2.0 + 0.3 * t[i] );
    const auto z = whittakerSmoothTime( y, t, {}, 100.0 );
    for ( size_t i = 0; i < t.size(); ++i )
        REQUIRE( z[i] == Approx( y[i] ).margin( 1e-3 ) );

    // NaN sample on the same linear axis is filled by the smooth fit.
    std::vector<float> yGap = y;
    yGap[4] = kNan;
    const auto zGap = whittakerSmoothTime( yGap, t, {}, 100.0 );
    REQUIRE( zGap[4] == Approx( y[4] ).margin( 1e-3 ) );
}

TEST_CASE( "whittakerSmoothTime couples neighbors by day distance, not index",
           "[temporal][irregular]" )
{
    ensureApp();
    // Same values on two axes: dense vs. a huge interior gap. The gap axis
    // must produce a fit that is NOT identical to treating the values as
    // equally spaced — curvature across a 300-day jump costs far less than
    // curvature across one index step.
    const std::vector<float> y = { 0.f, 0.f, 0.f, 100.f, 0.f, 0.f, 0.f };
    const std::vector<double> tDense = { 0, 10, 20, 30, 40, 50, 60 };
    const std::vector<double> tGap   = { 0, 10, 20, 30, 330, 340, 350 };
    const auto zDense = whittakerSmoothTime( y, tDense, {}, 5.0 );
    const auto zGap = whittakerSmoothTime( y, tGap, {}, 5.0 );
    REQUIRE( zDense[3] > 0.0f );
    // Across the gap the spike is isolated in time: the fit near it stays
    // closer to the spike than the dense-grid fit does at its neighbors.
    // Concretely: gap-axis neighbors of the spike are pulled toward it far
    // less than dense-axis neighbors are.
    REQUIRE( zGap[2] < zDense[2] );
    REQUIRE( zGap[4] < zDense[4] );
    // And the spike point itself is barely damped (far neighbors cannot
    // cheaply share its curvature).
    REQUIRE( zGap[3] > zDense[3] );
}

TEST_CASE( "whittakerSmoothTimeRobust resists a single spike outlier",
           "[temporal][irregular]" )
{
    ensureApp();
    const std::vector<double> t = { 0, 16, 32, 48, 64, 80, 96, 112, 300, 316, 332, 348 };
    std::vector<float> y( t.size() );
    for ( size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( 0.4 + 0.3 * std::sin( 2.0 * 3.14159265358979323846 * t[i] / 365.25 ) );
    const std::vector<float> clean = y;
    y[5] = 9.0f; // gross outlier
    const auto zPlain = whittakerSmoothTime( y, t, {}, 500.0 );
    const auto zRobust = whittakerSmoothTimeRobust( y, t, {}, 500.0, 4 );
    // At the outlier position the robust fit must sit closer to the clean
    // signal than the plain fit.
    REQUIRE( std::fabs( zRobust[5] - clean[5] ) <
             std::fabs( zPlain[5] - clean[5] ) );
}

// ---------------------------------------------------------------------------
// Phenology limb metrics (WP4) — analytic piecewise-linear season.
// ---------------------------------------------------------------------------

namespace
{
/// Piecewise-linear single season on doy: flat base 0.2 until doy 100,
/// linear ramp to 0.9 at doy 160, plateau until doy 200, linear fall to
/// 0.2 at doy 260, flat afterwards. Crossings of any amplitude level are
/// hand-computable, so the rate expectations below are independent
/// references, not outputs of the implementation.
float seasonCurve( int doy )
{
    if ( doy <= 100 ) return 0.2f;
    if ( doy < 160 )  return 0.2f + static_cast<float>( ( doy - 100 ) * ( 0.7 / 60.0 ) );
    if ( doy <= 200 ) return 0.9f;
    if ( doy < 260 )  return 0.9f - static_cast<float>( ( doy - 200 ) * ( 0.7 / 60.0 ) );
    return 0.2f;
}
} // namespace

TEST_CASE( "phenologyThreshold reports limb rates and midpoints on an "
           "irregular axis", "[temporal][irregular][phenology]" )
{
    ensureApp();
    // Irregular sampling of the analytic season (single year, doy = t+1).
    const std::vector<int> doys = { 1, 40, 90, 110, 118, 130, 145, 155,
                                    165, 195, 215, 240, 255, 270, 300, 360 };
    const int n = static_cast<int>( doys.size() );
    std::vector<float> y( n );
    std::vector<double> t( n );
    std::vector<int> doyOf( n );
    for ( int i = 0; i < n; ++i )
    {
        doyOf[i] = doys[i];
        t[i] = static_cast<double>( doys[i] - 1 );
        y[i] = seasonCurve( doys[i] );
    }
    const SeasonalMetrics m = phenologyThreshold( y, t, doyOf, 1, 365, 0.2 );
    REQUIRE( m.valid );
    REQUIRE( m.amplitude == Approx( 0.7 ).margin( 1e-4 ) );

    // Rising limb (20%→80% amplitude): v20=0.34 crosses at doy 112 inside
    // the (110,118) bracket → t=111; v80=0.76 crosses at doy 148 inside
    // (145,155) → t=147. Rate = 0.42 / 36.
    REQUIRE( m.greenUpRate == Approx( 0.42 / 36.0 ).margin( 1e-4 ) );
    // 50% crossing lands exactly on the doy-130 sample → midpoint doy 130.
    REQUIRE( m.greenUpMidDoy == Approx( 130.0 ).margin( 1.0 ) );

    // Falling limb: v80 crossing inside (195,215) → t=210; v20 inside
    // (240,255) → t=247. Rate = 0.42 / 37 (positive magnitude).
    REQUIRE( m.senescenceRate == Approx( 0.42 / 37.0 ).margin( 1e-4 ) );
    // 50% crossing inside (215,240): t=229 → doy 230.
    REQUIRE( m.senescenceMidDoy == Approx( 230.0 ).margin( 1.0 ) );

    // Rates are in value/day — amplifying the curve must scale the rate.
    std::vector<float> y2( n );
    for ( int i = 0; i < n; ++i )
        y2[i] = 2.0f * y[i];
    const SeasonalMetrics m2 = phenologyThreshold( y2, t, doyOf, 1, 365, 0.2 );
    REQUIRE( m2.greenUpRate == Approx( 2.0 * 0.42 / 36.0 ).margin( 2e-4 ) );
    REQUIRE( m2.greenUpMidDoy == Approx( m.greenUpMidDoy ).margin( 0.5 ) );
}

TEST_CASE( "phenologyThreshold leaves limb metrics undefined when the "
           "window opens mid-ramp", "[temporal][irregular][phenology]" )
{
    ensureApp();
    const std::vector<int> doys = { 1, 40, 90, 110, 118, 130, 145, 155,
                                    165, 195, 215, 240, 255, 270, 300, 360 };
    const int n = static_cast<int>( doys.size() );
    std::vector<float> y( n );
    std::vector<double> t( n );
    std::vector<int> doyOf( n );
    for ( int i = 0; i < n; ++i )
    {
        doyOf[i] = doys[i];
        t[i] = static_cast<double>( doys[i] - 1 );
        y[i] = seasonCurve( doys[i] );
    }
    // Window [130, 365]: the first in-season sample is already at 55% of the
    // amplitude, so the rising 20%/50%/80% crossings were never sampled —
    // greenUpRate/MidDoy stay undefined; the falling limb is fully sampled.
    const SeasonalMetrics m = phenologyThreshold( y, t, doyOf, 130, 365, 0.2 );
    REQUIRE( m.valid );
    REQUIRE( !( m.greenUpRate == m.greenUpRate ) ); // NaN, not fabricated
    REQUIRE( m.greenUpMidDoy < 0.0 );
    REQUIRE( m.senescenceRate == Approx( 0.42 / 37.0 ).margin( 1e-4 ) );
    REQUIRE( m.senescenceMidDoy == Approx( 230.0 ).margin( 1.0 ) );
}

TEST_CASE( "phenologyThreshold leaves limb metrics undefined on a flat season",
           "[temporal][irregular][phenology]" )
{
    ensureApp();
    const std::vector<int> doys = { 30, 90, 150, 210, 270, 330 };
    const int n = static_cast<int>( doys.size() );
    const std::vector<float> y( n, 0.5f );
    std::vector<double> t( n );
    std::vector<int> doyOf( n );
    for ( int i = 0; i < n; ++i )
    {
        doyOf[i] = doys[i];
        t[i] = static_cast<double>( doys[i] - 1 );
    }
    const SeasonalMetrics m = phenologyThreshold( y, t, doyOf, 1, 365, 0.2 );
    REQUIRE( m.valid ); // sos/eos still resolve on the degenerate curve
    REQUIRE( !( m.greenUpRate == m.greenUpRate ) );
    REQUIRE( !( m.senescenceRate == m.senescenceRate ) );
    REQUIRE( m.greenUpMidDoy < 0.0 );
    REQUIRE( m.senescenceMidDoy < 0.0 );
}

TEST_CASE( "whittakerSmoothTime refuses duplicate/degenerate axes",
           "[temporal][irregular]" )
{
    ensureApp();
    const std::vector<float> y = { 1.f, 2.f, 3.f, 4.f };
    const std::vector<double> ok = { 0, 8, 16, 24 };
    // keep_all duplicate instants: degenerate time metric → all-NaN.
    const std::vector<double> dup = { 0, 8, 8, 24 };
    REQUIRE( allNan( whittakerSmoothTime( y, dup, {}, 10.0 ) ) );
    // Descending axis.
    const std::vector<double> down = { 24, 16, 8, 0 };
    REQUIRE( allNan( whittakerSmoothTime( y, down, {}, 10.0 ) ) );
    // λ ≤ 0, size mismatch, empty.
    REQUIRE( allNan( whittakerSmoothTime( y, ok, {}, 0.0 ) ) );
    REQUIRE( allNan( whittakerSmoothTime( { 1.f, 2.f, 3.f }, ok, {}, 10.0 ) ) );
    REQUIRE( whittakerSmoothTime( {}, {}, {}, 10.0 ).empty() );
    // n < 3 pass-through contracts mirror whittakerSmooth.
    const auto one = whittakerSmoothTime( { 7.5f }, { 12.0 }, {}, 10.0 );
    REQUIRE( one.size() == 1 );
    REQUIRE( one[0] == Approx( 7.5f ).margin( 1e-6 ) );
    const auto two = whittakerSmoothTime( { 1.f, kNan }, { 0.0, 9.0 }, {}, 10.0 );
    REQUIRE( two[0] == Approx( 1.0f ).margin( 1e-6 ) );
    REQUIRE( !( two[1] == two[1] ) );
}

// ---------------------------------------------------------------------------
// WP1 — calendar axis fixtures: leap day, cross-year window, epoch ordinals.
// ---------------------------------------------------------------------------

TEST_CASE( "time-aware kernels honor the leap day on a real calendar axis",
           "[temporal][irregular][leap]" )
{
    ensureApp();
    // Scenes straddling 2024-02-29: day offsets from a real calendar so the
    // leap day occupies a genuine position on the axis (doy arithmetic via
    // QDate is leap-correct; this fixture pins the axis spacing).
    const QDate ref( 2024, 2, 25 );
    const std::vector<QDate> dates = { QDate( 2024, 2, 25 ), QDate( 2024, 2, 28 ),
                                       QDate( 2024, 2, 29 ), QDate( 2024, 3, 1 ),
                                       QDate( 2024, 3, 3 ) };
    std::vector<double> t( dates.size() );
    for ( size_t i = 0; i < dates.size(); ++i )
        t[i] = static_cast<double>( ref.daysTo( dates[i] ) );
    REQUIRE( t[2] - t[1] == Approx( 1.0 ) ); // Feb 28 → Feb 29 is ONE day
    REQUIRE( t[3] - t[2] == Approx( 1.0 ) ); // Feb 29 → Mar 1 is ONE day

    // A series linear in real days must be reproduced exactly by the day-axis
    // Whittaker — the leap day is just another 1-day step, not a gap.
    std::vector<float> y( t.size() );
    for ( size_t i = 0; i < t.size(); ++i )
        y[i] = static_cast<float>( 3.0 + 0.5 * t[i] );
    const auto z = whittakerSmoothTime( y, t, {}, 50.0 );
    for ( size_t i = 0; i < t.size(); ++i )
        REQUIRE( z[i] == Approx( y[i] ).margin( 1e-3 ) );

    // And gap-fill across the leap day interpolates by real elapsed days:
    // 2024-02-28→03-01 spans 2 days (Feb 29 missing as a SAMPLE, but the day
    // still exists on the axis): midpoint value lands halfway.
    const std::vector<float> gapped = { 10.f, kNan, 20.f };
    const std::vector<double> gapT = { 0.0, 1.0, 2.0 }; // Feb28, [Feb29], Mar1
    const auto filled = gapFillSeries( gapped, gapT, GapFillMethod::Linear, 10.0 );
    REQUIRE( filled[1] == Approx( 15.0f ).margin( 1e-4 ) );
}

TEST_CASE( "phenologyThreshold keeps metrics consistent across a year "
           "boundary (wrapped season)", "[temporal][irregular][phenology]" )
{
    ensureApp();
    // Southern-hemisphere style season wrapping Dec→Feb: season window
    // [330, 60] must not be confused with an empty interval — sos/eos still
    // resolve on the wrapped axis.
    const std::vector<int> doys = { 300, 330, 345, 365, 15, 30, 45, 60, 75, 120 };
    const int n = static_cast<int>( doys.size() );
    std::vector<float> y( n );
    std::vector<double> t( n );
    std::vector<int> doyOf( n );
    // Piecewise-linear wrapped season: base 0.2; ramp starts doy 340, peaks
    // at 0.9 around doy 20, falls back to base by doy 60.
    for ( int i = 0; i < n; ++i )
    {
        doyOf[i] = doys[i];
        t[i] = static_cast<double>( i ) * 10.0; // sample order = time order
        const int d = doys[i];
        const int wd = d >= 330 ? d - 330 : d + ( 365 - 330 ); // 0-based wrapped
        const double frac = wd <= 50 ? wd / 50.0 : std::max( 0.0, 1.0 - ( wd - 50 ) / 40.0 );
        y[i] = static_cast<float>( 0.2 + 0.7 * frac );
    }
    const SeasonalMetrics m = phenologyThreshold( y, t, doyOf, 330, 60, 0.2 );
    REQUIRE( m.valid );
    REQUIRE( m.amplitude == Approx( 0.7 ).margin( 1e-3 ) );
    REQUIRE( m.sos >= 0.0 );
    REQUIRE( m.eos >= 0.0 );
    // LOS is measured on the real time axis, not position count.
    REQUIRE( m.los > 0.0 );
}

// ---------------------------------------------------------------------------
// WP6 — grid-compatibility contract (kernel level; operator e2e lives in
// test_temporal_operators_10.cpp).
// ---------------------------------------------------------------------------

TEST_CASE( "checkGridCompatibility accepts identical grids and types "
           "each mismatch", "[temporal][fusion]" )
{
    ensureApp();
    GridSignature ref;
    ref.width = 128;
    ref.height = 96;
    ref.geoTransform = { 500000.0, 30.0, 0.0, 4500000.0, 0.0, -30.0 };
    ref.projection = "EPSG:32648-ish WKT";

    const auto ok = checkGridCompatibility( ref, ref, 1e-9 );
    REQUIRE( ok.compatible );
    REQUIRE( ok.mismatches.empty() );

    GridSignature w = ref; w.width = 129;
    GridSignature h = ref; h.height = 97;
    GridSignature g = ref; g.geoTransform[0] += 1e-6;
    GridSignature p = ref; p.projection = "EPSG:32649-ish WKT";

    const auto cw = checkGridCompatibility( ref, w, 1e-9 );
    const auto ch = checkGridCompatibility( ref, h, 1e-9 );
    const auto cg = checkGridCompatibility( ref, g, 1e-9 );
    const auto cp = checkGridCompatibility( ref, p, 1e-9 );
    REQUIRE( !cw.compatible );
    REQUIRE( cw.mismatches == std::vector<std::string>{ "width" } );
    REQUIRE( !ch.compatible );
    REQUIRE( ch.mismatches == std::vector<std::string>{ "height" } );
    REQUIRE( !cg.compatible );
    REQUIRE( cg.mismatches == std::vector<std::string>{ "geotransform" } );
    REQUIRE( !cp.compatible );
    REQUIRE( cp.mismatches == std::vector<std::string>{ "projection" } );

    // Tolerance is honored: a sub-tolerance shift passes.
    GridSignature tiny = ref; tiny.geoTransform[0] += 5e-10;
    REQUIRE( checkGridCompatibility( ref, tiny, 1e-9 ).compatible );

    // Multiple mismatches accumulate (typed list, not a single bool).
    GridSignature both = ref; both.width = 1; both.projection.clear();
    const auto cm = checkGridCompatibility( ref, both, 1e-9 );
    REQUIRE( !cm.compatible );
    REQUIRE( cm.mismatches.size() == 2 );
}

// ---------------------------------------------------------------------------
// WP7 — bounded-memory evidence: the streaming estimate scales with the tile,
// not the raster.
// ---------------------------------------------------------------------------

TEST_CASE( "TemporalTileReader working-set estimate is tile-bounded, not "
           "raster-bounded", "[temporal][irregular][streaming]" )
{
    ensureApp();
    // Peak memory must track tileWidth*tileHeight, not scene/pixel totals:
    // the same estimate must result from ANY raster size once the tile
    // shape and per-pixel buffer count are fixed.
    const std::uint64_t small =
      TemporalTileReader::estimateWorkingSetBytes( 64, 64, 8, 2 );
    const std::uint64_t big =
      TemporalTileReader::estimateWorkingSetBytes( 1024, 1024, 8, 2 );
    REQUIRE( small > 0 );
    REQUIRE( big > small );
    // Doubling tile side quadruples the estimate (linear in tile pixels).
    const std::uint64_t doubled =
      TemporalTileReader::estimateWorkingSetBytes( 128, 128, 8, 2 );
    REQUIRE( doubled == small * 4 );
    // Raster dimensions never enter the estimate — the contract the
    // operators rely on for their OOM guard.
}
