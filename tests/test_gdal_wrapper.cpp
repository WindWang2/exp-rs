// tests/test_gdal_wrapper.cpp — TDD Red phase for GDAL C API wrapper
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <vector>
#include <cmath>

static QString testDataPath()
{
    // Resolve relative to project root
    QString base = QFileInfo(__FILE__).absolutePath(); // tests/
    return QFileInfo(base + "/../data/sample_crops.tif").absoluteFilePath();
}

static QString testPhrPath()
{
    QString base = QFileInfo(__FILE__).absolutePath();
    return QFileInfo(base + "/../data/phr_xs.tif").absoluteFilePath();
}

static QString testLandsatPath()
{
    QString base = QFileInfo(__FILE__).absolutePath();
    // Prefer reorganized refs/qgis/; keep legacy qgis_ref/ for local trees
    const QStringList candidates = {
        base + "/../refs/qgis/tests/testdata/landsat.tif",
        base + "/../qgis_ref/tests/testdata/landsat.tif",
    };
    for ( const QString &c : candidates )
    {
        if ( QFileInfo::exists( c ) )
            return QFileInfo( c ).absoluteFilePath();
    }
    return QFileInfo( candidates.first() ).absoluteFilePath();
}

// --- Dataset open/close ---

TEST_CASE("GdalDatasetWrapper opens valid raster", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    REQUIRE_FALSE(ds.isValid());

    bool opened = ds.open(testDataPath());
    REQUIRE(opened);
    REQUIRE(ds.isValid());
}

TEST_CASE("GdalDatasetWrapper rejects invalid file", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    bool opened = ds.open("/nonexistent/file.tif");
    REQUIRE_FALSE(opened);
    REQUIRE_FALSE(ds.isValid());
}

TEST_CASE("GdalDatasetWrapper close releases dataset", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());
    REQUIRE(ds.isValid());

    ds.close();
    REQUIRE_FALSE(ds.isValid());
}

TEST_CASE("GdalDatasetWrapper open replaces previous dataset", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());
    REQUIRE(ds.isValid());
    int w1 = ds.width();

    ds.open(testPhrPath());
    REQUIRE(ds.isValid());
    int w2 = ds.width();

    REQUIRE(w1 != w2); // different files, different widths
}

// --- Metadata ---

TEST_CASE("GdalDatasetWrapper reads dimensions", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());

    REQUIRE(ds.width() == 512);
    REQUIRE(ds.height() == 512);
    REQUIRE(ds.bandCount() == 3);
}

TEST_CASE("GdalDatasetWrapper reads driver name", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());

    REQUIRE(ds.driverName() == "GTiff");
}

TEST_CASE("GdalDatasetWrapper reads geotransform", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());

    std::array<double, 6> gt = ds.geoTransform();
    REQUIRE(gt[0] == -10000.0); // origin X
    REQUIRE(gt[3] == 10000.0);  // origin Y
    REQUIRE_THAT(gt[1], Catch::Matchers::WithinAbs(39.0625, 0.001)); // pixel width
    REQUIRE_THAT(gt[5], Catch::Matchers::WithinAbs(-39.0625, 0.001)); // pixel height
}

TEST_CASE("GdalDatasetWrapper reads projection", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testLandsatPath()); // landsat.tif has UTM CRS

    QString proj = ds.projection();
    REQUIRE_FALSE(proj.isEmpty());
    REQUIRE(proj.contains("UTM"));
}

TEST_CASE("GdalDatasetWrapper handles empty projection gracefully", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath()); // sample_crops.tif has no CRS

    QString proj = ds.projection();
    // Empty projection is valid — just means CRS not defined
    REQUIRE(true); // API doesn't crash
}

// --- Band reading ---

TEST_CASE("GdalDatasetWrapper reads band data", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testPhrPath()); // phr_xs has non-zero data

    std::vector<float> buf(ds.width() * ds.height());
    bool ok = ds.readBandData(1, buf.data(), ds.width(), ds.height());
    REQUIRE(ok);

    // Verify we got some non-zero values
    bool hasNonZero = false;
    for (float v : buf) {
        if (v != 0.0f) { hasNonZero = true; break; }
    }
    REQUIRE(hasNonZero);
}

TEST_CASE("GdalDatasetWrapper readBandData rejects invalid band", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());

    std::vector<float> buf(ds.width() * ds.height());
    REQUIRE_FALSE(ds.readBandData(0, buf.data(), ds.width(), ds.height()));   // band 0 invalid
    REQUIRE_FALSE(ds.readBandData(99, buf.data(), ds.width(), ds.height()));  // band 99 invalid
}

TEST_CASE("GdalDatasetWrapper reads band no-data value", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testDataPath());

    bool hasNodata = false;
    double nodata = ds.bandNoDataValue(1, &hasNodata);
    // sample_crops.tif may or may not have nodata set; just verify the API works
    (void)nodata;
    (void)hasNodata;
    REQUIRE(true); // API doesn't crash
}

TEST_CASE("GdalDatasetWrapper reads single pixel value", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds;
    ds.open(testPhrPath());

    float val = 0.0f;
    bool ok = ds.readPixel(1, 0, 0, &val); // band 1, x=0, y=0
    REQUIRE(ok);
    // Just verify the API works; value depends on actual raster content
}

// --- RAII / move semantics ---

TEST_CASE("GdalDatasetWrapper move constructor transfers ownership", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds1;
    ds1.open(testDataPath());
    REQUIRE(ds1.isValid());

    GdalDatasetWrapper ds2(std::move(ds1));
    REQUIRE(ds2.isValid());
    REQUIRE_FALSE(ds1.isValid());
}

TEST_CASE("GdalDatasetWrapper move assignment transfers ownership", "[gdal][wrapper]")
{
    GdalDatasetWrapper ds1;
    ds1.open(testDataPath());

    GdalDatasetWrapper ds2;
    ds2 = std::move(ds1);
    REQUIRE(ds2.isValid());
    REQUIRE_FALSE(ds1.isValid());
}
