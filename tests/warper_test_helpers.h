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
  // Coefficient index 3 couples line offset to normalized height (H_n).  With
  // HEIGHT_OFF=0 and HEIGHT_SCALE=1000, an RPC_HEIGHT=0 input yields H_n=0 and
  // preserves the identity-at-center behaviour the Task 11.4.8 tests rely on;
  // a non-zero RPC_HEIGHT produces a small but measurable shift, which the
  // Task 11.5.4 Z-offset test depends on.
  md = CSLSetNameValue( md, "LINE_NUM_COEFF",
                        "0 1 0 0.5 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
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

/**
 * Task 11.5.6 — "synthetic real" RPC fixture.
 *
 * Creates a 256x256 single-band 8-bit GeoTIFF carrying a non-trivial RPC
 * metadata domain.  The polynomial couples line to both X (column) and Z
 * (height), and sample to Y (row) and Z, so the resulting transform
 * exercises the full RPC + DEM pipeline (unlike the identity-ish fixture
 * used by Task 11.4.8).  The pixel ramp is an 8-bit gray sweep so warp
 * output is identifiable.
 *
 * Returns the absolute path to the new raster.
 */
inline QString makeSyntheticRpcRasterRealistic( const QString &dir )
{
  GDALAllRegister();
  const QString path = dir + QStringLiteral( "/synthetic_rpc_realistic.tif" );
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return QString();
  const int W = 256, H = 256;
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Byte, nullptr );
  if ( !ds )
    return QString();

  // Bake an identifiable gray ramp:  pixel = ((x + y * 2) % 256).
  std::vector<uint8_t> row( W );
  for ( int y = 0; y < H; ++y )
  {
    for ( int x = 0; x < W; ++x )
      row[x] = static_cast<uint8_t>( ( x + 2 * y ) & 0xFF );
    ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, y, W, 1,
      row.data(), W, 1, GDT_Byte, 0, 0 );
  }

  // Non-trivial RPC metadata.  Center pixel (128, 128) maps to
  // (LONG_OFF, LAT_OFF) = (116.0, 39.0) when H_n = 0.
  //
  // LINE_NUM_COEFF index 1 (X) and index 3 (Z) are nonzero, so the line
  // varies with both column AND height.  SAMP_NUM_COEFF index 2 (Y) and
  // index 3 (Z) are nonzero, so the sample varies with both row AND
  // height.  This exercises the full RPC+DEM pipeline.
  char **md = nullptr;
  md = CSLSetNameValue( md, "LINE_OFF", "128" );
  md = CSLSetNameValue( md, "SAMP_OFF", "128" );
  md = CSLSetNameValue( md, "LAT_OFF", "39.0" );
  md = CSLSetNameValue( md, "LONG_OFF", "116.0" );
  md = CSLSetNameValue( md, "HEIGHT_OFF", "250" );
  md = CSLSetNameValue( md, "LINE_SCALE", "128" );
  md = CSLSetNameValue( md, "SAMP_SCALE", "128" );
  md = CSLSetNameValue( md, "LAT_SCALE", "0.01" );
  md = CSLSetNameValue( md, "LONG_SCALE", "0.01" );
  md = CSLSetNameValue( md, "HEIGHT_SCALE", "500" );
  // Coefficient order (RPC00B): [1, L, P, H, L*P, L*H, P*H, L*L, P*P, H*H,
  //   L*P*H, L*L*L, L*P*P, L*H*H, L*L*P, P*P*P, P*H*H, L*L*H, P*P*H, H*H*H]
  // where L=normalized longitude, P=normalized latitude, H=normalized height.
  // For LINE_NUM_COEFF, index 1 (L) and index 3 (H) couple line to column and height.
  md = CSLSetNameValue( md, "LINE_NUM_COEFF",
                        "0 1 0 0.4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  md = CSLSetNameValue( md, "LINE_DEN_COEFF",
                        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  // For SAMP_NUM_COEFF, index 2 (P) and index 3 (H) couple sample to row and height.
  md = CSLSetNameValue( md, "SAMP_NUM_COEFF",
                        "0 0 1 0.3 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  md = CSLSetNameValue( md, "SAMP_DEN_COEFF",
                        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );
  ds->SetMetadata( md, "RPC" );
  CSLDestroy( md );
  GDALClose( ds );
  return path;
}

/**
 * Task 11.5.6 — synthetic DEM tile.
 *
 * Creates a 16x16 single-band Float32 GeoTIFF in WGS84 (EPSG:4326) with a
 * sloped Z field: ~100 m at the SW corner ramping to ~500 m at the NE
 * corner.  Spatial extent covers the region (LONG_OFF, LAT_OFF) +/- LONG_SCALE
 * for the realistic RPC fixture so it fully covers the projected RPC
 * footprint.  Returns the absolute path.
 */
inline QString makeSyntheticDem( const QString &dir )
{
  GDALAllRegister();
  const QString path = dir + QStringLiteral( "/synthetic_dem.tif" );
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
    return QString();
  const int W = 16, H = 16;
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  if ( !ds )
    return QString();

  // Cover lon in [115.97, 116.03] x lat in [38.97, 39.03] (a generous
  // padding around the realistic RPC ground footprint of ~+/-0.01 degree).
  const double lonMin = 115.97;
  const double lonMax = 116.03;
  const double latMin = 38.97;
  const double latMax = 39.03;
  const double pxX = ( lonMax - lonMin ) / W;
  const double pxY = ( latMax - latMin ) / H;
  // GeoTransform: top-left at (lonMin, latMax); positive pxX, negative pxY.
  double gt[6] = { lonMin, pxX, 0.0, latMax, 0.0, -pxY };
  GDALSetGeoTransform( ds, gt );

  OGRSpatialReference srs;
  srs.SetFromUserInput( "EPSG:4326" );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  if ( wkt )
  {
    GDALSetProjection( ds, wkt );
    CPLFree( wkt );
  }

  std::vector<float> buf( W * H );
  // Slope from 100 m (SW corner: row=H-1, col=0) to 500 m (NE corner: row=0, col=W-1).
  for ( int y = 0; y < H; ++y )
  {
    for ( int x = 0; x < W; ++x )
    {
      const double fx = double( x ) / ( W - 1 );        // 0 at west, 1 at east
      const double fy = double( H - 1 - y ) / ( H - 1 ); // 0 at south, 1 at north
      // Total = SW + (east-west contribution) + (south-north contribution).
      buf[y * W + x] = static_cast<float>( 100.0 + 200.0 * fx + 200.0 * fy );
    }
  }
  ds->GetRasterBand( 1 )->RasterIO(
    GF_Write, 0, 0, W, H,
    buf.data(), W, H, GDT_Float32, 0, 0 );
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
inline QString makeSyntheticRpcRasterRealistic( const QString &dir )
{
  return warper_test::makeSyntheticRpcRasterRealistic( dir );
}
inline QString makeSyntheticDem( const QString &dir )
{
  return warper_test::makeSyntheticDem( dir );
}
