// src/processing/algorithms/temporal/temporal_selection.cpp
#include "temporal_selection.h"

#include "temporal_fit.h"
#include "temporal_linalg_detail.h"
#include "temporal_design_detail.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr double kNanD = std::numeric_limits<double>::quiet_NaN();
using detail::kMaxTerms;
using detail::harmonicTrendDesignRow;
using detail::evalHarmonicTrend;

/// Union-fit design width: [1, t', sin/cos_L(1..h), sin/cos_R(1..h)] with
/// h <= 3.
constexpr int kUnionTerms = 2 + 4 * 3;

/// Plain (weight-1-on-finite) harmonic+trend OLS over [a, b). The design
/// time axis is centered at @a tCenter (the break) so intercepts are levels
/// AT the break — direct jump estimates, better conditioned, less
/// level/seasonal confounding inside the test window. Returns false when
/// the segment holds fewer valid samples than terms or the system is
/// singular.
bool fitPlain( const std::vector<float> &y, const std::vector<double> &tDays,
               int a, int b, int harmonics, std::vector<double> *coefOut,
               double *sseOut, int *validOut, double tCenter )
{
  std::vector<double> ata( static_cast<size_t>( kMaxTerms ) * kMaxTerms, 0.0 );
  std::vector<double> atb( kMaxTerms, 0.0 );
  double design[kMaxTerms];
  const int terms = 2 + 2 * harmonics;
  int valid = 0;
  for ( int i = a; i < b; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const int m = harmonicTrendDesignRow( tDays[static_cast<size_t>( i )] - tCenter,
                                          harmonics, design );
    for ( int r = 0; r < m; ++r )
    {
      atb[static_cast<size_t>( r )] += design[r] * y[static_cast<size_t>( i )];
      for ( int c = 0; c < m; ++c )
        ata[static_cast<size_t>( r ) * kMaxTerms + c] += design[r] * design[c];
    }
    ++valid;
  }
  if ( valid < terms )
    return false;
  std::vector<double> aDense( static_cast<size_t>( terms ) * terms, 0.0 );
  std::vector<double> bDense( terms, 0.0 );
  for ( int r = 0; r < terms; ++r )
  {
    bDense[static_cast<size_t>( r )] = atb[static_cast<size_t>( r )];
    for ( int c = 0; c < terms; ++c )
      aDense[static_cast<size_t>( r ) * terms + c] =
        ata[static_cast<size_t>( r ) * kMaxTerms + c];
  }
  std::vector<double> coef;
  if ( !detail::solveSmallDense( aDense, bDense, terms, &coef ) )
    return false;
  double sse = 0.0;
  for ( int i = a; i < b; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const int m = harmonicTrendDesignRow( tDays[static_cast<size_t>( i )] - tCenter,
                                          harmonics, design );
    double v = 0.0;
    for ( int r = 0; r < m; ++r )
      v += coef[r] * design[r];
    const double d = y[static_cast<size_t>( i )] - v;
    sse += d * d;
  }
  if ( coefOut )
    *coefOut = coef;
  if ( sseOut )
    *sseOut = sse;
  if ( validOut )
    *validOut = valid;
  return true;
}

