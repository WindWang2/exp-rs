// src/processing/gdal/gdal_window_read.cpp
#include "gdal_window_read.h"

#include "gdal_dataset_wrapper.h"

#include <gdal.h>

#include <algorithm>
#include <limits>

namespace sicnu::processing
{

bool readClampedWindow( const GdalDatasetWrapper &ds, int band, int x0, int y0,
                        int w, int h, int halo, std::vector<float> &out )
{
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  const int bw = w + 2 * halo;
  const int bh = h + 2 * halo;
  out.assign( static_cast<size_t>( bw ) * bh, kNan );

  const int sx = std::max( 0, x0 - halo );
  const int sy = std::max( 0, y0 - halo );
  const int ex = std::min( ds.width(), x0 + w + halo );
  const int ey = std::min( ds.height(), y0 + h + halo );
  if ( ex <= sx || ey <= sy )
    return false;
  const int iw = ex - sx;
  const int ih = ey - sy;

  GDALRasterBandH bandH = GDALGetRasterBand( static_cast<GDALDatasetH>( ds.dataset() ), band );
  if ( !bandH )
    return false;

  const int ox = sx - ( x0 - halo );
  const int oy = sy - ( y0 - halo );
  std::vector<float> inner( static_cast<size_t>( iw ) * ih );
  if ( GDALRasterIO( bandH, GF_Read, sx, sy, iw, ih, inner.data(), iw, ih, GDT_Float32,
                     0, 0 ) != CE_None )
    return false;

  for ( int y = 0; y < ih; ++y )
    std::copy_n( inner.begin() + static_cast<size_t>( y ) * iw, iw,
                 out.begin() + static_cast<size_t>( y + oy ) * bw + ox );

  // Edge replication: replicate the top/bottom rows first, then fill the
  // left/right margins column-wise (a second row pass keeps every position
  // covered even in the 1-pixel-raster corner cases).
  for ( int y = 0; y < bh; ++y )
  {
    const int srcY = std::clamp( y, oy, oy + ih - 1 );
    if ( y != srcY )
      std::copy_n( out.begin() + static_cast<size_t>( srcY ) * bw, bw,
                   out.begin() + static_cast<size_t>( y ) * bw );
  }
  for ( int y = 0; y < bh; ++y )
  {
    float *row = out.data() + static_cast<size_t>( y ) * bw;
    for ( int x = 0; x < bw; ++x )
      row[x] = row[std::clamp( x, ox, ox + iw - 1 )];
  }
  return true;
}

} // namespace sicnu::processing
