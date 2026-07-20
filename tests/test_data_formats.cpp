// Data Format tests — verify GDAL driver support for RS formats
#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

#include <string>

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

TEST_CASE("ENVI raster open via data file", "[gdal][envi]") {
    GdalDriverChecker checker;
    REQUIRE(checker.hasDriver("ENVI"));

    // Prefer project data samples when present (gitignored large rasters)
    const char *candidates[] = {
        "data/dem.dat",
        "data/CCD1.dat",
        "data/GF_1",
    };

    bool openedAny = false;
    for (const char *path : candidates) {
        GDALDatasetH ds = GDALOpen(path, GA_ReadOnly);
        if (!ds)
            continue;
        openedAny = true;
        CHECK(GDALGetRasterXSize(ds) > 0);
        CHECK(GDALGetRasterYSize(ds) > 0);
        CHECK(GDALGetRasterCount(ds) >= 1);
        const char *driverName = GDALGetDriverShortName(GDALGetDatasetDriver(ds));
        CHECK(std::string(driverName ? driverName : "") == "ENVI");
        GDALClose(ds);
    }

    // Skip soft if sample ENVI files not checked out locally
    if (!openedAny) {
        WARN("No local ENVI samples under data/; driver presence already checked");
    }
}

TEST_CASE("ENVI header alone is not openable", "[gdal][envi]") {
    GdalDriverChecker checker;
    REQUIRE(checker.hasDriver("ENVI"));

    GDALDatasetH ds = GDALOpen("data/dem.hdr", GA_ReadOnly);
    // GDAL refuses .hdr; open path must be the binary data file
    CHECK(ds == nullptr);
}