/// Constrained union fit over [l, r) at break @a b: intercept + slope
/// SHARED, seasonal sin/cos SEPARATE per side (design [1, t', sin/cos_L...,
/// sin/cos_R...] with t' = t − @a tCenter, the break day). The symmetric
/// complement of fitUnionTrendChange: together they let each model block
/// (trend, seasonal) be tested against the FULL separate model, so a pure
/// amplitude change does not leak into the trend verdict (and vice versa).
/// Returns false when too few valid samples or singular.
bool fitUnionSeasonalChange( const std::vector<float> &y,
                             const std::vector<double> &tDays, int l, int r,
                             int b, int harmonics, double *sseOut,
                             int *validOut, double tCenter )
{
  double ata[static_cast<size_t>( kUnionTerms ) * kUnionTerms] = {};
  double atb[kUnionTerms] = {};
  double designL[kUnionTerms];
  double designR[kUnionTerms];
  const int m = 2 + 4 * harmonics;
  int valid = 0;
  for ( int i = l; i < r; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const double t = tDays[static_cast<size_t>( i )] - tCenter;
    harmonicTrendDesignRow( t, harmonics, designL );
    // Shared [1, t'] then this side's seasonal block.
    designR[0] = designL[0];
    designR[1] = designL[1];
    for ( int c = 2; c < m; ++c )
      designR[c] = 0.0;
    const int dst = i >= b ? 2 + 2 * harmonics : 2;
    for ( int c = 0; c < 2 * harmonics; ++c )
      designR[dst + c] = designL[2 + c];
    for ( int rr = 0; rr < m; ++rr )
    {
      atb[rr] += designR[rr] * y[static_cast<size_t>( i )];
      for ( int c = 0; c < m; ++c )
        ata[static_cast<size_t>( rr ) * kUnionTerms + c] += designR[rr] * designR[c];
    }
    ++valid;
  }
  if ( valid < m )
    return false;
  std::vector<double> aDense( static_cast<size_t>( m ) * m, 0.0 );
  std::vector<double> bDense( m, 0.0 );
  for ( int rr = 0; rr < m; ++rr )
  {
    bDense[static_cast<size_t>( rr )] = atb[rr];
    for ( int c = 0; c < m; ++c )
      aDense[static_cast<size_t>( rr ) * m + c] =
        ata[static_cast<size_t>( rr ) * kUnionTerms + c];
  }
  std::vector<double> coef;
  if ( !detail::solveSmallDense( aDense, bDense, m, &coef ) )
    return false;
  double sse = 0.0;
  for ( int i = l; i < r; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const double t = tDays[static_cast<size_t>( i )] - tCenter;
    harmonicTrendDesignRow( t, harmonics, designL );
    designR[0] = designL[0];
    designR[1] = designL[1];
    for ( int c = 2; c < m; ++c )
      designR[c] = 0.0;
    const int dst = i >= b ? 2 + 2 * harmonics : 2;
    for ( int c = 0; c < 2 * harmonics; ++c )
      designR[dst + c] = designL[2 + c];
    double v = 0.0;
    for ( int rr = 0; rr < m; ++rr )
      v += coef[static_cast<size_t>( rr )] * designR[rr];
    const double d = y[static_cast<size_t>( i )] - v;
    sse += d * d;
  }
  if ( sseOut )
    *sseOut = sse;
  if ( validOut )
    *validOut = valid;
  return true;
}

/// Constrained union fit over [l, r) at break @a b: seasonal sin/cos shared,
/// intercept + slope SEPARATE per side (design [1, t', sin/cos..., 1_R,
/// t'·1_R] with t' = t − @a tCenter). The symmetric complement of
/// fitUnionSeasonalChange. Returns false when too few valid samples or
/// singular.
bool fitUnionTrendChange( const std::vector<float> &y,
                          const std::vector<double> &tDays, int l, int r, int b,
                          int harmonics, double *sseOut, int *validOut,
                          double tCenter )
{
  double ata[static_cast<size_t>( kUnionTerms ) * kUnionTerms] = {};
  double atb[kUnionTerms] = {};
  double designL[kUnionTerms];
  double designR[kUnionTerms];
  const int mBase = 2 + 2 * harmonics;
  const int m = mBase + 2;
  int valid = 0;
  for ( int i = l; i < r; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const double t = tDays[static_cast<size_t>( i )] - tCenter;
    const int mRow = harmonicTrendDesignRow( t, harmonics, designL );
    for ( int c = 0; c < mRow; ++c )
      designR[c] = designL[c];
    designR[mBase] = 0.0;
    designR[mBase + 1] = 0.0;
    if ( i >= b )
    {
      designR[mBase] = 1.0;
      designR[mBase + 1] = t;
    }
    for ( int rr = 0; rr < m; ++rr )
    {
      atb[rr] += designR[rr] * y[static_cast<size_t>( i )];
      for ( int c = 0; c < m; ++c )
        ata[static_cast<size_t>( rr ) * kUnionTerms + c] += designR[rr] * designR[c];
    }
    ++valid;
  }
  if ( valid < m )
    return false;
  std::vector<double> aDense( static_cast<size_t>( m ) * m, 0.0 );
  std::vector<double> bDense( m, 0.0 );
  for ( int rr = 0; rr < m; ++rr )
  {
    bDense[static_cast<size_t>( rr )] = atb[rr];
    for ( int c = 0; c < m; ++c )
      aDense[static_cast<size_t>( rr ) * m + c] =
        ata[static_cast<size_t>( rr ) * kUnionTerms + c];
  }
  std::vector<double> coef;
  if ( !detail::solveSmallDense( aDense, bDense, m, &coef ) )
    return false;
  double sse = 0.0;
  for ( int i = l; i < r; ++i )
  {
    if ( !std::isfinite( y[static_cast<size_t>( i )] ) )
      continue;
    const double t = tDays[static_cast<size_t>( i )] - tCenter;
    const int mRow = harmonicTrendDesignRow( t, harmonics, designL );
    for ( int c = 0; c < mRow; ++c )
      designR[c] = designL[c];
    designR[mBase] = 0.0;
    designR[mBase + 1] = 0.0;
    if ( i >= b )
    {
      designR[mBase] = 1.0;
      designR[mBase + 1] = t;
    }
    double v = 0.0;
    for ( int rr = 0; rr < m; ++rr )
      v += coef[static_cast<size_t>( rr )] * designR[rr];
    const double d = y[static_cast<size_t>( i )] - v;
    sse += d * d;
  }
  if ( sseOut )
    *sseOut = sse;
  if ( validOut )
    *validOut = valid;
  return true;
}

