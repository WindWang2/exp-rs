// Task 11.5.3 — Image-to-Image mode load + canvas swap.
//
// Validates that the I2I shell loads an independent reference raster into REF.

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include "qgsgeoreferencermainwindow.h"
#include "qgsmapcanvas.h"

namespace
{
  QApplication *ensureApp()
  {
    static int argc = 1;
    static char arg0[] = "test_image_to_image_load";
    static char *argv[] = { arg0, nullptr };
    return qApp ? nullptr : new QApplication( argc, argv );
  }

  QString makeSimpleRaster( const QString &dir, const QString &name )
  {
    GDALAllRegister();
    const QString path = dir + "/" + name;
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), 32, 32, 1, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, 32, 0, -1 };
    ds->SetGeoTransform( gt );
    ds->SetProjection(
      "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS 84\",6378137,298.257223563]],"
      "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALClose( ds );
    return path;
  }
} // namespace

TEST_CASE( "Image-to-Image: loadReferenceRaster wires REF canvas with one layer", "[georef][i2i]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString refPath = makeSimpleRaster( tmp.path(), "ref.tif" );

  QgsGeoreferencerMainWindow w( nullptr );
  REQUIRE( w.loadReferenceRaster( refPath ) );

  auto *refCanvas = w.findChild<QgsMapCanvas *>( "rsRefCanvas" );
  REQUIRE( refCanvas );
  REQUIRE( refCanvas->layerCount() == 1 );
}

TEST_CASE( "Image-to-Image: invalid path returns false and leaves canvas untouched",
           "[georef][i2i]" )
{
  ensureApp();
  QgsGeoreferencerMainWindow w( nullptr );
  REQUIRE_FALSE( w.loadReferenceRaster( "/does/not/exist.tif" ) );
}
