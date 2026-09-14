// src/processing/algorithms/change_detector.cpp — D15 Package E.
#include "change_detector.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rs::processing
{
namespace
{
  std::vector<float> elementwise( std::span<const float> t1, std::span<const float> t2,
                                  float ( *op )( float, float ) )
  {
    if ( t1.size() != t2.size() )
      return {};
    std::vector<float> out( t1.size() );
    for ( size_t i = 0; i < t1.size(); ++i )
      out[i] = op( t1[i], t2[i] );
    return out;
  }

  // Symmetric Jacobi eigendecomposition for small dense matrices:
  // eigenvalues ascending in @p eigenvalues, orthonormal column vectors in
  // @p eigenvectors.  Deterministic (classic cyclic Jacobi sweeps).
  bool jacobiEigen( std::vector<double> a, int n, std::vector<double> &eigenvalues,
                    std::vector<double> &eigenvectors )
  {
    eigenvectors.assign( static_cast<size_t>( n ) * n, 0.0 );
    for ( int i = 0; i < n; ++i )
      eigenvectors[static_cast<size_t>( i ) * n + i] = 1.0;
    for ( int sweep = 0; sweep < 100; ++sweep )
    {
      double off = 0.0;
      for ( int i = 0; i < n; ++i )
        for ( int j = i + 1; j < n; ++j )
          off += a[static_cast<size_t>( i ) * n + j] * a[static_cast<size_t>( i ) * n + j];
      if ( off < 1e-24 )
        break;
      for ( int p = 0; p < n; ++p )
      {
        for ( int q = p + 1; q < n; ++q )
        {
          const double apq = a[static_cast<size_t>( p ) * n + q];
          if ( std::abs( apq ) < 1e-15 )
            continue;
          const double app = a[static_cast<size_t>( p ) * n + p];
          const double aqq = a[static_cast<size_t>( q ) * n + q];
          const double theta = 0.5 * std::atan2( 2.0 * apq, aqq - app );
          const double c = std::cos( theta );
          const double s = std::sin( theta );
          for ( int k = 0; k < n; ++k )
          {
            const double akp = a[static_cast<size_t>( k ) * n + p];
            const double akq = a[static_cast<size_t>( k ) * n + q];
            a[static_cast<size_t>( k ) * n + p] = c * akp - s * akq;
            a[static_cast<size_t>( k ) * n + q] = s * akp + c * akq;
          }
          for ( int k = 0; k < n; ++k )
          {
            const double apk = a[static_cast<size_t>( p ) * n + k];
            const double aqk = a[static_cast<size_t>( q ) * n + k];
            a[static_cast<size_t>( p ) * n + k] = c * apk - s * aqk;
            a[static_cast<size_t>( q ) * n + k] = s * apk + c * aqk;
          }
          for ( int k = 0; k < n; ++k )
          {
            const double vkp = eigenvectors[static_cast<size_t>( k ) * n + p];
            const double vkq = eigenvectors[static_cast<size_t>( k ) * n + q];
            eigenvectors[static_cast<size_t>( k ) * n + p] = c * vkp - s * vkq;
            eigenvectors[static_cast<size_t>( k ) * n + q] = s * vkp + c * vkq;
          }
        }
      }
    }
    eigenvalues.resize( n );
    for ( int i = 0; i < n; ++i )
      eigenvalues[i] = a[static_cast<size_t>( i ) * n + i];
    return true;
  }
} // namespace

std::vector<float> ChangeDetector::computeDifference( std::span<const float> t1, std::span<const float> t2 )
{
  return elementwise( t1, t2, []( float a, float b ) { return b - a; } );
}

std::vector<float> ChangeDetector::computeNormalizedDifference( std::span<const float> t1, std::span<const float> t2 )
{
  if ( t1.size() != t2.size() )
    return {};
  std::vector<float> out( t1.size() );
  for ( size_t i = 0; i < t1.size(); ++i )
    out[i] = ( t2[i] - t1[i] ) / ( t2[i] + t1[i] + 1e-6f );
  return out;
}

std::vector<float> ChangeDetector::computeLogRatio( std::span<const float> t1, std::span<const float> t2,
                                                    float epsilon )
{
  if ( t1.size() != t2.size() )
    return {};
  const float eps = std::max( epsilon, 1e-12f );
  std::vector<float> out( t1.size() );
  for ( size_t i = 0; i < t1.size(); ++i )
    out[i] = std::log( ( t2[i] + eps ) / ( t1[i] + eps ) );
  return out;
}

CvaChangeResult ChangeDetector::computeCva( const float *t1MultiBand, const float *t2MultiBand,
                                            int width, int height, int bands,
                                            float thresholdStdDevMultiplier )
{
  CvaChangeResult result;
  if ( !t1MultiBand || !t2MultiBand || width <= 0 || height <= 0 || bands <= 0 )
    return result;
  const size_t pixels = static_cast<size_t>( width ) * height;
  result.changeMagnitude.resize( pixels );
  result.changeDirectionAngle.assign( pixels, 0.0f );
  result.binaryChangeMask.assign( pixels, 0 );

  for ( size_t p = 0; p < pixels; ++p )
  {
    double sum = 0.0;
    bool finite = true;
    for ( int b = 0; b < bands; ++b )
    {
      const double diff = static_cast<double>( t2MultiBand[b * pixels + p] ) - t1MultiBand[b * pixels + p];
      if ( !std::isfinite( diff ) )
      {
        finite = false;
        break;
      }
      sum += diff * diff;
    }
    if ( !finite )
    {
      result.changeMagnitude[p] = std::numeric_limits<float>::quiet_NaN();
      result.changeDirectionAngle[p] = std::numeric_limits<float>::quiet_NaN();
      continue;
    }
    result.changeMagnitude[p] = static_cast<float>( std::sqrt( sum ) );
    if ( bands >= 2 )
    {
      const double dx0 = static_cast<double>( t2MultiBand[p] ) - t1MultiBand[p];
      const double dx1 = static_cast<double>( t2MultiBand[pixels + p] ) - t1MultiBand[pixels + p];
      double angle = std::atan2( dx1, dx0 );
      if ( angle < 0.0 )
        angle += 2.0 * M_PI;
      result.changeDirectionAngle[p] = static_cast<float>( angle );
    }
  }

  // Adaptive threshold over finite magnitudes only (population stddev).
  double mean = 0.0;
  size_t finiteCount = 0;
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( std::isfinite( result.changeMagnitude[p] ) )
    {
      mean += result.changeMagnitude[p];
      ++finiteCount;
    }
  }
  if ( finiteCount == 0 )
    return result;
  mean /= static_cast<double>( finiteCount );
  double variance = 0.0;
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( !std::isfinite( result.changeMagnitude[p] ) )
      continue;
    const double d = result.changeMagnitude[p] - mean;
    variance += d * d;
  }
  variance /= static_cast<double>( finiteCount );
  const double threshold = mean + static_cast<double>( thresholdStdDevMultiplier ) * std::sqrt( variance );
  result.computedThreshold = static_cast<float>( threshold );
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( std::isfinite( result.changeMagnitude[p] ) && result.changeMagnitude[p] >= result.computedThreshold )
      result.binaryChangeMask[p] = 1;
  }
  return result;
}