// --- F-distribution survival via the regularized incomplete beta ---

/// Continued-fraction complement for I_x(a, b) (Lentz's algorithm).
double betaCF( double a, double b, double x )
{
  constexpr int kMaxIterations = 200;
  constexpr double kEps = 3e-16;
  constexpr double kFpMin = 1e-300;
  const double qab = a + b;
  const double qap = a + 1.0;
  const double qam = a - 1.0;
  double c = 1.0;
  double d = 1.0 - qab * x / qap;
  if ( std::fabs( d ) < kFpMin )
    d = kFpMin;
  d = 1.0 / d;
  double h = d;
  for ( int m = 1; m <= kMaxIterations; ++m )
  {
    const int m2 = 2 * m;
    double aa = static_cast<double>( m ) * ( b - static_cast<double>( m ) ) * x /
                ( ( qam + static_cast<double>( m2 ) ) * ( a + static_cast<double>( m2 ) ) );
    d = 1.0 + aa * d;
    if ( std::fabs( d ) < kFpMin )
      d = kFpMin;
    c = 1.0 + aa / c;
    if ( std::fabs( c ) < kFpMin )
      c = kFpMin;
    d = 1.0 / d;
    h *= d * c;
    aa = -( a + static_cast<double>( m ) ) * ( qab + static_cast<double>( m ) ) * x /
         ( ( a + static_cast<double>( m2 ) ) * ( qap + static_cast<double>( m2 ) ) );
    d = 1.0 + aa * d;
    if ( std::fabs( d ) < kFpMin )
      d = kFpMin;
    c = 1.0 + aa / c;
    if ( std::fabs( c ) < kFpMin )
      c = kFpMin;
    d = 1.0 / d;
    const double del = d * c;
    h *= del;
    if ( std::fabs( del - 1.0 ) < kEps )
      break;
  }
  return h;
}

/// Regularized incomplete beta I_x(a, b).
double incompleteBeta( double a, double b, double x )
{
  if ( !( x > 0.0 ) )
    return 0.0;
  if ( x >= 1.0 )
    return 1.0;
  const double bt = std::exp( std::lgamma( a + b ) - std::lgamma( a ) - std::lgamma( b ) +
                              a * std::log( x ) + b * std::log( 1.0 - x ) );
  if ( x < ( a + 1.0 ) / ( a + b + 2.0 ) )
    return bt * betaCF( a, b, x ) / a;
  return 1.0 - bt * betaCF( b, a, 1.0 - x ) / b;
}

/// P(F(df1, df2) > f).
double fSurvival( double f, double df1, double df2 )
{
  if ( !( f > 0.0 ) || df1 <= 0.0 || df2 <= 0.0 )
    return 1.0;
  const double x = df2 / ( df2 + df1 * f );
  return incompleteBeta( df2 / 2.0, df1 / 2.0, x );
}

