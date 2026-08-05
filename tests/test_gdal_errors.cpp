// tests/test_gdal_errors.cpp — TDD Red phase for GDAL error handler
#include <catch2/catch_test_macros.hpp>

#include "processing/gdal/gdal_error_handler.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gdal.h>
#include <cpl_conv.h>

// A small valid GeoTIFF synthesised at runtime so the suite does not depend on
// a committed sample raster under data/.
static QString validRasterPath()
{
  static QTemporaryDir dir;
  static const QString path = []() {
    GDALAllRegister();
    const QString p = dir.path() + QStringLiteral( "/sample.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
    return p;
  }();
  return path;
}

// --- Error handler basics ---

static QString invalidRasterPath()
{
    // Create a path that exists but is not a valid raster — triggers GDAL error
    return QFileInfo(__FILE__).absolutePath() + "/../CMakeLists.txt"; // text file, not raster
}

TEST_CASE("GdalErrorHandler captures GDAL errors", "[gdal][errors]")
{
    GdalErrorHandler handler;
    handler.install();

    // Trigger a GDAL error by opening a non-raster file
    GdalDatasetWrapper ds;
    ds.open(invalidRasterPath());

    REQUIRE(handler.hasError());
    REQUIRE_FALSE(handler.lastErrorMessage().isEmpty());
}

TEST_CASE("GdalErrorHandler clear resets error state", "[gdal][errors]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    ds.open(invalidRasterPath());
    REQUIRE(handler.hasError());

    handler.clear();
    REQUIRE_FALSE(handler.hasError());
    REQUIRE(handler.lastErrorMessage().isEmpty());
}

TEST_CASE("GdalErrorHandler captures error severity", "[gdal][errors]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    ds.open(invalidRasterPath());

    // GDAL open failure typically produces CE_Failure
    REQUIRE(handler.lastErrorSeverity() >= CE_Failure);
}

TEST_CASE("GdalErrorHandler no error on successful open", "[gdal][errors]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    ds.open(validRasterPath());

    REQUIRE_FALSE(handler.hasError());
}

TEST_CASE("GdalErrorHandler captures error number", "[gdal][errors]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    ds.open(invalidRasterPath());

    REQUIRE(handler.lastErrorNumber() != 0);
}

// --- Integration with GdalDatasetWrapper ---

TEST_CASE("GdalDatasetWrapper reports error message on failure", "[gdal][errors][wrapper]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    bool opened = ds.open(invalidRasterPath());

    REQUIRE_FALSE(opened);
    REQUIRE_FALSE(ds.lastError().isEmpty());
}

TEST_CASE("GdalDatasetWrapper no error on success", "[gdal][errors][wrapper]")
{
    GdalErrorHandler handler;
    handler.install();

    GdalDatasetWrapper ds;
    ds.open(validRasterPath());

    REQUIRE(ds.lastError().isEmpty());
}

// --- Thread safety: handler is per-instance ---

TEST_CASE("GdalErrorHandler instances are independent", "[gdal][errors]")
{
    GdalErrorHandler handler1;
    GdalErrorHandler handler2;
    handler1.install();

    GdalDatasetWrapper ds;
    ds.open(invalidRasterPath());

    // handler1 should see the error
    REQUIRE(handler1.hasError());
    // handler2 is not installed, should not see it
    REQUIRE_FALSE(handler2.hasError());
}
