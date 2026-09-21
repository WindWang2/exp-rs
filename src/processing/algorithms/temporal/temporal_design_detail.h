// src/processing/algorithms/temporal/temporal_design_detail.h
// Shared harmonic+trend design row for the temporal kernels (detail
// namespace, not public API). Extracted from temporal_change.cpp so the
// model-selection/attribution kernels (temporal_selection.h) and the
// uncertainty kernel (temporal_uncertainty.h) evaluate the SAME basis as
// the joint segmentation instead of growing second copies.
//
// Design row: [1, t, sin(1·ωt), cos(1·ωt), ..., sin(h·ωt), cos(h·ωt)],
// ωk = 2π·k·t / 365.25 (the platform-locked harmonic period).
#pragma once

#include <cmath>
#include <vector>

namespace sicnu::temporal::detail
{

constexpr double kDesignPi = 3.14159265358979323846;

/// Max design width: intercept + t + 2·3 harmonics.
constexpr int kMaxTerms = 1 + 1 + 2 * 3;

/// Max design width for the no-trend harmonic design used by harmonicFit:
/// intercept + 2·6 harmonics (temporal_fit.h clamps harmonics to [1, 6]).
constexpr int kMaxHarmonicTerms = 1 + 2 * 6;

/// Fills @a design with the row for day offset @a t and @a harmonics
/// sin/cos pairs; returns the number of used leading columns.
inline int harmonicTrendDesignRow( double t, int harmonics, double *design )
{
  design[0] = 1.0;
  design[1] = t;
  for ( int k = 1; k <= harmonics; ++k )
  {
    const double omega = 2.0 * kDesignPi * k * t / 365.25;
    design[2 + 2 * ( k - 1 )] = std::sin( omega );
    design[3 + 2 * ( k - 1 )] = std::cos( omega );
  }
  return 2 + 2 * harmonics;
}

/// Evaluates coefficients from harmonicTrendDesignRow at @a t.
inline double evalHarmonicTrend( const std::vector<double> &coef, double t,
                                 int harmonics )
{
  double design[kMaxTerms];
  const int m = harmonicTrendDesignRow( t, harmonics, design );
  double v = 0.0;
  for ( int r = 0; r < m; ++r )
    v += coef[r] * design[r];
  return v;
}

/// Design row WITHOUT the trend column — [1, sin(ωt), cos(ωt), …,
/// sin(h·ωt), cos(h·ωt)]: the exact basis harmonicFit (temporal_fit.h)
/// solves. Returns 1 + 2·harmonics.
inline int harmonicDesignRow( double t, int harmonics, double *design )
{
  design[0] = 1.0;
  for ( int k = 1; k <= harmonics; ++k )
  {
    const double omega = 2.0 * kDesignPi * k * t / 365.25;
    design[2 * k - 1] = std::sin( omega );
    design[2 * k] = std::cos( omega );
  }
  return 1 + 2 * harmonics;
}

} // namespace sicnu::temporal::detail