double wrapPhase( double radians )
{
  return std::atan2( std::sin( radians ), std::cos( radians ) );
}

} // namespace

BreakAttributionResult attributeSeasonalTrendBreaks(
    const SeasonalTrendBreaksResult &fit, const std::vector<float> &y,
    const std::vector<double> &tDays, const BreakAttributionOptions &options )
{
  BreakAttributionResult result;
  const int harmonics = std::clamp( options.harmonics, 1, 3 );
  const double alpha = std::clamp( options.alpha, 1e-6, 0.5 );
  const int terms = 2 + 2 * harmonics;

  // Polar seasonal parameters per segment (from the segmentation's own
  // stored coefficients; valid only where the segment has a fit).
  result.segmentSeasonal.assign( fit.segments.size(), {} );
  for ( size_t s = 0; s < fit.segments.size(); ++s )
  {
    const auto &coef = s < fit.segmentCoefficients.size()
                         ? fit.segmentCoefficients[s]
                         : std::vector<double>{};
    std::vector<SeasonalParam> params(
        static_cast<size_t>( harmonics ) );
    if ( coef.size() >= static_cast<size_t>( terms ) )
    {
      for ( int k = 1; k <= harmonics; ++k )
      {
        const double sC = coef[static_cast<size_t>( 2 + 2 * ( k - 1 ) )];
        const double cC = coef[static_cast<size_t>( 3 + 2 * ( k - 1 ) )];
        SeasonalParam &p = params[static_cast<size_t>( k - 1 )];
        p.amplitude = std::hypot( sC, cC );
        p.phase = wrapPhase( std::atan2( cC, sC ) );
        p.valid = true;
      }
    }
    result.segmentSeasonal[s] = params;
  }

  result.breaks.reserve( fit.breaks.size() );
  for ( size_t j = 0; j < fit.breaks.size(); ++j )
  {
    const BreakEvent &event = fit.breaks[j];
    AttributedBreak out;
    out.index = event.index;
    out.breakDays = event.breakDays;
    out.trendMagnitude = event.magnitude;
    // Untestable is the honest default: every statistic below stays NaN
    // unless the nested fits actually ran.
    out.seasonalShift = kNanD;
    out.amplitudeChange1 = kNanD;
    out.phaseChange1 = kNanD;
    out.fStatistic = kNanD;
    out.pValue = kNanD;

    // The break sits between segments s and s+1 (boundary = segments[s+1].first).
    const int s = static_cast<int>(
        std::find_if( fit.segments.begin(), fit.segments.end(),
                      [&]( const SeasonalTrendSegment &seg ) {
                        return seg.startIndex == event.index;
                      } ) -
        fit.segments.begin() );
    if ( s >= 1 && s < static_cast<int>( fit.segments.size() ) )
    {
      const int l = fit.segments[static_cast<size_t>( s - 1 )].startIndex;
      const int r = fit.segments[static_cast<size_t>( s )].endIndex;
      const int b = event.index;
      std::vector<double> coefL, coefR;
      double sseL = 0.0, sseR = 0.0, sseTrend = 0.0, sseTrendShared = 0.0, sseFull = 0.0;
      int validL = 0, validR = 0, validTrend = 0, validSeasonShared = 0, validFull = 0;
      // Center the design at the break day (intercepts = levels AT the
      // break; better conditioned).
      const double tCenter = tDays[static_cast<size_t>( b )];
      const bool okL = fitPlain( y, tDays, l, b, harmonics, &coefL, &sseL, &validL, tCenter );
      const bool okR = fitPlain( y, tDays, b, r, harmonics, &coefR, &sseR, &validR, tCenter );
      const int nUnion = validL + validR;
      const int dfFull = nUnion - ( 4 + 4 * harmonics );
      const bool okTrend =
        fitUnionTrendChange( y, tDays, l, r, b, harmonics, &sseTrend, &validTrend,
                             tCenter );
      const bool okSeasonShared =
        fitUnionSeasonalChange( y, tDays, l, r, b, harmonics, &sseTrendShared,
                                &validSeasonShared, tCenter );
      sseFull = sseL + sseR;
      validFull = nUnion;
      if ( okL && okR && okTrend && okSeasonShared && dfFull >= 1 )
      {
        // Symmetric nested tests against the FULL separate model (SSE_full,
        // the smallest SSE): freeing the trend block (2 params: side
        // intercept+slope) beyond free seasonals, and freeing the seasonal
        // block (2·harmonics params) beyond a free trend. A pure level step
        // moves only the first statistic; a pure amplitude/phase change
        // moves only the second; a mixed break moves both.
        const double fTrend =
          ( ( sseTrendShared - sseFull ) / 2.0 ) /
          ( sseFull / static_cast<double>( dfFull ) );
        const double pTrend = fSurvival( fTrend, 2.0, static_cast<double>( dfFull ) );
        const double fSeasonal =
          ( ( sseTrend - sseFull ) / ( 2.0 * harmonics ) ) /
          ( sseFull / static_cast<double>( dfFull ) );
        const double pSeasonal =
          fSurvival( fSeasonal, 2.0 * harmonics, static_cast<double>( dfFull ) );
        out.fStatistic = fSeasonal;
        out.pValue = pSeasonal;
        const bool trendChanged = pTrend < alpha;
        const bool seasonalChanged = pSeasonal < alpha;
        if ( trendChanged && seasonalChanged )
          out.kind = BreakKind::Both;
        else if ( seasonalChanged )
          out.kind = BreakKind::SeasonalOnly;
        else if ( trendChanged )
          out.kind = BreakKind::TrendOnly;
        else
          out.kind = BreakKind::None;

        // Polar deltas from the same plain fits (self-consistent with the
        // tests above; NaN-free by construction here).
        double shift = 0.0;
        for ( int k = 1; k <= harmonics; ++k )
        {
          const int si = 2 + 2 * ( k - 1 );
          const double ds = coefR[static_cast<size_t>( si )] - coefL[static_cast<size_t>( si )];
          const double dc = coefR[static_cast<size_t>( si + 1 )] - coefL[static_cast<size_t>( si + 1 )];
          shift += ds * ds + dc * dc;
          if ( k == 1 )
          {
            const double aL = std::hypot( coefL[2], coefL[3] );
            const double aR = std::hypot( coefR[2], coefR[3] );
            out.amplitudeChange1 = aR - aL;
            out.phaseChange1 = wrapPhase( std::atan2( coefR[3], coefR[2] ) -
                                          std::atan2( coefL[3], coefL[2] ) );
          }
        }
        out.seasonalShift = std::sqrt( shift );
        // Reported trend magnitude: plain-fit level jump at the break day
        // (evaluated on the centered axis; the segmentation magnitude may
        // differ when it ran with robust IRLS).
        out.trendMagnitude =
          std::abs( evalHarmonicTrend( coefR, out.breakDays - tCenter, harmonics ) -
                    evalHarmonicTrend( coefL, out.breakDays - tCenter, harmonics ) );
        ++result.testedCount;
        if ( out.kind == BreakKind::SeasonalOnly || out.kind == BreakKind::Both )
          ++result.seasonalCount;
      }
    }
    result.breaks.push_back( out );
  }
  return result;
}

