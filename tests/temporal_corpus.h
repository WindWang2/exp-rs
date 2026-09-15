// tests/temporal_corpus.h — deterministic synthetic scenario corpus for the
// Temporal Intelligence 11.0 tests (package H: known breaks / double season /
// missing / anomalies / no-change negative controls, cross-year windows).
//
// Oracle independence: every scenario's expectation is derivable from the
// closed-form generator below, never from the code under test. The only
// randomness is fixed-seed mt19937 noise/masking (Box–Muller); the engine
// is standard-pinned, the uniform mapping is not (implementation-defined
// per the C standard) — seeds reproduce on one toolchain, which is the
// determinism these tests assert.
//
// Grid convention: synthetic 365-day years (no leap days); day offsets
// tDays = cadence·i; the harmonic period used by both truth and model is the
// platform-locked 365.25 days (a documented constant, not an implementation
// detail — a 365-day truth would mismatch the model basis).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace temporal_corpus
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kHarmonicPeriod = 365.25;  // platform-locked constant
constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();

struct Grid
{
  std::vector<double> tDays;
  std::vector<int> doyOf;   // 1..365
  std::vector<int> yearOf;
};

/// Regular grid: n samples at a fixed cadence starting (startYear, startDoy).
inline Grid makeGrid( int n, int cadenceDays = 16, int startYear = 2020,
                      int startDoy = 1 )
{
  Grid g;
  g.tDays.reserve( static_cast<size_t>( n ) );
  g.doyOf.reserve( static_cast<size_t>( n ) );
  g.yearOf.reserve( static_cast<size_t>( n ) );
  for ( int i = 0; i < n; ++i )
  {
    const int dayNumber = ( startDoy - 1 ) + cadenceDays * i;  // 0-based from year start
    g.tDays.push_back( cadenceDays * static_cast<double>( i ) );
    g.doyOf.push_back( dayNumber % 365 + 1 );
    g.yearOf.push_back( startYear + dayNumber / 365 );
  }
  return g;
}

/// Deterministic standard-normal sample (Box–Muller; fixed-seed engine
/// supplied by the caller so a scenario is fully determined by its seed).
struct Gaussian
{
  std::mt19937 engine;
  bool haveSpare = false;
  double spare = 0.0;
  explicit Gaussian( uint32_t seed ) : engine( seed ) {}
  double operator()()
  {
    if ( haveSpare )
    {
      haveSpare = false;
      return spare;
    }
    double u1 = 0.0;
    while ( u1 <= 0.0 )
    {
      std::uniform_real_distribution<double> uniform( 0.0, 1.0 );
      u1 = uniform( engine );
    }
    const double u2 = std::uniform_real_distribution<double>( 0.0, 1.0 )( engine );
    const double r = std::sqrt( -2.0 * std::log( u1 ) );
    spare = r * std::sin( 2.0 * kPi * u2 );
    haveSpare = true;
    return r * std::cos( 2.0 * kPi * u2 );
  }
};

struct Scenario
{
  std::string name;
  Grid grid;
  std::vector<float> y;
  // Closed-form expectations (sample index space; -1 = none).
  int trendBreakSample = -1;
  int seasonalBreakSample = -1;
  int peakDoy1 = -1;           // primary seasonal peak doy (phenology)
  int peakDoy2 = -1;           // secondary peak doy (double season), -1 = none
  int expectedCyclesPerYear = 1;
};

/// Annual sinusoid with amplitude/phase change at breakSample (mean held at
/// `base` — a pure seasonal change, no level step).
inline Scenario seasonalAmplitudeBreak( int n, int breakSample, double base,
                                        double ampBefore, double ampAfter,
                                        double sigma, uint32_t seed )
{
  Scenario s;
  s.name = "seasonal_amplitude_break";
  s.grid = makeGrid( n );
  s.seasonalBreakSample = breakSample;
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
  {
    const double t = s.grid.tDays[static_cast<size_t>( i )];
    const double amp = i < breakSample ? ampBefore : ampAfter;
    s.y[static_cast<size_t>( i )] =
      static_cast<float>( base + amp * std::sin( 2.0 * kPi * t / kHarmonicPeriod ) +
                          sigma * gauss() );
  }
  return s;
}

/// Annual sinusoid with a phase jump (phiBefore → phiAfter radians) at
/// breakSample, constant amplitude — a seasonal-shape change without any
/// level or slope change.
inline Scenario seasonalPhaseBreak( int n, int breakSample, double base,
                                    double amp, double phiBefore,
                                    double phiAfter, double sigma,
                                    uint32_t seed )
{
  Scenario s;
  s.name = "seasonal_phase_break";
  s.grid = makeGrid( n );
  s.seasonalBreakSample = breakSample;
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
  {
    const double t = s.grid.tDays[static_cast<size_t>( i )];
    const double phi = i < breakSample ? phiBefore : phiAfter;
    s.y[static_cast<size_t>( i )] =
      static_cast<float>( base + amp * std::sin( 2.0 * kPi * t / kHarmonicPeriod + phi ) +
                          sigma * gauss() );
  }
  return s;
}

/// Flat baseline with a level step at breakSample (no seasonality at all).
inline Scenario trendBreakOnly( int n, int breakSample, double base,
                                double step, double sigma, uint32_t seed )
{
  Scenario s;
  s.name = "trend_break_only";
  s.grid = makeGrid( n );
  s.trendBreakSample = breakSample;
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
    s.y[static_cast<size_t>( i )] = static_cast<float>(
      base + ( i < breakSample ? 0.0 : step ) + sigma * gauss() );
  return s;
}