std::vector<float> ChangeDetector::computePcaDifference( const float *t1MultiBand, const float *t2MultiBand,
                                                         int width, int height, int bands )
{
  if ( !t1MultiBand || !t2MultiBand || width <= 0 || height <= 0 || bands <= 0 )
    return {};
  const size_t pixels = static_cast<size_t>( width ) * height;

  std::vector<double> mean( bands, 0.0 );
  for ( size_t p = 0; p < pixels; ++p )
    for ( int b = 0; b < bands; ++b )
    {
      const double diff = static_cast<double>( t2MultiBand[b * pixels + p] ) - t1MultiBand[b * pixels + p];
      if ( !std::isfinite( diff ) )
        return std::vector<float>( pixels, std::numeric_limits<float>::quiet_NaN() );
      mean[b] += diff;
    }
  for ( int b = 0; b < bands; ++b )
    mean[b] /= static_cast<double>( pixels );

  std::vector<double> covariance( static_cast<size_t>( bands ) * bands, 0.0 );
  std::vector<double> centered( bands, 0.0 );
  for ( size_t p = 0; p < pixels; ++p )
  {
    for ( int b = 0; b < bands; ++b )
      centered[b] = ( t2MultiBand[b * pixels + p] - t1MultiBand[b * pixels + p] ) - mean[b];
    for ( int a = 0; a < bands; ++a )
      for ( int c = a; c < bands; ++c )
      {
        covariance[static_cast<size_t>( a ) * bands + c] += centered[a] * centered[c] / pixels;
        covariance[static_cast<size_t>( c ) * bands + a] =
          covariance[static_cast<size_t>( a ) * bands + c];
      }
  }

  std::vector<double> eigenvalues, eigenvectors;
  jacobiEigen( covariance, bands, eigenvalues, eigenvectors );

  // Minor component = smallest eigenvalue; sign convention: the largest-|
  // component| entry is positive (so scores are comparable run to run).
  const int minor = static_cast<int>( std::min_element( eigenvalues.begin(), eigenvalues.end() )
                                      - eigenvalues.begin() );
  int pivot = 0;
  double pivotAbs = -1.0;
  for ( int r = 0; r < bands; ++r )
  {
    const double v = eigenvectors[static_cast<size_t>( r ) * bands + minor];
    if ( std::abs( v ) > pivotAbs )
    {
      pivotAbs = std::abs( v );
      pivot = r;
    }
  }
  const double sign = eigenvectors[static_cast<size_t>( pivot ) * bands + minor] >= 0.0 ? 1.0 : -1.0;

  std::vector<float> scores( pixels, 0.0f );
  for ( size_t p = 0; p < pixels; ++p )
  {
    double projection = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
      const double diff = ( t2MultiBand[b * pixels + p] - t1MultiBand[b * pixels + p] ) - mean[b];
      projection += sign * eigenvectors[static_cast<size_t>( b ) * bands + minor] * diff;
    }
    scores[p] = static_cast<float>( std::abs( projection ) );
  }
  return scores;
}

} // namespace rs::processing
