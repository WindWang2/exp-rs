// src/analysis/hyperspectral/continuum_removal.cpp — convex-hull continuum (D13)
#include "hyperspectral/continuum_removal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace exp_spectral
{
  namespace
  {
    // Absorption-run detection epsilon: values this close to 1.0 count as
    // "on the continuum", so hull-vertex float noise never forms features.
    constexpr double kContinuumEpsilon = 1e-6;

    /// Rc(λ) lives in (0, 1] by construction; anything outside that family
    /// (NaN, NoData sentinels, garbage) is an invalid sample, never absorption.
    bool validRc( double value )
    {
      return std::isfinite( value ) && value > 0.0 && value <= 1.0 + kContinuumEpsilon;
    }

    /// Cross product sign of (a - o) × (b - o); > 0 = counter-clockwise turn.
    double cross( double ox, double oy, double ax, double ay, double bx, double by )
    {
      return ( ax - ox ) * ( by - oy ) - ( ay - oy ) * ( bx - ox );
    }
  } // namespace

  bool ContinuumRemoval::compute( const float *wavelengths, const float *reflectance, size_t bandCount,
                                  float *continuumOut, float *normalizedOut, float noData )
  {
    if ( wavelengths == nullptr || reflectance == nullptr || continuumOut == nullptr ||
         normalizedOut == nullptr || bandCount < 2 )
      return false;

    // Strictly increasing finite grid is part of the seam contract.
    for ( size_t i = 0; i < bandCount; ++i )
    {
      if ( !std::isfinite( wavelengths[i] ) )
        return false;
      if ( i > 0 && !( wavelengths[i] > wavelengths[i - 1] ) )
        return false;
    }

    // Andrew monotone chain — upper hull over valid (λ, R) points. A point is
    // a hull vertex when the slope towards the next point drops; equivalently,
    // walk left-to-right popping every point that bends clockwise (or stays
    // collinear), keeping the chain strictly convex from above.
    std::vector<size_t> hull;
    for ( size_t i = 0; i < bandCount; ++i )
    {
      if ( reflectance[i] == noData || !std::isfinite( reflectance[i] ) )
        continue;
      while ( hull.size() >= 2 &&
              cross( wavelengths[hull[hull.size() - 2]], reflectance[hull[hull.size() - 2]],
                     wavelengths[hull.back()], reflectance[hull.back()],
                     wavelengths[i], reflectance[i] ) >= 0.0 )
      {
        hull.pop_back();
      }
      hull.push_back( i );
    }
    if ( hull.size() < 2 )
      return false; // fewer than two valid points: no envelope exists

    size_t segment = 0;
    for ( size_t i = 0; i < bandCount; ++i )
    {
      // Advance to the hull segment containing wavelengths[i].
      while ( segment + 2 < hull.size() && wavelengths[i] > wavelengths[hull[segment + 1]] )
        ++segment;

      const size_t k = hull[segment];
      const size_t k1 = hull[segment + 1];
      const double dx = wavelengths[k1] - wavelengths[k];
      const double c = dx > 0.0
                         ? reflectance[k] +
                             ( reflectance[k1] - reflectance[k] ) * ( wavelengths[i] - wavelengths[k] ) / dx
                         : reflectance[k];
      continuumOut[i] = static_cast<float>( c );

      if ( reflectance[i] == noData )
      {
        normalizedOut[i] = noData;
        continue;
      }
      normalizedOut[i] = c > 0.0 && std::isfinite( c )
                           ? static_cast<float>( reflectance[i] / c )
                           : std::numeric_limits<float>::quiet_NaN();
    }
    return true;
  }

  std::vector<SpectralAbsorptionFeature> ContinuumRemoval::extractFeatures( const float *wavelengths,
                                                                            const float *normalized,
                                                                            size_t bandCount,
                                                                            double minDepthThreshold )
  {
    std::vector<SpectralAbsorptionFeature> features;
    if ( wavelengths == nullptr || normalized == nullptr || bandCount < 2 )
      return features;

    size_t i = 0;
    while ( i < bandCount )
    {
      // Skip everything at/above the continuum line (and invalid samples).
      if ( !validRc( normalized[i] ) || !( normalized[i] < 1.0f - kContinuumEpsilon ) )
      {
        ++i;
        continue;
      }

      // Contiguous sub-continuum run [start, end]; invalid samples break runs.
      const size_t start = i;
      size_t end = i;
      size_t minIdx = i;
      while ( end + 1 < bandCount && validRc( normalized[end + 1] ) &&
              normalized[end + 1] < 1.0f - kContinuumEpsilon )
      {
        ++end;
        if ( normalized[end] < normalized[minIdx] )
          minIdx = end;
      }

      const double depth = 1.0 - static_cast<double>( normalized[minIdx] );
      if ( depth >= minDepthThreshold )
      {
        SpectralAbsorptionFeature f;
        f.centerWavelengthNm = wavelengths[minIdx];
        f.absorptionDepth = depth;

        // Half-depth reference: Rc = 1 - D/2, located by linear interpolation
        // on each side of the trough minimum.
        const double halfLevel = 1.0 - depth / 2.0;

        auto interpolateCrossing = [&]( size_t from, size_t to, double target ) {
          const double y0 = normalized[from];
          const double y1 = normalized[to];
          if ( std::abs( y1 - y0 ) < 1e-12 )
            return static_cast<double>( wavelengths[to] );
          const double t = ( target - y0 ) / ( y1 - y0 );
          return wavelengths[from] + t * ( wavelengths[to] - wavelengths[from] );
        };

        size_t left = minIdx;
        while ( left > start && static_cast<double>( normalized[left] ) < halfLevel )
          --left;
        f.leftHalfWavelengthNm = left == minIdx
                                   ? wavelengths[minIdx]
                                   : interpolateCrossing( left, left + 1, halfLevel );

        size_t right = minIdx;
        while ( right < end && static_cast<double>( normalized[right] ) < halfLevel )
          ++right;
        f.rightHalfWavelengthNm = right == minIdx
                                    ? wavelengths[minIdx]
                                    : interpolateCrossing( right, right - 1, halfLevel );

        f.fwhmNm = f.rightHalfWavelengthNm - f.leftHalfWavelengthNm;

        // Trough area: trapezoidal integral of (1 - Rc) over the run.
        double area = 0.0;
        for ( size_t k = start; k < end; ++k )
        {
          const double y0 = 1.0 - std::max( 0.0, static_cast<double>( normalized[k] ) );
          const double y1 = 1.0 - std::max( 0.0, static_cast<double>( normalized[k + 1] ) );
          area += 0.5 * ( y0 + y1 ) * ( wavelengths[k + 1] - wavelengths[k] );
        }
        f.bandArea = area;

        // Asymmetry = (λ0 - λleft) / (λright - λ0): 1.0 for a symmetric
        // trough, > 1 when the trough tail extends to the right.
        const double rightWing = f.rightHalfWavelengthNm - f.centerWavelengthNm;
        f.asymmetry = rightWing > 0.0
                        ? ( f.centerWavelengthNm - f.leftHalfWavelengthNm ) / rightWing
                        : 1.0;

        features.push_back( f );
      }
      i = end + 1;
    }
    return features;
  }
} // namespace exp_spectral
