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

  for ( size_t i = 0; i + 1 < m_points.size(); ++i )
  {
    const double x1 = m_points[i].x;
    const double y1 = m_points[i].y;
    const double x2 = m_points[i + 1].x;
    const double y2 = m_points[i + 1].y;
    if ( value >= x1 && value <= x2 )
    {
      const double t = ( x2 > x1 ) ? ( value - x1 ) / ( x2 - x1 ) : 0.0;
      const double y = y1 + t * ( y2 - y1 );
      return qBound( 0, static_cast<int>( std::lround( y ) ), 255 );
    }
  }

  return 0;
}

bool PiecewiseLinearEnhancement::isValueInDisplayableRange( double value )
{
  (void) value;
  // Map all values; endpoints clamp. Keeps image fully opaque under piecewise.
  return true;
}

} // namespace rs::display
