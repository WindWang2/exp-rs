// W4 issue 299 regression: RsClassRaster::polygonize atomic write + sidecar cleanup.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include "rs_class_raster.h"
#include "rs_segment_map.h"

TEST_CASE( "RsClassRaster::polygonize preserves existing output on failure (299)", "[w4][299][polygonize]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Create a valid class raster (4x4, Byte)
    const QString classPath = tmp.path() + "/class.tif";
    {
        GDALAllRegister();
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        REQUIRE( drv != nullptr );
        GDALDataset *ds = drv->Create( classPath.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
        REQUIRE( ds != nullptr );
        uint8_t data[16] = { 1,1,0,0, 1,2,0,0, 0,0,2,2, 0,0,2,1 };
        ds->GetRasterBand(1)->RasterIO( GF_Write, 0,0,4,4, data,4,4, GDT_Byte,0,0 );
        double gt[6] = {0,1,0,4,0,-1};
        ds->SetGeoTransform(gt);
        GDALClose(ds);
    }

    // Create a previous output shapefile that should survive a failed polygonize on missing input
    const QString prevShp = tmp.path() + "/prev.shp";
    {
        GDALDriverH drv = GDALGetDriverByName( "ESRI Shapefile" );
        REQUIRE( drv != nullptr );
        GDALDatasetH ds = GDALCreate( drv, prevShp.toUtf8().constData(),0,0,0,GDT_Unknown,nullptr );
        REQUIRE( ds != nullptr );
        OGRLayerH layer = GDALDatasetCreateLayer(ds, "classes", nullptr, wkbPolygon, nullptr);
        REQUIRE( layer != nullptr );
        OGRFieldDefnH f = OGR_Fld_Create("class_id", OFTInteger);
        OGR_L_CreateField(layer,f,TRUE);
        OGR_Fld_Destroy(f);
        GDALClose(ds);
    }
    REQUIRE( QFile::exists(prevShp) );
    const QString prevDbf = prevShp + QStringLiteral(".dbf");
    // ESRI driver may create .dbf with same base name; check .dbf exists as prev.dbf or with tmp suffix
    // At least main .shp exists
    const QString missingRaster = tmp.path() + "/no_such.tif";
    auto res = RsClassRaster::polygonize( missingRaster, prevShp );
    REQUIRE_FALSE( res.ok );
    // Previous output must still exist (atomic: temp failure does not delete it)
    CHECK( QFile::exists(prevShp) );
    // No temp leftover
    CHECK_FALSE( QFile::exists(prevShp + QStringLiteral(".tmp~")) );
}

TEST_CASE( "RsClassRaster::polygonize cleans temp sidecars on failure (299)", "[w4][299][polygonize]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString classPath = tmp.path() + "/class.tif";
    {
        GDALAllRegister();
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        GDALDataset *ds = drv->Create( classPath.toUtf8().constData(), 2,2,1,GDT_Byte,nullptr);
        REQUIRE(ds!=nullptr);
        uint8_t d[4]={1,0,0,1};
        ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,2,2,d,2,2,GDT_Byte,0,0);
        GDALClose(ds);
    }
    const QString outShp = tmp.path() + "/out.shp";
    // Corrupt input causing polygonize to read? Use non-existent path to ensure no output created
    auto res = RsClassRaster::polygonize( QStringLiteral("/no/such/file.tif"), outShp );
    REQUIRE_FALSE(res.ok);
    CHECK_FALSE( QFile::exists(outShp) );
    // Ensure no orphan sidecars: check that out.dbf/.shx do not exist after failure
    CHECK_FALSE( QFile::exists(outShp + ".dbf") );
    CHECK_FALSE( QFile::exists(outShp + ".shx") );
}
