// src/processing/algorithms/temporal/temporal_change.cpp
#include "temporal_change.h"

#include "temporal_fit.h"
#include "temporal_linalg_detail.h"
#include "temporal_design_detail.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr double kNanD = std::numeric_limits<double>::quiet_NaN();
constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();

using detail::kMaxTerms;
using detail::harmonicTrendDesignRow;

// Per-call fit scratch, reused across pixels/segments (Temporal Intelligence
// 11.0 performance pass): the per-pixel loop used to heap-allocate the Gram,
// dense copy and solution vectors on every fitSegment call. Reassigning the
// buffers keeps the arithmetic (accumulation order, eliminations) EXACTLY
// unchanged — only the allocations go away. thread_local keeps the kernel
// single-threaded-contract safe (each thread reuses its own buffers).
struct FitScratch
{
  std::vector<double> ata;
  std::vector<double> atb;
  std::vector<double> aDense;
  std::vector<double> bDense;
  std::vector<double> coef;
};
thread_local FitScratch tFitScratch;

/// One weighted OLS fit of the harmonic+trend design on [a, b) of y/t.
/// Returns false when the segment holds fewer valid samples than terms or
/// the system is singular (underdetermined segment).
bool fitSegment( const std::vector<float> &y, const std::vector<double> &tDays,
                 int a, int b, int harmonics, const std::vector<double> &weights,
                 std::vector<double> *coefOut, double *sseOut, int *validOut,
                 double *slopeOut, double *interceptOut )
{
  FitScratch &scratch = tFitScratch;
  scratch.ata.assign( static_cast<size_t>( kMaxTerms ) * kMaxTerms, 0.0 );
  scratch.atb.assign( kMaxTerms, 0.0 );
  std::vector<double> &ata = scratch.ata;
  std::vector<double> &atb = scratch.atb;
  double design[kMaxTerms];
  const int terms = 2 + 2 * harmonics;
  int valid = 0;
  for ( int i = a; i < b; ++i )
  {
    const double w = weights[i];
    if ( w <= 0.0 )
      continue;
    const int m = harmonicTrendDesignRow( tDays[i], harmonics, design );
    for ( int r = 0; r < m; ++r )
    {
      atb[static_cast<size_t>( r )] += w * design[r] * y[i];
      for ( int c = 0; c < m; ++c )
        ata[static_cast<size_t>( r ) * kMaxTerms + c] += w * design[r] * design[c];
    }
    ++valid;
  }
  if ( valid < terms )
    return false;
  // solveSmallDense consumes dense n×n; compress the kMaxTerms-strided Gram.
  scratch.aDense.assign( static_cast<size_t>( terms ) * terms, 0.0 );
  scratch.bDense.assign( terms, 0.0 );
  for ( int r = 0; r < terms; ++r )
  {
    scratch.bDense[static_cast<size_t>( r )] = atb[static_cast<size_t>( r )];
    for ( int c = 0; c < terms; ++c )
      scratch.aDense[static_cast<size_t>( r ) * terms + c] =
        ata[static_cast<size_t>( r ) * kMaxTerms + c];
  }
  // solveSmallDense takes the system by value: passing the scratch buffers
  // copies them (same values as before), while their capacity stays warm
  // for the next per-pixel call.
  if ( !detail::solveSmallDense( scratch.aDense, scratch.bDense, terms,
                                 &scratch.coef ) )
    return false;
  const std::vector<double> &coef = scratch.coef;
  double sse = 0.0;
  for ( int i = a; i < b; ++i )
  {
    const double w = weights[i];
    if ( w <= 0.0 )
      continue;
    const int m = harmonicTrendDesignRow( tDays[i], harmonics, design );
    double v = 0.0;
    for ( int r = 0; r < m; ++r )
      v += coef[static_cast<size_t>( r )] * design[r];
    sse += w * ( y[i] - v ) * ( y[i] - v );
  }
  if ( coefOut )
    *coefOut = coef;
  if ( sseOut )
    *sseOut = sse;
  if ( validOut )
    *validOut = valid;
  if ( slopeOut )
    *slopeOut = coef[1]; // t column
  if ( interceptOut )
    *interceptOut = coef[0];
  return true;
}

using detail::evalHarmonicTrend;
} // namespace

