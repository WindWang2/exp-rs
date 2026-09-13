// F-OPS-4 — io:reproject drops srcCrsOverride: a CRS-less source reprojects
// as identity and gets labeled with the target CRS. Review draft, not built.
#include <catch2/catch.hpp>
#include <QDir>
#include <gdal_priv.h>
#include "operators/io/io_operators.h"
#include "operators/framework/rs_operator_context.h"

namespace {
QString writeCrslessRaster( const QDir &dir )
{
  const QString path = dir.filePath( "crsless.tif" );
  GDALDriverH drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  GDALDatasetH ds = GDALCreate( drv, path.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0, 1, 0, 0, 0, 1 }; // plain pixel grid, no CRS
  REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
  GDALClose( ds );
  return path;
}
} // namespace

TEST_CASE( "F-OPS-4: srcCrsOverride must actually reproject a CRS-less source",
           "[review][F-OPS-4][test-draft]" )
{
  // io:reproject with srcCrsOverride=EPSG:4326, targetCrs=EPSG:32633 must
  // produce a raster whose geotransform is a UTM 33N grid derived from the
  // 4326 pixel centers. Current master: WarpOptions carries no source CRS,
  // GDALWarp assumes same-CRS → the output geotransform is the untouched
  // pixel grid tagged EPSG:32633 → CHECK fails.
  REQUIRE( false ); // placeholder: drafts are not compiled on this track
}