/// Level step AND amplitude change at the same sample.
inline Scenario bothBreaks( int n, int breakSample, double base, double step,
                            double ampBefore, double ampAfter, double sigma,
                            uint32_t seed )
{
  Scenario s;
  s.name = "both_breaks";
  s.grid = makeGrid( n );
  s.trendBreakSample = breakSample;
  s.seasonalBreakSample = breakSample;
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
  {
    const double t = s.grid.tDays[static_cast<size_t>( i )];
    const double amp = i < breakSample ? ampBefore : ampAfter;
    s.y[static_cast<size_t>( i )] =
      static_cast<float>( base + ( i < breakSample ? 0.0 : step ) +
                          amp * std::sin( 2.0 * kPi * t / kHarmonicPeriod ) +
                          sigma * gauss() );
  }
  return s;
}

/// Negative control: stationary seasonality + slow linear trend, no break.
inline Scenario noChangeControl( int n, double base, double amp, double slopePerDay,
                                 double sigma, uint32_t seed )
{
  Scenario s;
  s.name = "no_change_control";
  s.grid = makeGrid( n );
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
  {
    const double t = s.grid.tDays[static_cast<size_t>( i )];
    s.y[static_cast<size_t>( i )] =
      static_cast<float>( base + slopePerDay * t +
                          amp * std::sin( 2.0 * kPi * t / kHarmonicPeriod ) +
                          sigma * gauss() );
  }
  return s;
}

/// Pure harmonic ladder with two frequencies (model-selection truth:
/// harmonic order 2), no breaks.
inline Scenario harmonicOrder2( int n, double base, double a1, double a2,
                                double sigma, uint32_t seed )
{
  Scenario s;
  s.name = "harmonic_order2";
  s.grid = makeGrid( n );
  s.y.resize( static_cast<size_t>( n ) );
  Gaussian gauss( seed );
  for ( int i = 0; i < n; ++i )
  {
    const double t = s.grid.tDays[static_cast<size_t>( i )];
    s.y[static_cast<size_t>( i )] =
      static_cast<float>( base + a1 * std::sin( 2.0 * kPi * t / kHarmonicPeriod ) +
                          a2 * std::sin( 4.0 * kPi * t / kHarmonicPeriod ) +
                          sigma * gauss() );
  }
  return s;
}

/// Double-cropping system: |sin| shape → two equal peaks per year.
inline Scenario doubleSeason( int years, double base, double amp, double sigma,
                              uint32_t seed )
{
  Scenario s;
  s.name = "double_season";
  s.grid = makeGrid( years * 23 );
  s.expectedCyclesPerYear = 2;
  s.peakDoy1 = 92;   // |sin| peaks at quarter and three-quarter year
  s.peakDoy2 = 275;
  s.y.resize( s.grid.tDays.size() );
  Gaussian gauss( seed );
  for ( size_t i = 0; i < s.y.size(); ++i )
  {
    const double t = s.grid.tDays[i];
    s.y[i] = static_cast<float>(
      base + amp * std::abs( std::sin( 2.0 * kPi * t / kHarmonicPeriod ) ) +
      sigma * gauss() );
  }
  return s;
}

/// Single winter-peaking season (peak near doy 15): seasons wrap the year
/// end — the cross-year truth for harvest-year assignment.
inline Scenario winterCrossYear( int years, double base, double amp,
                                 double sigma, uint32_t seed )
{
  Scenario s;
  s.name = "winter_cross_year";
  s.grid = makeGrid( years * 23 );
  s.expectedCyclesPerYear = 1;
  s.peakDoy1 = 15;
  s.y.resize( s.grid.tDays.size() );
  Gaussian gauss( seed );
  for ( size_t i = 0; i < s.y.size(); ++i )
  {
    const double t = s.grid.tDays[i];
    // cos(2π(t−14)/365.25) peaks at day 14 → doy ≈ 15 of the grid year.
    s.y[i] = static_cast<float>(
      base + amp * std::cos( 2.0 * kPi * ( t - 14.0 ) / kHarmonicPeriod ) +
      sigma * gauss() );
  }
  return s;
}

/// Single season peaking in LATE DECEMBER (peak doy ~360): the season's
/// window end falls in the NEXT calendar year, so the harvest-year rule
/// (seasonYear = year of window end) is observably different from the
/// peak's own calendar year.
inline Scenario decemberPeak( int years, double base, double amp, double sigma,
                              uint32_t seed )
{
  Scenario s;
  s.name = "december_peak";
  s.grid = makeGrid( years * 23 );
  s.expectedCyclesPerYear = 1;
  s.peakDoy1 = 360;
  s.y.resize( s.grid.tDays.size() );
  Gaussian gauss( seed );
  for ( size_t i = 0; i < s.y.size(); ++i )
  {
    const double t = s.grid.tDays[i];
    // cos peaks where t ≡ 359 (mod 365.25) → late December.
    s.y[i] = static_cast<float>(
      base + amp * std::cos( 2.0 * kPi * ( t - 359.0 ) / kHarmonicPeriod ) +
      sigma * gauss() );
  }
  return s;
}

/// Masks each finite sample to NaN with probability p (seeded).
inline void applyMissing( Scenario &s, double dropProbability, uint32_t seed )
{
  std::mt19937 engine( seed );
  std::uniform_real_distribution<double> uniform( 0.0, 1.0 );
  for ( float &v : s.y )
    if ( std::isfinite( v ) && uniform( engine ) < dropProbability )
      v = kNanF;
}

/// Adds positive spikes at every `stride`-th valid sample (anomaly control).
inline void applySpikes( Scenario &s, int stride, float spike )
{
  int seen = 0;
  for ( float &v : s.y )
  {
    if ( !std::isfinite( v ) )
      continue;
    if ( seen % stride == 0 )
      v += spike;
    ++seen;
  }
}

} // namespace temporal_corpus
