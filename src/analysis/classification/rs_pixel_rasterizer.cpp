// rs_pixel_rasterizer.cpp — see header for design notes.
#include "rs_pixel_rasterizer.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>

#include <QByteArray>

#include <vector>

QSet<quint64> RsPixelRasterizer::rasterize( const QgsGeometry &geom,
                                            const double gt[6],
                                            int W,
                                            int H )
{
  QSet<quint64> out;
  if ( geom.isNull() || geom.isEmpty() || W <= 0 || H <= 0 )
    return out;

  GDALAllRegister();
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "MEM" );
  if ( !drv )
    return out;

  GDALDataset *ds = drv->Create( "", W, H, 1, GDT_Byte, nullptr );
  if ( !ds )
    return out;

  ds->SetGeoTransform( const_cast<double *>( gt ) );
  ds->GetRasterBand( 1 )->Fill( 0 );

  const QByteArray wkb = geom.asWkb();
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

  std::vector<uint8_t> buf( static_cast<size_t>( W ) );
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  for ( int y = 0; y < H; ++y )
  {
    band->RasterIO( GF_Read, 0, y, W, 1,
                    buf.data(), W, 1, GDT_Byte, 0, 0 );
    for ( int x = 0; x < W; ++x )
    {
      if ( buf[static_cast<size_t>( x )] )
        out.insert( static_cast<quint64>( y ) * static_cast<quint64>( W ) + static_cast<quint64>( x ) );
    }
  }

  OGR_G_DestroyGeometry( ogrGeom );
  GDALClose( ds );
  return out;
}
