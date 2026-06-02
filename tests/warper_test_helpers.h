// SICNU GEO RS — test helpers for the QgsImageWarper test suite.
// Header-only; included by tests/test_image_warper*.cpp.

#pragma once

#include <QString>
#include <QFile>
#include <cstdint>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

namespace warper_test
{

/**
 * Create a synthetic 8-bit single-band GeoTIFF of size \a w x \a h on disk.
 * The pixel ramp is `(x + y) % 256` so the file is non-trivial but cheap to
 * generate.  Returns the absolute path to the new raster.
 */
inline QString makeSyntheticRaster( const QString &dir, int w, int h )
{
  GDALAllRegister();
  const QString path = dir + QStringLiteral( "/synthetic_%1x%2.tif" ).arg( w ).arg( h );
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return QString();
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), w, h, 1, GDT_Byte, nullptr );
  if ( !ds )
    return QString();

  std::vector<uint8_t> row( w );
  for ( int y = 0; y < h; ++y )
  {
    for ( int x = 0; x < w; ++x )
      row[x] = static_cast<uint8_t>( ( x + y ) % 256 );
    ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, y, w, 1,
      row.data(), w, 1, GDT_Byte, 0, 0 );
  }
  GDALClose( ds );
  return path;
}

inline QString makeSynthetic64Raster( const QString &dir )
{
  return makeSyntheticRaster( dir, 64, 64 );
}

/**
 * Create a 64x64 synthetic raster that already carries a GeoTransform and a
 * CRS (via EPSG:32650 by default).  Used by the CRS pass-through test.
 */
inline QString makeSynthetic64RasterWithCrs( const QString &dir, const QString &epsg )
{
  GDALAllRegister();
  const QString path = dir + QStringLiteral( "/synthetic_64x64_crs.tif" );
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return QString();
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), 64, 64, 1, GDT_Byte, nullptr );
  if ( !ds )
    return QString();

  std::vector<uint8_t> row( 64 );
  for ( int y = 0; y < 64; ++y )
  {
    for ( int x = 0; x < 64; ++x )
      row[x] = static_cast<uint8_t>( ( x + y ) % 256 );
    ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, y, 64, 1,
      row.data(), 64, 1, GDT_Byte, 0, 0 );
  }

  // Mark the dataset as already georeferenced — start at (0,0) with pixel
  // size 1 so the warp doesn't need a reprojection step.
  double gt[6] = { 0.0, 1.0, 0.0, 64.0, 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );

  OGRSpatialReference srs;
  srs.SetFromUserInput( epsg.toUtf8().constData() );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  if ( wkt )
  {
    GDALSetProjection( ds, wkt );
    CPLFree( wkt );
  }

  GDALClose( ds );
  return path;
}

/**
 * Create a 64x64 raster with a synthetic RPC metadata domain.  The polynomial
 * coefficients are an identity-ish RFM so that the center pixel (32, 32) maps
 * to (LONG_OFF, LAT_OFF) = (116.0, 39.0).  Used by the RPC transformer tests.
 */
inline QString makeSyntheticRpcRaster( const QString &dir )
{
  GDALAllRegister();
  const QString path = dir + QStringLiteral( "/synthetic_rpc.tif" );
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return QString();
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), 64, 64, 1, GDT_Byte, nullptr );
  if ( !ds )
    return QString();

  char **md = nullptr;
  md = CSLSetNameValue( md, "LINE_OFF", "32" );
  md = CSLSetNameValue( md, "SAMP_OFF", "32" );
  md = CSLSetNameValue( md, "LAT_OFF", "39.0" );
  md = CSLSetNameValue( md, "LONG_OFF", "116.0" );
  md = CSLSetNameValue( md, "HEIGHT_OFF", "0" );
  md = CSLSetNameValue( md, "LINE_SCALE", "32" );
  md = CSLSetNameValue( md, "SAMP_SCALE", "32" );
  md = CSLSetNameValue( md, "LAT_SCALE", "0.001" );
  md = CSLSetNameValue( md, "LONG_SCALE", "0.001" );
  md = CSLSetNameValue( md, "HEIGHT_SCALE", "1000" );
  md = CSLSetNameValue( md, "LINE_NUM_COEFF",
                        "0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  md = CSLSetNameValue( md, "LINE_DEN_COEFF",
                        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  md = CSLSetNameValue( md, "SAMP_NUM_COEFF",
                        "0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  md = CSLSetNameValue( md, "SAMP_DEN_COEFF",
                        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  ds->SetMetadata( md, "RPC" );
  CSLDestroy( md );
  GDALClose( ds );
  return path;
}

} // namespace warper_test

// Convenience inline non-namespaced aliases so test files don't need an
// extra `using` directive.
inline QString makeSyntheticRaster( const QString &dir, int w, int h )
{
  return warper_test::makeSyntheticRaster( dir, w, h );
}
inline QString makeSynthetic64Raster( const QString &dir )
{
  return warper_test::makeSynthetic64Raster( dir );
}
inline QString makeSynthetic64RasterWithCrs( const QString &dir, const QString &epsg = QStringLiteral( "EPSG:32650" ) )
{
  return warper_test::makeSynthetic64RasterWithCrs( dir, epsg );
}
inline QString makeSyntheticRpcRaster( const QString &dir )
{
  return warper_test::makeSyntheticRpcRaster( dir );
}