SeasonalTrendBreaksResult fitSeasonalTrendBreaks( const std::vector<float> &y,
                                                  const std::vector<double> &tDays,
                                                  int harmonics, int maxBreaks,
                                                  int minSegment, double minImprovement,
                                                  bool robust )
{
  SeasonalTrendBreaksResult result;
  const int n = static_cast<int>( y.size() );
  result.fitted.assign( static_cast<size_t>( n ), kNanF );
  if ( n < 4 || static_cast<int>( tDays.size() ) != n )
  {
    result.rmse = kNanD;
    result.r2 = kNanD;
    return result;
  }
  const int harm = std::clamp( harmonics, 1, 3 );
  const int maxSeg = std::clamp( maxBreaks, 0, 8 ) + 1;
  const int minSeg = std::max( 3, minSegment );
  const int terms = 2 + 2 * harm;

  std::vector<double> weights( static_cast<size_t>( n ), 0.0 );
  long totalValid = 0;
  double meanY = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    if ( std::isfinite( y[i] ) )
    {
      weights[i] = 1.0;
      ++totalValid;
      meanY += y[i];
    }
  }
  result.validCount = static_cast<int>( totalValid );
  if ( totalValid < terms )
  {
    result.rmse = kNanD;
    result.r2 = kNanD;
    return result;
  }
  meanY /= static_cast<double>( totalValid );

  // Segment set: [start, end) index pairs; initially the whole series.
  std::vector<std::pair<int, int>> segments{ { 0, n } };
  std::vector<int> breakIndices;

  constexpr int kMaxIterations = 3;
  for ( int iter = 0; iter < kMaxIterations; ++iter )
  {
    result.iterations = iter + 1;
    // (a) Fit every segment; residual = y − per-segment fit.
    std::vector<float> residual( static_cast<size_t>( n ), kNanF );
    bool anyFit = false;
    for ( const auto &seg : segments )
    {
      std::vector<double> coef;
      if ( !fitSegment( y, tDays, seg.first, seg.second, harm, weights, &coef,
                        nullptr, nullptr, nullptr, nullptr ) )
        continue;
      anyFit = true;
      for ( int i = seg.first; i < seg.second; ++i )
      {
        if ( weights[i] <= 0.0 )
          continue;
        residual[static_cast<size_t>( i )] =
          static_cast<float>( y[i] - evalHarmonicTrend( coef, tDays[i], harm ) );
      }
    }
    if ( !anyFit )
      break;

    // (b) Trend breaks of the seasonality-adjusted residual. The remaining
    // break budget bounds the split count; the greedy RSS search is the
    // shared piecewiseLinearTrend kernel (same semantics as the linear
    // rs:temporal_breakpoints operator).
    const int budget = maxSeg - 1 - static_cast<int>( breakIndices.size() );
    if ( budget <= 0 )
      break;
    const BreakpointResult splits =
      piecewiseLinearTrend( residual, tDays, budget, minSeg, minImprovement );
    if ( splits.breakIndices.empty() )
      break;

    // (c) Merge + re-split; stop when nothing moved.
    std::vector<int> merged( breakIndices );
    merged.insert( merged.end(), splits.breakIndices.begin(), splits.breakIndices.end() );
    std::sort( merged.begin(), merged.end() );
    merged.erase( std::unique( merged.begin(), merged.end() ), merged.end() );
    // Enforce the min-segment constraint after merging (residual breaks may
    // sit inside previously fitted short segments).
    std::vector<int> enforced;
    int prev = 0;
    for ( int b : merged )
    {
      if ( b - prev >= minSeg && n - b >= minSeg )
      {
        enforced.push_back( b );
        prev = b;
      }
    }
    if ( enforced.size() == breakIndices.size() &&
         std::equal( enforced.begin(), enforced.end(), breakIndices.begin() ) )
      break;
    breakIndices = enforced;
    segments.clear();
    int start = 0;
    for ( int b : breakIndices )
    {
      segments.push_back( { start, b } );
      start = b;
    }
    segments.push_back( { start, n } );
    if ( static_cast<int>( breakIndices.size() ) >= maxSeg - 1 )
      break; // break budget spent (segments = breaks + 1)
  }

  // Backward pruning: the residual-driven search can propose splits that
  // only explain the GLOBAL fit's mis-fit (a false break). Remove, greedily
  // and deterministically, any break whose removal raises the TOTAL kept SSE
  // by less than minImprovement × SSE(kept). The denominator is the total
  // (documented here), deliberately coarser than the forward per-segment
  // rule; a configuration where any segment cannot fit (fewer valid samples
  // than model terms) is treated as unacceptable (+infinity), so pruning
  // never favors a layout that hides unfittable segments.
  {
    constexpr double kUnfittable = std::numeric_limits<double>::infinity();
    auto totalSseFor = [&]( const std::vector<int> &cuts ) {
      double sse = 0.0;
      int prev = 0;
      for ( int b : cuts )
      {
        double s = 0.0;
        if ( !fitSegment( y, tDays, prev, b, harm, weights, nullptr, &s,
                          nullptr, nullptr, nullptr ) )
          return kUnfittable;
        sse += std::max( 0.0, s );
        prev = b;
      }
      double s = 0.0;
      if ( !fitSegment( y, tDays, prev, n, harm, weights, nullptr, &s,
                        nullptr, nullptr, nullptr ) )
        return kUnfittable;
      return sse + std::max( 0.0, s );
    };
    std::vector<int> cuts = breakIndices;
    while ( !cuts.empty() )
    {
      const double sseWith = totalSseFor( cuts );
      double bestIncrease = 0.0;
      size_t bestIdx = cuts.size();
      for ( size_t k = 0; k < cuts.size(); ++k )
      {
        std::vector<int> without( cuts );
        without.erase( without.begin() + static_cast<std::ptrdiff_t>( k ) );
        const double increase = totalSseFor( without ) - sseWith;
        if ( bestIdx == cuts.size() || increase < bestIncrease )
        {
          bestIncrease = increase;
          bestIdx = k;
        }
      }
      // Drop the least-missed break while its removal is within the same
      // relative-improvement budget the forward pass demanded.
      if ( bestIdx < cuts.size() && bestIncrease <= minImprovement * sseWith )
        cuts.erase( cuts.begin() + static_cast<std::ptrdiff_t>( bestIdx ) );
      else
        break;
    }
    if ( cuts.size() != breakIndices.size() )
    {
      breakIndices = cuts;
      segments.clear();
      int start = 0;
      for ( int b : breakIndices )
      {
        segments.push_back( { start, b } );
        start = b;
      }
      segments.push_back( { start, n } );
    }
  }

  // Final per-segment refit (optionally robust IRLS) + stats. Coefficients
  // are kept per segment so break magnitudes evaluate BOTH models AT the
  // break day (no differential-trend leak from adjacent fitted samples).
  std::vector<std::vector<double>> segCoefByIndex( segments.size() );
  result.segmentCoefficients.assign( segments.size(), {} );
  double totalSse = 0.0;
  double totalSst = 0.0;
  long totalValidFinal = 0;
  for ( int i = 0; i < n; ++i )
  {
    if ( weights[i] > 0.0 )
      totalSst += ( y[i] - meanY ) * ( y[i] - meanY );
  }
  size_t segIndex = 0;
  for ( const auto &seg : segments )
  {
    // Weights for the final fit: optional Huber IRLS within the segment.
    std::vector<double> segWeights( weights );
    std::vector<double> coef;
    double sse = 0.0;
    int valid = 0;
    double slope = 0.0;
    double intercept = 0.0;
    bool ok = fitSegment( y, tDays, seg.first, seg.second, harm, segWeights, &coef,
                          &sse, &valid, &slope, &intercept );
    if ( ok && robust )
    {
      for ( int ir = 0; ir < 3; ++ir )
      {
        std::vector<double> absRes;
        absRes.reserve( static_cast<size_t>( valid ) );
        for ( int i = seg.first; i < seg.second; ++i )
        {
          if ( segWeights[i] <= 0.0 )
            continue;
          absRes.push_back( std::abs( y[i] - evalHarmonicTrend( coef, tDays[i], harm ) ) );
        }
        if ( absRes.empty() )
          break;
        std::sort( absRes.begin(), absRes.end() );
        const double scale = 1.4826 * absRes[absRes.size() / 2];
        const double delta = scale > 1e-9 ? 1.5 * scale : 1e6;
        for ( int i = seg.first; i < seg.second; ++i )
        {
          if ( segWeights[i] <= 0.0 )
            continue;
          const double r = std::abs( y[i] - evalHarmonicTrend( coef, tDays[i], harm ) );
          segWeights[i] = r <= delta ? 1.0 : delta / r;
        }
        ok = fitSegment( y, tDays, seg.first, seg.second, harm, segWeights, &coef,
                         &sse, &valid, &slope, &intercept );
        if ( !ok )
          break;
      }
    }
    SeasonalTrendSegment out;
    out.startIndex = seg.first;
    out.endIndex = seg.second;
    out.startDays = tDays[static_cast<size_t>( seg.first )];
    out.endDays = tDays[static_cast<size_t>( seg.second - 1 )];
    if ( ok )
    {
      out.slopePerDay = slope;
      out.intercept = intercept;
      out.rmse = valid > 0 ? std::sqrt( std::max( 0.0, sse ) / valid ) : kNanD;
      out.validCount = valid;
      segCoefByIndex[static_cast<size_t>( segIndex )] = coef;
      result.segmentCoefficients[static_cast<size_t>( segIndex )] = coef;
      for ( int i = seg.first; i < seg.second; ++i )
      {
        if ( weights[i] <= 0.0 )
          continue;
        result.fitted[static_cast<size_t>( i )] =
          static_cast<float>( evalHarmonicTrend( coef, tDays[i], harm ) );
      }
      totalSse += std::max( 0.0, sse );
      totalValidFinal += valid;
    }
    else
    {
      // No fit (underdetermined segment): the honest report is NaN, never a
      // default-zero slope masquerading as a flat trend.
      out.slopePerDay = kNanD;
      out.intercept = kNanD;
      out.rmse = kNanD;
    }
    result.segments.push_back( out );
    ++segIndex;
  }

  result.breaks.reserve( breakIndices.size() );
  for ( size_t s = 1; s < segments.size(); ++s )
  {
    const int idx = segments[s].first;
    // Magnitude = |fitL(tBreak) − fitR(tBreak)|: both neighbouring segments'
    // models evaluated AT the break day (undefined when either side has no
    // stored fit).
    BreakEvent ev;
    ev.index = idx;
    ev.breakDays = tDays[static_cast<size_t>( idx )];
    const std::vector<double> &coefL = segCoefByIndex[s - 1];
    const std::vector<double> &coefR = segCoefByIndex[s];
    ev.magnitude =
      ( !coefL.empty() && !coefR.empty() )
        ? std::abs( evalHarmonicTrend( coefR, ev.breakDays, harm ) -
                    evalHarmonicTrend( coefL, ev.breakDays, harm ) )
        : kNanD;
    result.breaks.push_back( ev );
  }

  result.rmse = totalValidFinal > 0
                  ? std::sqrt( totalSse / static_cast<double>( totalValidFinal ) )
                  : kNanD;
  result.r2 = ( totalValidFinal > 0 && totalSst > 0.0 )
                ? 1.0 - totalSse / totalSst
                : kNanD;
  return result;
}

