// rs_pixel_window.cpp — map-extent → raster pixel half-open window
#include "rs_pixel_window.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gdal_priv.h>

RsPixelWindow rsMapExtentToPixelWindow( const QgsRectangle &extent,
                                        const double gt[6],
                                        int W,
                                        int H )
{
  RsPixelWindow out;
  if ( W <= 0 || H <= 0 || extent.isEmpty() )
    return out;

  double inv[6];
  // GDALInvGeoTransform takes non-const gt; values are not modified.
  if ( !GDALInvGeoTransform( const_cast<double *>( gt ), inv ) )
    return out;

  const double xs[4] = {
    extent.xMinimum(), extent.xMaximum(),
    extent.xMinimum(), extent.xMaximum()
  };
  const double ys[4] = {
    extent.yMinimum(), extent.yMinimum(),
    extent.yMaximum(), extent.yMaximum()
  };

  double minPx = std::numeric_limits<double>::infinity();
  double minPy = std::numeric_limits<double>::infinity();
  double maxPx = -std::numeric_limits<double>::infinity();
  double maxPy = -std::numeric_limits<double>::infinity();

  for ( int i = 0; i < 4; ++i )
  {
    // Pixel = InvGT · (mapX, mapY, 1)
    const double px = inv[0] + inv[1] * xs[i] + inv[2] * ys[i];
    const double py = inv[3] + inv[4] * xs[i] + inv[5] * ys[i];
    minPx = std::min( minPx, px );
    maxPx = std::max( maxPx, px );
    minPy = std::min( minPy, py );
    maxPy = std::max( maxPy, py );
  }

  int x0 = static_cast<int>( std::floor( minPx ) );
  int y0 = static_cast<int>( std::floor( minPy ) );
  int x1 = static_cast<int>( std::ceil( maxPx ) );
  int y1 = static_cast<int>( std::ceil( maxPy ) );

  // Clamp half-open interval endpoints into [0, W] / [0, H].
  x0 = std::clamp( x0, 0, W );
  x1 = std::clamp( x1, 0, W );
  y0 = std::clamp( y0, 0, H );
  y1 = std::clamp( y1, 0, H );

  if ( x1 > x0 && y1 > y0 )
  {
    out.x0 = x0;
    out.y0 = y0;
    out.x1 = x1;
    out.y1 = y1;
    out.valid = true;
  }
  return out;
}
