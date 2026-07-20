// rs_pixel_rasterizer.cpp — see header for design notes.
#include "rs_pixel_rasterizer.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <vector>

QSet<quint64> RsPixelRasterizer::rasterize( const QgsGeometry &geom,
                                            const double gt[6],
                                            int W,
                                            int H )
{
  QSet<quint64> out;
  if ( geom.isNull() || geom.isEmpty() || W <= 0 || H <= 0 )
    return out;

  // Inverse GT: map (x,y) → pixel (col,row). Fail closed if non-invertible.
  double inv[6];
  if ( !GDALInvGeoTransform( const_cast<double *>( gt ), inv ) )
    return out;

  // Clip geometry to the raster footprint in map space, then rasterize only
  // the bounding-box window so we never allocate a full W×H mask.
  const double x0 = gt[0];
  const double y0 = gt[3];
  const double x1 = gt[0] + gt[1] * W + gt[2] * H;
  const double y1 = gt[3] + gt[4] * W + gt[5] * H;
  const QgsRectangle rasterExtent( std::min( x0, x1 ), std::min( y0, y1 ),
                                   std::max( x0, x1 ), std::max( y0, y1 ) );

  QgsGeometry clipped = geom;
  if ( !rasterExtent.isEmpty() )
  {
    const QgsGeometry footprint = QgsGeometry::fromRect( rasterExtent );
    clipped = geom.intersection( footprint );
    if ( clipped.isEmpty() || clipped.isNull() )
      return out;
  }

  const QgsRectangle bbox = clipped.boundingBox();
  if ( bbox.isEmpty() )
    return out;

  auto mapToCol = [&]( double x, double y ) -> double {
    return inv[0] + inv[1] * x + inv[2] * y;
  };
  auto mapToRow = [&]( double x, double y ) -> double {
    return inv[3] + inv[4] * x + inv[5] * y;
  };

  // Sample bbox corners (handles rotation/shear GT terms).
  const double cols[4] = {
    mapToCol( bbox.xMinimum(), bbox.yMinimum() ),
    mapToCol( bbox.xMinimum(), bbox.yMaximum() ),
    mapToCol( bbox.xMaximum(), bbox.yMinimum() ),
    mapToCol( bbox.xMaximum(), bbox.yMaximum() ),
  };
  const double rows[4] = {
    mapToRow( bbox.xMinimum(), bbox.yMinimum() ),
    mapToRow( bbox.xMinimum(), bbox.yMaximum() ),
    mapToRow( bbox.xMaximum(), bbox.yMinimum() ),
    mapToRow( bbox.xMaximum(), bbox.yMaximum() ),
  };

  int col0 = static_cast<int>( std::floor( *std::min_element( cols, cols + 4 ) ) ) - 1;
  int col1 = static_cast<int>( std::ceil( *std::max_element( cols, cols + 4 ) ) ) + 1;
  int row0 = static_cast<int>( std::floor( *std::min_element( rows, rows + 4 ) ) ) - 1;
  int row1 = static_cast<int>( std::ceil( *std::max_element( rows, rows + 4 ) ) ) + 1;

  col0 = std::clamp( col0, 0, W - 1 );
  col1 = std::clamp( col1, 0, W - 1 );
  row0 = std::clamp( row0, 0, H - 1 );
  row1 = std::clamp( row1, 0, H - 1 );
  if ( col1 < col0 || row1 < row0 )
    return out;

  const int winW = col1 - col0 + 1;
  const int winH = row1 - row0 + 1;

  // Window geotransform: origin at top-left of the window in map space.
  double winGt[6] = {
    gt[0] + gt[1] * col0 + gt[2] * row0,
    gt[1],
    gt[2],
    gt[3] + gt[4] * col0 + gt[5] * row0,
    gt[4],
    gt[5],
  };

  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "MEM" );
  if ( !drv )
    return out;

  GDALDataset *ds = drv->Create( "", winW, winH, 1, GDT_Byte, nullptr );
  if ( !ds )
    return out;

  ds->SetGeoTransform( winGt );
  ds->GetRasterBand( 1 )->Fill( 0 );

  const QByteArray wkb = clipped.asWkb();
  OGRGeometryH ogrGeom = nullptr;
  OGR_G_CreateFromWkb(
    const_cast<unsigned char *>( reinterpret_cast<const unsigned char *>( wkb.constData() ) ),
    nullptr,
    &ogrGeom,
    wkb.size() );
  if ( !ogrGeom )
  {
    GDALClose( ds );
    return out;
  }

  int bands[1] = { 1 };
  double burnValues[1] = { 1.0 };
  OGRGeometryH geoms[1] = { ogrGeom };
  GDALRasterizeGeometries( ds,
                           1, bands,
                           1, geoms,
                           nullptr, nullptr,
                           burnValues,
                           nullptr,
                           nullptr,
                           nullptr );

  std::vector<uint8_t> buf( static_cast<size_t>( winW ) );
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  for ( int y = 0; y < winH; ++y )
  {
    band->RasterIO( GF_Read, 0, y, winW, 1,
                    buf.data(), winW, 1, GDT_Byte, 0, 0 );
    const int globalRow = row0 + y;
    for ( int x = 0; x < winW; ++x )
    {
      if ( !buf[static_cast<size_t>( x )] )
        continue;
      const int globalCol = col0 + x;
      out.insert( static_cast<quint64>( globalRow ) * static_cast<quint64>( W )
                  + static_cast<quint64>( globalCol ) );
    }
  }

  OGR_G_DestroyGeometry( ogrGeom );
  GDALClose( ds );
  return out;
}
