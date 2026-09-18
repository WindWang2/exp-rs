// rs_pixel_rasterizer.cpp — see header for design notes.
#include "rs_pixel_rasterizer.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <vector>

// Test-only failure injection hooks (#1056). Null by default in production.
bool ( *RsPixelRasterizer::sForceRasterizeGeometriesFailure )() = nullptr;
bool ( *RsPixelRasterizer::sForceRasterIOFailure )() = nullptr;

QSet<quint64> RsPixelRasterizer::rasterize( const QgsGeometry &geom,
                                            const double gt[6],
                                            int W,
                                            int H,
                                            bool *ok )
{
  QSet<quint64> out;
  const auto succeed = [&ok]( QSet<quint64> set ) -> QSet<quint64> {
    if ( ok )
      *ok = true;
    return set;
  };
  const auto fail = [&ok]() -> QSet<quint64> {
    if ( ok )
      *ok = false;
    return QSet<quint64>();
  };

  // A null/empty geometry over a valid raster footprint is exact empty
  // coverage, not a failure. W/H <= 0 leaves no pixels to burn, so the
  // (empty) result is also exact.
  if ( geom.isNull() || geom.isEmpty() || W <= 0 || H <= 0 )
    return succeed( out );

  // Inverse GT: map (x,y) → pixel (col,row). Fail closed if non-invertible —
  // coverage cannot be determined, which must not read as "no pixels".
  double inv[6];
  if ( !GDALInvGeoTransform( const_cast<double *>( gt ), inv ) )
    return fail();

  // Clip geometry to the raster footprint in map space, then rasterize only
  // the bounding-box window so we never allocate a full W×H mask.
  // Calculate map-space footprint over all 4 corners to support rotated/sheared GT (#417).
  const double cx[4] = {
    gt[0],
    gt[0] + gt[1] * W,
    gt[0] + gt[2] * H,
    gt[0] + gt[1] * W + gt[2] * H
  };
  const double cy[4] = {
    gt[3],
    gt[3] + gt[4] * W,
    gt[3] + gt[5] * H,
    gt[3] + gt[4] * W + gt[5] * H
  };
  const QgsRectangle rasterExtent(
    *std::min_element( cx, cx + 4 ),
    *std::min_element( cy, cy + 4 ),
    *std::max_element( cx, cx + 4 ),
    *std::max_element( cy, cy + 4 ) );

  QgsGeometry clipped = geom;
  if ( !rasterExtent.isEmpty() )
  {
    const QgsGeometry footprint = QgsGeometry::fromRect( rasterExtent );
    clipped = geom.intersection( footprint );
    if ( clipped.isEmpty() || clipped.isNull() )
      return succeed( out );
  }

  const QgsRectangle bbox = clipped.boundingBox();
  if ( bbox.isEmpty() )
    return succeed( out );

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
    return succeed( out );

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
    return fail();

  GDALDataset *ds = drv->Create( "", winW, winH, 1, GDT_Byte, nullptr );
  if ( !ds )
    return fail();

  if ( ds->SetGeoTransform( winGt ) != CE_None )
  {
    GDALClose( ds );
    return fail();
  }
  // Fill is what makes "never burned" distinguishable from stale memory —
  // a failed Fill means the scan below would read undefined bytes.
  if ( ds->GetRasterBand( 1 )->Fill( 0 ) != CE_None )
  {
    GDALClose( ds );
    return fail();
  }

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
    return fail();
  }

  int bands[1] = { 1 };
  double burnValues[1] = { 1.0 };
  OGRGeometryH geoms[1] = { ogrGeom };
  // An empty coverage and a failed rasterization are indistinguishable in
  // the burned band; only the API result tells them apart (#1056).
  if ( sForceRasterizeGeometriesFailure && sForceRasterizeGeometriesFailure() )
  {
    OGR_G_DestroyGeometry( ogrGeom );
    GDALClose( ds );
    return fail();
  }
  const CPLErr rasterizeErr = GDALRasterizeGeometries( ds,
                           1, bands,
                           1, geoms,
                           nullptr, nullptr,
                           burnValues,
                           nullptr,
                           nullptr,
                           nullptr );
  OGR_G_DestroyGeometry( ogrGeom );
  if ( rasterizeErr == CE_Failure )
  {
    GDALClose( ds );
    return fail();
  }

  std::vector<uint8_t> buf( static_cast<size_t>( winW ) );
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  for ( int y = 0; y < winH; ++y )
  {
    if ( sForceRasterIOFailure && sForceRasterIOFailure() )
    {
      GDALClose( ds );
      return fail();
    }
    const CPLErr readErr = band->RasterIO( GF_Read, 0, y, winW, 1,
                                           buf.data(), winW, 1, GDT_Byte, 0, 0 );
    if ( readErr == CE_Failure )
    {
      GDALClose( ds );
      return fail();
    }
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

  GDALClose( ds );
  return succeed( out );
}