double disturbanceOnset( const std::vector<float> &fitted,
                         const std::vector<double> &tDays,
                         const std::vector<BreakEvent> &breaks,
                         bool decrease, double minMagnitude,
                         double *recoveryDaysOut, double recoveryTolerance )
{
  if ( recoveryDaysOut )
    *recoveryDaysOut = kNanD;
  const int n = static_cast<int>( fitted.size() );
  if ( n == 0 || static_cast<int>( tDays.size() ) != n )
    return -1.0;

  for ( const auto &ev : breaks )
  {
    if ( !( ev.magnitude >= minMagnitude ) )
      continue; // NaN magnitude or below threshold
    // Level jump direction: fit after the break minus fit before it.
    double fitL = kNanD;
    for ( int i = ev.index - 1; i >= 0; --i )
    {
      if ( std::isfinite( fitted[static_cast<size_t>( i )] ) )
      {
        fitL = fitted[static_cast<size_t>( i )];
        break;
      }
    }
    double fitR = kNanD;
    for ( int i = ev.index; i < n; ++i )
    {
      if ( std::isfinite( fitted[static_cast<size_t>( i )] ) )
      {
        fitR = fitted[static_cast<size_t>( i )];
        break;
      }
    }
    if ( !std::isfinite( fitL ) || !std::isfinite( fitR ) )
      continue;
    const double jump = fitR - fitL;
    const bool isOnset = decrease ? jump < 0.0 : jump > 0.0;
    if ( !isOnset )
      continue;

    // First fitted return to the pre-break level within tolerance.
    const double target = decrease ? fitL - recoveryTolerance
                                   : fitL + recoveryTolerance;
    for ( int i = ev.index; i < n; ++i )
    {
      if ( !std::isfinite( fitted[static_cast<size_t>( i )] ) )
        continue;
      const bool recovered = decrease ? fitted[static_cast<size_t>( i )] >= target
                                      : fitted[static_cast<size_t>( i )] <= target;
      if ( recovered )
      {
        if ( recoveryDaysOut )
          *recoveryDaysOut = tDays[static_cast<size_t>( i )] - ev.breakDays;
        return ev.breakDays;
      }
    }
    if ( recoveryDaysOut )
      *recoveryDaysOut = -1.0; // never recovered within the series
    return ev.breakDays;
  }
  return -1.0;
}

} // namespace sicnu::temporal
