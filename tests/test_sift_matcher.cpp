#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <vector>

#include <gdal_priv.h>

#include "rs_sift_matcher.h"
#include "qgsfeedback.h"

using Catch::Approx;

namespace
{
QString makeRandomRaster( const QString &dir, const QString &name, int w, int h, int seed )
{
  GDALAllRegister();
  const QString path = dir + "/" + name;
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), w, h, 1, GDT_Byte, nullptr );
  REQUIRE( ds != nullptr );

  // Identity-ish GeoTransform: world X = px, world Y = H - py (so flipped Y for typical north-up).
  double gt[6] = { 0.0, 1.0, 0.0, double( h ), 0.0, -1.0 };
  ds->SetGeoTransform( gt );

  std::srand( static_cast<unsigned>( seed ) );
  std::vector<uint8_t> row( w );
  for ( int y = 0; y < h; ++y )
  {
    for ( int x = 0; x < w; ++x )
    {
      // Mix several frequencies + strong noise so SIFT has rich features to lock onto.
      double v = 128.0;
      v += 40.0 * std::sin( 0.05 * x + 0.07 * y );
      v += 30.0 * std::sin( 0.13 * x - 0.11 * y );
      v += 20.0 * std::cos( 0.31 * x + 0.27 * y );
      v += ( std::rand() % 60 ) - 30;
      row[x] = uint8_t( std::clamp( int( v ), 0, 255 ) );
    }
    ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, y, w, 1, row.data(), w, 1, GDT_Byte, 0, 0 );
  }
  GDALClose( ds );
  return path;
}

QString makeShiftedRaster( const QString &dir, const QString &name,
                           const QString &src, int dx, int dy )
{
  GDALAllRegister();
  const QString path = dir + "/" + name;
  GDALDataset *srcDs = static_cast<GDALDataset *>(
    GDALOpen( src.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( srcDs != nullptr );
  const int W = srcDs->GetRasterXSize();
  const int H = srcDs->GetRasterYSize();

  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  GDALDataset *dstDs = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Byte, nullptr );
  REQUIRE( dstDs != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, double( H ), 0.0, -1.0 };
  dstDs->SetGeoTransform( gt );

  std::vector<uint8_t> srcRow( W ), dstRow( W, 0 );
  for ( int y = 0; y < H; ++y )
  {
    const int sy = y - dy;
    if ( sy < 0 || sy >= H )
    {
      std::fill( dstRow.begin(), dstRow.end(), 0 );
      dstDs->GetRasterBand( 1 )->RasterIO(
        GF_Write, 0, y, W, 1, dstRow.data(), W, 1, GDT_Byte, 0, 0 );
      continue;
    }
    srcDs->GetRasterBand( 1 )->RasterIO(
      GF_Read, 0, sy, W, 1, srcRow.data(), W, 1, GDT_Byte, 0, 0 );
    std::fill( dstRow.begin(), dstRow.end(), 0 );
    for ( int x = 0; x < W; ++x )
    {
      const int sx = x - dx;
      if ( sx >= 0 && sx < W )
        dstRow[x] = srcRow[sx];
    }
    dstDs->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, y, W, 1, dstRow.data(), W, 1, GDT_Byte, 0, 0 );
  }
  GDALClose( srcDs );
  GDALClose( dstDs );
  return path;
}
} // namespace

TEST_CASE( "SIFT matcher: synthetic translation yields >= 20 inliers", "[georef][sift]" )
{
#ifndef SICNU_HAS_OPENCV
  SKIP( "OpenCV not available at build time — see CMakeCache.txt" );
#else
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const QString srcPath = makeRandomRaster( tmp.path(), "src.tif", 512, 512, 42 );
  const QString refPath = makeShiftedRaster( tmp.path(), "ref.tif", srcPath, +50, +30 );

  QgsFeedback fb;
  RsSiftMatcher matcher( &fb );
  RsSiftMatcher::Params params;
  params.maxImageSide = 512; // no downsample for this synthetic test
  auto result = matcher.run(
    srcPath, refPath,
    QgsCoordinateReferenceSystem( "EPSG:4326" ), params );

  INFO( "errorMessage=" << result.errorMessage.toStdString()
        << " totalMatches=" << result.totalMatches
        << " inliers=" << result.inliers.size()
        << " inlierRatio=" << result.inlierRatio );
  REQUIRE( result.ok() );
  REQUIRE( result.inliers.size() >= 20 );
  REQUIRE( result.inlierRatio >= 0.5 );

  // Average estimated translation: in pixel space we shifted by (+50, +30).
  // Source GeoTransform has world Y = H - py, so dy_pixel=+30 makes the world-Y
  // location *smaller* by 30 (since gt[5] = -1). The matched point's REF world
  // coords come from REF GeoTransform applied to REF pixel — and REF pixel for
  // the corresponding SRC pixel is shifted by (+50, +30). So:
  //   dstWorld.x = refPxX = srcPxX + 50
  //   dstWorld.y = H - refPxY = H - (srcPxY + 30) = (H - srcPxY) - 30 = srcWorldY - 30
  // i.e. avg (dstWorld.x - srcPx.x) ~= +50, avg (srcPx.y - dstWorld.y) ~= H - srcPxY - dstWorld.y? careful.
  // srcPx.y is pixel coords; dstWorld.y is world (= H - refPxY = H - (srcPxY + 30)).
  // So (H - srcPx.y) - dstWorld.y = (H - srcPx.y) - (H - srcPx.y - 30) = +30. OK.
  double sumDx = 0, sumDy = 0;
  for ( const auto &m : result.inliers )
  {
    sumDx += ( m.dstWorld.x() - m.srcPx.x() );
    sumDy += ( ( 512.0 - m.srcPx.y() ) - m.dstWorld.y() );
  }
  const double avgDx = sumDx / result.inliers.size();
  const double avgDy = sumDy / result.inliers.size();
  REQUIRE( std::abs( avgDx - 50.0 ) <= 3.0 );
  REQUIRE( std::abs( avgDy - 30.0 ) <= 3.0 );
#endif
}

TEST_CASE( "SIFT matcher: returns graceful error on missing files", "[georef][sift]" )
{
  QgsFeedback fb;
  RsSiftMatcher m( &fb );
  auto r = m.run(
    "/does/not/exist.tif", "/also/not.tif",
    QgsCoordinateReferenceSystem( "EPSG:4326" ),
    RsSiftMatcher::Params{} );
  REQUIRE_FALSE( r.ok() );
}