// ---------------------------------------------------------------------------
// Model selection
// ---------------------------------------------------------------------------

namespace
{
/// One candidate refit (possibly on a fold-masked copy of the series),
/// keeping enough state to predict masked samples afterwards.
struct CandidateFit
{
  bool fittable = false;
  double rss = 0.0;
  int validCount = 0;
  int segments = 0;
  std::vector<int> segStart;
  std::vector<int> segEnd;
  std::vector<std::vector<double>> coef;  // k >= 1
  std::vector<double> slope;              // k == 0
  std::vector<double> intercept;          // k == 0
};

CandidateFit runCandidate( const std::vector<float> &y,
                           const std::vector<double> &tDays, int harmonics,
                           int maxBreaks, const ModelSelectionOptions &options )
{
  CandidateFit fit;
  if ( harmonics <= 0 )
  {
    const BreakpointResult res = piecewiseLinearTrend(
        y, tDays, maxBreaks, options.minSegment, options.minImprovement );
    fit.fittable = std::isfinite( res.rmse ) && res.validCount > 0;
    if ( fit.fittable )
    {
      fit.rss = res.rmse * res.rmse * static_cast<double>( res.validCount );
      fit.validCount = static_cast<int>( res.validCount );
      fit.segments = static_cast<int>( res.breakIndices.size() ) + 1;
      fit.segStart.push_back( 0 );
      fit.slope = res.slopes;
      fit.intercept = res.intercepts;
      for ( int b : res.breakIndices )
      {
        fit.segEnd.push_back( b );
        fit.segStart.push_back( b );
      }
      fit.segEnd.push_back( static_cast<int>( y.size() ) );
    }
    return fit;
  }
  const SeasonalTrendBreaksResult res = fitSeasonalTrendBreaks(
      y, tDays, harmonics, maxBreaks, options.minSegment, options.minImprovement,
      options.robust );
  fit.fittable = std::isfinite( res.rmse ) && res.validCount >= 2 + 2 * harmonics;
  if ( fit.fittable )
  {
    fit.rss = res.rmse * res.rmse * static_cast<double>( res.validCount );
    fit.validCount = res.validCount;
    fit.segments = static_cast<int>( res.segments.size() );
    for ( const auto &seg : res.segments )
    {
      fit.segStart.push_back( seg.startIndex );
      fit.segEnd.push_back( seg.endIndex );
    }
    fit.coef = res.segmentCoefficients;
  }
  return fit;
}

/// Predicts sample @a i from the candidate's segment models (NaN when its
/// segment has no usable fit).
double predictSample( const CandidateFit &fit, int i, double t, int harmonics )
{
  for ( size_t s = 0; s < fit.segStart.size(); ++s )
  {
    if ( i < fit.segStart[s] || i >= fit.segEnd[s] )
      continue;
    if ( !fit.coef.empty() )
    {
      if ( s >= fit.coef.size() || fit.coef[s].empty() )
        return kNanD;
      return evalHarmonicTrend( fit.coef[s], t, harmonics );
    }
    if ( s >= fit.slope.size() || !std::isfinite( fit.slope[s] ) )
      return kNanD;
    return fit.slope[s] * t + fit.intercept[s];
  }
  return kNanD;
}

double scoreFromRss( double rss, int n, int params, SelectionPenalty penalty )
{
  if ( !( rss >= 0.0 ) || n <= 0 || params <= 0 )
    return kNanD;
  const double logL = -0.5 * static_cast<double>( n ) *
                      ( std::log( 2.0 * 3.14159265358979323846 * rss /
                                  static_cast<double>( n ) ) +
                        1.0 );
  if ( penalty == SelectionPenalty::BIC )
    return static_cast<double>( params ) * std::log( static_cast<double>( n ) ) -
           2.0 * logL;
  const double aic = 2.0 * static_cast<double>( params ) - 2.0 * logL;
  const double denom = static_cast<double>( n - params - 1 );
  if ( denom <= 0.0 )
    return kNanD;  // AICc undefined at this sample/parameter ratio
  return aic + 2.0 * static_cast<double>( params ) *
                 ( static_cast<double>( params ) + 1.0 ) / denom;
}
} // namespace

