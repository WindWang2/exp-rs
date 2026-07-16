// rs_pixel_window.cpp — invert GT, map four corners, clamp to raster.
#include "rs_pixel_window.h"

#include <algorithm>
#include <cmath>

#include <gdal_priv.h>

RsPixelWindow rsMapExtentToPixelWindow( const QgsRectangle &extent,
                                        const double gt[6],
                                        int W,
                                        int H )
{
  RsPixelWindow win;
  if ( W <= 0 || H <= 0 || extent.isEmpty() || !gt )
    return win;

  double inv[6];
  if ( !GDALInvGeoTransform( const_cast<double *>( gt ), inv ) )
    return win;

  // Four corners of the map rectangle → pixel space.
  const double xs[4] = { extent.xMinimum(), extent.xMaximum(),
                         extent.xMinimum(), extent.xMaximum() };
  const double ys[4] = { extent.yMinimum(), extent.yMinimum(),
                         extent.yMaximum(), extent.yMaximum() };

  double minPx = 0.0;
  double maxPx = 0.0;
  double minPy = 0.0;
  double maxPy = 0.0;
  for ( int i = 0; i < 4; ++i )
  {
    const double px = inv[0] + inv[1] * xs[i] + inv[2] * ys[i];
    const double py = inv[3] + inv[4] * xs[i] + inv[5] * ys[i];
    if ( i == 0 )
    {
      minPx = maxPx = px;
      minPy = maxPy = py;
    }
    else
    {
      minPx = std::min( minPx, px );
      maxPx = std::max( maxPx, px );
      minPy = std::min( minPy, py );
      maxPy = std::max( maxPy, py );
    }
  }

  // Half-open integer window: floor min / ceil max, then clamp to [0,W]/[0,H].
  int x0 = static_cast<int>( std::floor( minPx ) );
  int x1 = static_cast<int>( std::ceil( maxPx ) );
  int y0 = static_cast<int>( std::floor( minPy ) );
  int y1 = static_cast<int>( std::ceil( maxPy ) );

  x0 = std::clamp( x0, 0, W );
  x1 = std::clamp( x1, 0, W );
  y0 = std::clamp( y0, 0, H );
  y1 = std::clamp( y1, 0, H );

  if ( x1 <= x0 || y1 <= y0 )
    return win;

  win.x0 = x0;
  win.y0 = y0;
  win.x1 = x1;
  win.y1 = y1;
  win.valid = true;
  return win;
}
