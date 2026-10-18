// piecewise_linear_enhancement.cpp
#include "piecewise_linear_enhancement.h"

#include <algorithm>
#include <cmath>

namespace rs::display {

PiecewiseLinearEnhancement::PiecewiseLinearEnhancement( Qgis::DataType dataType,
                                                        double minimumValue,
                                                        double maximumValue,
                                                        std::vector<ControlPoint> points )
  : QgsContrastEnhancementFunction( dataType, minimumValue, maximumValue )
  , m_points( std::move( points ) )
{
  std::sort( m_points.begin(), m_points.end(),
             []( const ControlPoint &a, const ControlPoint &b ) { return a.x < b.x; } );

  // Drop non-finite points
  m_points.erase( std::remove_if( m_points.begin(), m_points.end(),
                                  []( const ControlPoint &p ) {
                                    return !std::isfinite( p.x ) || !std::isfinite( p.y );
                                  } ),
                  m_points.end() );

  // Ensure at least a 2-point identity-like curve if empty after filter
  if ( m_points.size() < 2 )
  {
    m_points = {
      ControlPoint{ minimumValue, 0.0 },
      ControlPoint{ maximumValue, 255.0 },
    };
  }
}

QgsContrastEnhancementFunction *PiecewiseLinearEnhancement::clone() const
{
  return new PiecewiseLinearEnhancement( *this );
}

int PiecewiseLinearEnhancement::enhance( double value )
{
  if ( m_points.empty() )
    return 0;

  if ( value <= m_points.front().x )
    return qBound( 0, static_cast<int>( std::lround( m_points.front().y ) ), 255 );

  if ( value >= m_points.back().x )
    return qBound( 0, static_cast<int>( std::lround( m_points.back().y ) ), 255 );

  // m_points is sorted by x (constructor); find the segment [i, i+1] that
  // contains value with a binary search — the linear scan paid ~n
  // comparisons per pixel on float rasters where QGIS builds no LUT
  // (gamma/equalization curves inject ~257 points).
  const auto upper = std::lower_bound(
    m_points.begin(), m_points.end(), value,
    []( const ControlPoint &p, double v ) { return p.x < v; } );
  if ( upper == m_points.begin() || upper == m_points.end() )
    return 0; // unreachable after the endpoint guards; keep honest
  const ControlPoint &right = *upper;
  const ControlPoint &left = *( upper - 1 );
  const double t = ( right.x > left.x ) ? ( value - left.x ) / ( right.x - left.x ) : 0.0;
  const double y = left.y + t * ( right.y - left.y );
  return qBound( 0, static_cast<int>( std::lround( y ) ), 255 );
}

bool PiecewiseLinearEnhancement::isValueInDisplayableRange( double value )
{
  (void) value;
  // Map all values; endpoints clamp. Keeps image fully opaque under piecewise.
  return true;
}

} // namespace rs::display
