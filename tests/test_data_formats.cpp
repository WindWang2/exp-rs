// Data Format tests — verify GDAL driver support for RS formats
#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

class GdalDriverChecker
{
public:
    GdalDriverChecker()
    {
        GDALAllRegister();
    }

    bool hasDriver(const char *driverName) const
    {
        GDALDriverH driver = GDALGetDriverByName(driverName);
        return driver != nullptr;
    }
};

TEST_CASE("GDAL driver availability", "[gdal][drivers]") {
    GdalDriverChecker checker;

    SECTION("GeoTIFF driver available") {
        CHECK(checker.hasDriver("GTiff"));
    }

    SECTION("JPEG2000 driver available") {
        // JP2OpenJPEG or JP2ECW
        bool hasJp2 = checker.hasDriver("JP2OpenJPEG") || checker.hasDriver("JP2ECW");
        CHECK(hasJp2);
    }

    SECTION("HDF5 driver available") {
        bool hasHdf5 = checker.hasDriver("HDF5") || checker.hasDriver("HDF5Image");
        CHECK(hasHdf5);
    }

    SECTION("NetCDF driver available") {
        CHECK(checker.hasDriver("netCDF"));
    }

    SECTION("ENVI driver available") {
        CHECK(checker.hasDriver("ENVI"));
    }

    SECTION("Erdas Imagine driver available") {
        CHECK(checker.hasDriver("HFA"));
    }
}

TEST_CASE("Remote sensing format support", "[gdal][rs]") {
    GdalDriverChecker checker;

    SECTION("Sentinel-2 JP2 support") {
        CHECK(checker.hasDriver("JP2OpenJPEG"));
    }

    SECTION("Landsat GeoTIFF support") {
        CHECK(checker.hasDriver("GTiff"));
    }

    SECTION("MODIS HDF support") {
        bool hasHdf = checker.hasDriver("HDF4") || checker.hasDriver("HDF5");
        CHECK(hasHdf);
    }
}
