/***************************************************************************
  processing/algorithms/breakpoint_detection.cpp
  Temporal Phenology Timeline Studio (D16) — breakpoint detection kernels.
  ---------------------------
  See breakpoint_detection.h for the seam contract (ADR 0161 /
  DECISIONS D-160-6: greedy RSS splitting with exact F-test p-values and a
  strict BIC gate; MOSUM is the reported structural-stability screen).
 ***************************************************************************/

#include "processing/algorithms/breakpoint_detection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace sicnu::temporal
{

namespace
{

constexpr double kPeriod = 365.25;
constexpr double kTinyRss = 1e-12;
constexpr float kNanOutput = std::numeric_limits<float>::quiet_NaN();

/// Continued fraction of the incomplete beta function (Lentz's algorithm).
double betacf( double a, double b, double x )
{
    constexpr int kMaxIter = 300;
    constexpr double kEps = 3e-15;
    constexpr double kFpMin = 1e-300;
    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if ( std::abs( d ) < kFpMin )
        d = kFpMin;
    d = 1.0 / d;
    double h = d;
    for ( int m = 1; m <= kMaxIter; ++m )
    {
        const int m2 = 2 * m;
        double aa = static_cast<double>( m ) * ( b - m ) * x /
                    ( ( qam + m2 ) * ( a + m2 ) );
        d = 1.0 + aa * d;
        if ( std::abs( d ) < kFpMin )
            d = kFpMin;
        c = 1.0 + aa / c;
        if ( std::abs( c ) < kFpMin )
            c = kFpMin;
        d = 1.0 / d;
        h *= d * c;
        aa = -static_cast<double>( a + m ) * ( qab + m ) * x /
             ( ( a + m2 ) * ( qap + m2 ) );
        d = 1.0 + aa * d;
        if ( std::abs( d ) < kFpMin )
            d = kFpMin;
        c = 1.0 + aa / c;
        if ( std::abs( c ) < kFpMin )
            c = kFpMin;
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if ( std::abs( delta - 1.0 ) < kEps )
            break;
    }
    return h;
}

/// Regularized incomplete beta I_x(a, b).
double betainc( double a, double b, double x )
{
    if ( x <= 0.0 )
        return 0.0;
    if ( x >= 1.0 )
        return 1.0;
    const double bt = std::exp( std::lgamma( a + b ) - std::lgamma( a ) -
                                std::lgamma( b ) + a * std::log( x ) +
                                b * std::log( 1.0 - x ) );
    if ( x < ( a + 1.0 ) / ( a + b + 2.0 ) )
        return bt * betacf( a, b, x ) / a;
    return 1.0 - bt * betacf( b, a, 1.0 - x ) / b;
}

/// Exact p-value of the Chow-type F test (upper tail of F(d1, d2)).
double fTestPValue( double f, double d1, double d2 )
{
    if ( !( f > 0.0 ) || !( d1 > 0.0 ) || !( d2 > 0.0 ) )
        return 1.0;
    const double x = d1 * f / ( d1 * f + d2 );
    return betainc( d2 / 2.0, d1 / 2.0, 1.0 - x );
}

/// Solves the symmetric-in-structure dense system a·x = b (row-major n×n)
/// by Gaussian elimination with partial pivoting; false on singular input.
bool solveDense( int n, std::vector<double> &a, std::vector<double> &b )
{
    for ( int col = 0; col < n; ++col )
    {
        int pivot = col;
        for ( int r = col + 1; r < n; ++r )
            if ( std::abs( a[static_cast<std::size_t>( r ) * n + col] ) >
                 std::abs( a[static_cast<std::size_t>( pivot ) * n + col] ) )
                pivot = r;
        if ( !( std::abs( a[static_cast<std::size_t>( pivot ) * n + col] ) > 1e-12 ) )
            return false;
        if ( pivot != col )
        {
            for ( int c = 0; c < n; ++c )
                std::swap( a[static_cast<std::size_t>( pivot ) * n + c],
                           a[static_cast<std::size_t>( col ) * n + c] );
            std::swap( b[pivot], b[col] );
        }
        for ( int r = col + 1; r < n; ++r )
        {
            const double f = a[static_cast<std::size_t>( r ) * n + col] /
                             a[static_cast<std::size_t>( col ) * n + col];
            for ( int c = col; c < n; ++c )
                a[static_cast<std::size_t>( r ) * n + c] -=
                    f * a[static_cast<std::size_t>( col ) * n + c];
            b[r] -= f * b[col];
        }
    }
    for ( int r = n; r-- > 0; )
    {
        double v = b[r];
        for ( int c = r + 1; c < n; ++c )
            v -= a[static_cast<std::size_t>( r ) * n + c] * b[c];
        b[r] = v / a[static_cast<std::size_t>( r ) * n + r];
    }
    return true;
}

/// OLS fit of intercept + trend + @a harmonics sin/cos pairs over [lo, hi).
/// Returns coefficients [α, β, γ1, δ1, …] and the segment RSS.
bool olsSegmentFit( const std::vector<double> &t, const std::vector<float> &y,
                    std::size_t lo, std::size_t hi, int harmonics,
                    std::vector<double> &coef, double &rss )
{
    const int p = 2 + 2 * harmonics;
    const std::size_t len = hi - lo;
    if ( len < static_cast<std::size_t>( p ) + 2 )
        return false;

    std::vector<double> ata( static_cast<std::size_t>( p ) * p, 0.0 );
    std::vector<double> atb( p, 0.0 );
    for ( std::size_t i = lo; i < hi; ++i )
    {
        double row[14]; // p <= 14 (harmonics <= 6)
        row[0] = 1.0;
        row[1] = t[i];
        for ( int j = 1; j <= harmonics; ++j )
        {
            const double w = 2.0 * M_PI * static_cast<double>( j ) * t[i] / kPeriod;
            row[2 * j] = std::sin( w );
            row[2 * j + 1] = std::cos( w );
        }
        for ( int r = 0; r < p; ++r )
        {
            atb[r] += row[r] * y[i];
            for ( int c = r; c < p; ++c )
                ata[static_cast<std::size_t>( r ) * p + c] += row[r] * row[c];
        }
    }
    for ( int r = 0; r < p; ++r )
        for ( int c = 0; c < r; ++c )
            ata[static_cast<std::size_t>( r ) * p + c] =
                ata[static_cast<std::size_t>( c ) * p + r];

    if ( !solveDense( p, ata, atb ) )
        return false;
    coef = atb;

    rss = 0.0;
    for ( std::size_t i = lo; i < hi; ++i )
    {
        double fit = coef[0] + coef[1] * t[i];
        for ( int j = 1; j <= harmonics; ++j )
        {
            const double w = 2.0 * M_PI * static_cast<double>( j ) * t[i] / kPeriod;
            fit += coef[2 * j] * std::sin( w ) + coef[2 * j + 1] * std::cos( w );
        }
        const double r = static_cast<double>( y[i] ) - fit;
        rss += r * r;
    }
    return std::isfinite( rss );
}

} // namespace

BfastResult BreakpointDetector::detectHarmonicBreaks( const std::vector<float> &y,
                                                      const std::vector<double> &tDays,
                                                      int harmonics, int maxBreaks,
                                                      int minSegmentSamples,
                                                      double significanceAlpha )
{
    BfastResult result;
    if ( y.size() != tDays.size() || y.empty() )
        return result;
    if ( harmonics < 1 || harmonics > 6 || maxBreaks < 0 || minSegmentSamples < 2 ||
         !( significanceAlpha > 0.0 && significanceAlpha < 1.0 ) )
        return result;

    // Finite pairs with their original positions, ascending time (NaN =
    // absent; outputs map back to the original axis at the end).
    std::vector<double> t;
    std::vector<float> v;
    std::vector<std::size_t> origin;
    for ( std::size_t i = 0; i < y.size(); ++i )
        if ( std::isfinite( y[i] ) && std::isfinite( tDays[i] ) )
        {
            t.push_back( tDays[i] );
            v.push_back( y[i] );
            origin.push_back( i );
        }
    std::vector<std::size_t> order( t.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
        order[i] = i;
    std::sort( order.begin(), order.end(),
               [&]( std::size_t a, std::size_t b ) { return t[a] < t[b]; } );
    std::vector<double> ts( t.size() );
    std::vector<float> ys( t.size() );
    std::vector<std::size_t> originSorted( t.size() );
    for ( std::size_t i = 0; i < order.size(); ++i )
    {
        ts[i] = t[order[i]];
        ys[i] = v[order[i]];
        originSorted[i] = origin[order[i]];
    }
    const std::size_t n = ts.size();
    const int p = 2 + 2 * harmonics;
    if ( n < static_cast<std::size_t>( p ) + 2 )
        return result;

    // Global fit: RSS baseline, MOSUM screen.
    std::vector<double> poolCoef;
    double poolRss = 0.0;
    if ( !olsSegmentFit( ts, ys, 0, n, harmonics, poolCoef, poolRss ) )
        return result;

    const auto harmonicValue = [&]( const std::vector<double> &coef, double time ) {
        double s = 0.0;
        for ( int j = 1; j <= harmonics; ++j )
        {
            const double w = 2.0 * M_PI * static_cast<double>( j ) * time / kPeriod;
            s += coef[2 * j] * std::sin( w ) + coef[2 * j + 1] * std::cos( w );
        }
        return s;
    };
    const auto trendValue = [&]( const std::vector<double> &coef, double time ) {
        return coef[0] + coef[1] * time;
    };

    const int h = std::max( 1, static_cast<int>( 0.15 * static_cast<double>( n ) ) );
    result.mosumH = h;
    if ( poolRss > kTinyRss )
    {
        const double sigma = std::sqrt( poolRss / static_cast<double>( n ) );
        // Sliding window sum of the global-fit residuals.
        std::vector<double> residuals( n );
        for ( std::size_t i = 0; i < n; ++i )
            residuals[i] = static_cast<double>( ys[i] ) -
                           ( trendValue( poolCoef, ts[i] ) + harmonicValue( poolCoef, ts[i] ) );
        double window = 0.0;
        for ( int s = 0; s < h && s < static_cast<int>( n ); ++s )
            window += residuals[s];
        for ( int end = h; end <= static_cast<int>( n ); ++end )
        {
            result.mosumMax = std::max( result.mosumMax,
                                        std::abs( window / ( sigma * std::sqrt( static_cast<double>( n ) ) ) ) );
            if ( end < static_cast<int>( n ) )
                window += residuals[end] - residuals[end - h];
        }
    }

    // Greedy recursive splitting under F + BIC gates.
    std::vector<std::size_t> bounds{ 0, n }; // segment boundary indices

    for ( int accepted = 0; accepted < maxBreaks; ++accepted )
    {
        struct Candidate
        {
            std::size_t split = 0;
            double rssReduction = 0.0;
            double pValue = 1.0;
            bool bicImproves = false;
            double leftRss = 0.0;
            double rightRss = 0.0;
            std::vector<double> leftCoef;
            std::vector<double> rightCoef;
            bool ok = false;
        };
        Candidate best;

        for ( std::size_t seg = 0; seg + 1 < bounds.size(); ++seg )
        {
            const std::size_t a = bounds[seg];
            const std::size_t b = bounds[seg + 1];
            if ( b - a < static_cast<std::size_t>( 2 * minSegmentSamples ) )
                continue;
            double segRss = 0.0;
            std::vector<double> segCoef;
            if ( !olsSegmentFit( ts, ys, a, b, harmonics, segCoef, segRss ) )
                continue;

            Candidate candidate;
            for ( std::size_t s = a + minSegmentSamples;
                  s <= b - minSegmentSamples; ++s )
            {
                double leftRss = 0.0, rightRss = 0.0;
                std::vector<double> leftCoef, rightCoef;
                if ( !olsSegmentFit( ts, ys, a, s, harmonics, leftCoef, leftRss ) )
                    continue;
                if ( !olsSegmentFit( ts, ys, s, b, harmonics, rightCoef, rightRss ) )
                    continue;
                const double splitRss = leftRss + rightRss;
                // RSS-minimizing split; first index wins on exact ties.
                const bool better =
                    !candidate.ok || splitRss < candidate.leftRss + candidate.rightRss - kTinyRss;
                if ( better )
                {
                    candidate.split = s;
                    candidate.leftRss = leftRss;
                    candidate.rightRss = rightRss;
                    candidate.leftCoef = std::move( leftCoef );
                    candidate.rightCoef = std::move( rightCoef );
                    candidate.rssReduction = segRss - splitRss;
                    candidate.ok = true;
                }
            }
            if ( !candidate.ok )
                continue;

            const std::size_t segLen = b - a;
            const double splitRss = candidate.leftRss + candidate.rightRss;
            // Chow-type F test for the split. Strict positivity, not an
            // absolute floor: noise-free series have residual RSS far below
            // any absolute epsilon while the test stays well-defined
            // (constant series are caught by the rssReduction gate instead).
            const double fStat =
                ( splitRss > 0.0 && candidate.rssReduction > 0.0 )
                    ? ( candidate.rssReduction / static_cast<double>( p ) ) /
                          ( splitRss / static_cast<double>( segLen - 2 * p ) )
                    : 0.0;
            candidate.pValue = fTestPValue( fStat, p, static_cast<double>( segLen - 2 * p ) );
            // BIC gate: n·ln(RSS/n) + p·ln(n), strictly decreasing.
            const double bicPool = static_cast<double>( segLen ) *
                                       std::log( std::max( segRss, kTinyRss ) /
                                                 static_cast<double>( segLen ) ) +
                                   static_cast<double>( p ) * std::log( static_cast<double>( segLen ) );
            const double bicSplit =
                static_cast<double>( segLen ) *
                    std::log( std::max( splitRss, kTinyRss ) / static_cast<double>( segLen ) ) +
                static_cast<double>( 2 * p ) * std::log( static_cast<double>( segLen ) );
            candidate.bicImproves = bicSplit < bicPool - 1e-9;

            if ( !best.ok || candidate.rssReduction > best.rssReduction )
                best = candidate;
        }

        if ( !best.ok )
            break;
        if ( !( best.pValue < significanceAlpha ) || !best.bicImproves ||
             !( best.rssReduction > kTinyRss ) )
            break; // no statistically supported split remains

        bounds.insert( std::upper_bound( bounds.begin(), bounds.end(), best.split ),
                       best.split );
        BreakpointCandidate bp;
        bp.index = static_cast<int>( best.split );
        bp.tDays = ts[best.split];
        const double levelLeft =
            trendValue( best.leftCoef, ts[best.split] ) +
            harmonicValue( best.leftCoef, ts[best.split] );
        const double levelRight =
            trendValue( best.rightCoef, ts[best.split] ) +
            harmonicValue( best.rightCoef, ts[best.split] );
        bp.magnitude = levelRight - levelLeft;
        bp.pValue = best.pValue;
        bp.rssReduction = best.rssReduction;
        result.breakpoints.push_back( bp );
        result.breakCount = static_cast<int>( result.breakpoints.size() );
    }

    // Final per-segment decomposition, mapped back onto the original axis
    // (positions that were NaN keep NaN in every output plane).
    result.fittedTrend.assign( y.size(), 0.0f );
    result.fittedHarmonics.assign( y.size(), 0.0f );
    result.residuals.assign( y.size(), 0.0f );
    double rss = 0.0;
    bool ok = true;
    for ( std::size_t seg = 0; seg + 1 < bounds.size() && ok; ++seg )
    {
        std::vector<double> coef;
        double segRss = 0.0;
        if ( !olsSegmentFit( ts, ys, bounds[seg], bounds[seg + 1], harmonics, coef, segRss ) )
        {
            ok = false;
            break;
        }
        rss += segRss;
        for ( std::size_t i = bounds[seg]; i < bounds[seg + 1]; ++i )
        {
            const std::size_t out = originSorted[i];
            result.fittedTrend[out] = static_cast<float>( trendValue( coef, ts[i] ) );
            result.fittedHarmonics[out] = static_cast<float>( harmonicValue( coef, ts[i] ) );
            result.residuals[out] = static_cast<float>(
                static_cast<double>( ys[i] ) - result.fittedTrend[out] -
                result.fittedHarmonics[out] );
        }
    }
    if ( ok )
    {
        for ( std::size_t i = 0; i < y.size(); ++i )
        {
            if ( std::isfinite( y[i] ) )
                continue;
            result.fittedTrend[i] = kNanOutput;
            result.fittedHarmonics[i] = kNanOutput;
            result.residuals[i] = kNanOutput;
        }
    }
    if ( !ok )
    {
        result.breakpoints.clear();
        result.breakCount = 0;
        result.fittedTrend.assign( y.size(), kNanOutput );
        result.fittedHarmonics.assign( y.size(), kNanOutput );
        result.residuals.assign( y.size(), kNanOutput );
        return result; // honest failure: no stale partial output
    }

    result.overallRmse = std::sqrt( rss / static_cast<double>( n ) );
    result.valid = true;
    return result;
}

} // namespace sicnu::temporal