ModelSelectionResult selectSeasonalTrendModel(
    const std::vector<float> &y, const std::vector<double> &tDays,
    const ModelSelectionOptions &options )
{
  ModelSelectionResult result;
  const int n = static_cast<int>( y.size() );
  if ( static_cast<int>( tDays.size() ) != n )
  {
    result.degradedReason = "insufficient_valid_samples";
    return result;
  }
  int validTotal = 0;
  for ( float v : y )
    if ( std::isfinite( v ) )
      ++validTotal;

  ModelSelectionOptions opts = options;
  opts.maxHarmonics = std::clamp( opts.maxHarmonics, 0, 3 );
  opts.maxBreaks = std::clamp( opts.maxBreaks, 0, 4 );
  opts.minSegment = std::max( 3, opts.minSegment );
  opts.minImprovement = std::clamp( opts.minImprovement, 0.0, 1.0 );
  opts.cvFolds = std::clamp( opts.cvFolds, 2, 10 );

  // Global refusal only when nothing can be fitted at all; per-candidate
  // terms requirements reject over-parameterized candidates individually
  // (AICc becomes undefined there, which the scorer maps to "not fittable").
  if ( validTotal < 4 )
  {
    result.degradedReason = "insufficient_valid_samples";
    return result;
  }

  // Candidate enumeration: harmonics ascending, then maxBreaks ascending.
  for ( int k = 0; k <= opts.maxHarmonics; ++k )
  {
    for ( int b = 0; b <= opts.maxBreaks; ++b )
    {
      ModelCandidateScore score;
      score.harmonics = k;
      score.maxBreaks = b;
      if ( opts.penalty == SelectionPenalty::BlockCv )
      {
        const int folds = opts.cvFolds;
        double cvSse = 0.0;
        long cvCount = 0;
        bool anyFoldRan = false;
        for ( int f = 0; f < folds; ++f )
        {
          const int lo = static_cast<int>( static_cast<long>( f ) * n / folds );
          const int hi = static_cast<int>( static_cast<long>( f + 1 ) * n / folds );
          if ( hi <= lo )
            continue;
          std::vector<float> masked( y );
          for ( int i = lo; i < hi; ++i )
            masked[static_cast<size_t>( i )] =
              std::numeric_limits<float>::quiet_NaN();
          const CandidateFit fit =
            runCandidate( masked, tDays, k, b, opts );
          if ( !fit.fittable )
            continue;
          anyFoldRan = true;
          for ( int i = lo; i < hi; ++i )
          {
            const double yi = y[static_cast<size_t>( i )];
            if ( !std::isfinite( yi ) )
              continue;
            const double pred = predictSample( fit, i, tDays[static_cast<size_t>( i )], k );
            if ( !std::isfinite( pred ) )
              continue;
            const double d = yi - pred;
            cvSse += d * d;
            ++cvCount;
          }
        }
        if ( anyFoldRan && cvCount > 0 )
        {
          const CandidateFit full = runCandidate( y, tDays, k, b, opts );
          score.fittable = full.fittable;
          score.rss = full.rss;
          score.segments = full.segments;
          score.paramCount =
            full.segments * ( 2 + 2 * k ) + std::max( 0, full.segments - 1 ) + 1;
          score.score = cvSse / static_cast<double>( cvCount );
        }
      }
      else
      {
        const CandidateFit fit = runCandidate( y, tDays, k, b, opts );
        score.fittable = fit.fittable;
        score.rss = fit.rss;
        score.segments = fit.segments;
        score.paramCount =
          fit.segments * ( 2 + 2 * k ) + std::max( 0, fit.segments - 1 ) + 1;
        score.score =
          score.fittable ? scoreFromRss( fit.rss, validTotal, score.paramCount,
                                         opts.penalty )
                         : kNanD;
        // Only NaN disqualifies: a perfect fit legitimately scores -inf
        // (RSS = 0 -> log-likelihood +inf) and wins ties by enumeration
        // order.
        if ( std::isnan( score.score ) )
          score.fittable = false;
      }
      result.candidates.push_back( score );
    }
  }

  // Deterministic selection: first strictly-better candidate wins; exact
  // ties keep the earliest enumerated (harmonics asc, maxBreaks asc) — a
  // parsimony-leaning fixed rule.
  int best = -1;
  for ( size_t i = 0; i < result.candidates.size(); ++i )
  {
    const ModelCandidateScore &c = result.candidates[i];
    if ( !c.fittable )
      continue;
    if ( best < 0 || c.score < result.candidates[static_cast<size_t>( best )].score )
      best = static_cast<int>( i );
  }
  if ( best < 0 )
  {
    result.degradedReason = "no_fittable_candidate";
    return result;
  }
  const ModelCandidateScore &c = result.candidates[static_cast<size_t>( best )];
  result.selected = true;
  result.selectedHarmonics = c.harmonics;
  result.selectedMaxBreaks = c.maxBreaks;
  result.selectedParamCount = c.paramCount;
  result.selectedScore = c.score;
  return result;
}

} // namespace sicnu::temporal
