// primitives/distance_transform.cpp — see distance_transform.h.

#include "distance_transform.h"

#include <algorithm>
#include <cmath>

namespace sicnu::rs::primitives
{
namespace
{

/// Squared-distance sentinel for "no source seen". Large enough to dominate
/// any real squared distance on integer grids (d² <= ~1e12 for this
/// platform's raster sizes) while staying far from double overflow, so the
/// lower-envelope arithmetic stays finite — an actual infinity would poison
/// the intersection tests. Unreached cells are mapped back to float infinity
/// in the final conversion.
constexpr double kUnreached = 1e30;

/// 1D squared-distance lower-envelope pass (Felzenszwalb & Huttenlocher
/// 2012, distance transform of a sampled function). In-place over @a f
/// (@a n values); @a z needs @a n + 1 slots, @a v needs @a n slots.
void squaredDistancePass( double *f, double *z, size_t *v, size_t n )
{
  if ( n == 0 )
    return;
  size_t k = 0;
  v[0] = 0;
  z[0] = -kUnreached;
  z[1] = kUnreached;
  for ( size_t q = 1; q < n; ++q )
  {
    double s = ( ( f[q] + static_cast<double>( q ) * q ) -
                 ( f[v[k]] + static_cast<double>( v[k] ) * static_cast<double>( v[k] ) ) ) /
               ( 2.0 * static_cast<double>( q ) - 2.0 * static_cast<double>( v[k] ) );
    while ( s <= z[k] )
    {
      --k;
      s = ( ( f[q] + static_cast<double>( q ) * q ) -
            ( f[v[k]] + static_cast<double>( v[k] ) * static_cast<double>( v[k] ) ) ) /
          ( 2.0 * static_cast<double>( q ) - 2.0 * static_cast<double>( v[k] ) );
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = kUnreached;
  }
  size_t j = 0;
  for ( size_t q = 0; q < n; ++q )
  {
    while ( z[j + 1] < static_cast<double>( q ) )
      ++j;
    const double d = static_cast<double>( q ) - static_cast<double>( v[j] );
    f[q] = d * d + f[v[j]];
  }
}

} // namespace

bool distanceToForeground( const uint8_t *mask, int width, int height, float *out )
{
  if ( !mask || !out || width <= 0 || height <= 0 )
    return false;

  const size_t w = static_cast<size_t>( width );
  const size_t h = static_cast<size_t>( height );
  const size_t n = w * h;

  std::vector<double> f( n );
  for ( size_t i = 0; i < n; ++i )
    f[i] = ( mask[i] == 1 ) ? 0.0 : kUnreached;

  // Pass 1: along each row.
  {
    std::vector<double> z( w + 1 );
    std::vector<size_t> v( w );
    std::vector<double> row( w );
    for ( size_t y = 0; y < h; ++y )
    {
      std::copy( f.begin() + static_cast<std::ptrdiff_t>( y * w ),
                 f.begin() + static_cast<std::ptrdiff_t>( ( y + 1 ) * w ), row.begin() );
      squaredDistancePass( row.data(), z.data(), v.data(), w );
      std::copy( row.begin(), row.end(), f.begin() + static_cast<std::ptrdiff_t>( y * w ) );
    }
  }

  // Pass 2: down each column — the column pass consumes the row-transformed
  // values as-is: min over y' of (f1(x,y') + (y−y')²). No per-cell offset is
  // added; seeding with f + y² would corrupt the source cells.
  std::vector<double> z( h + 1 );
  std::vector<size_t> v( h );
  std::vector<double> col( h );
  for ( size_t x = 0; x < w; ++x )
  {
    for ( size_t y = 0; y < h; ++y )
      col[y] = f[y * w + x];
    squaredDistancePass( col.data(), z.data(), v.data(), h );
    for ( size_t y = 0; y < h; ++y )
      f[y * w + x] = col[y];
  }

  for ( size_t i = 0; i < n; ++i )
    out[i] = f[i] >= kUnreached ? std::numeric_limits<float>::infinity()
                                : static_cast<float>( std::sqrt( f[i] ) );
  return true;
}

} // namespace sicnu::rs::primitives
