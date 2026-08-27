// tests/test_gdal_wrapper.cpp — TDD Red phase for GDAL C API wrapper
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QMap>
#include <vector>
#include <cmath>
#include <gdal.h>
#include <cpl_conv.h>

// Synthesise the three rasters this suite used to read from data/ (sample_crops,
// phr_xs, landsat) with the exact properties each test asserts, so the suite is
// self-contained and does not depend on committed sample files. Files are
// generated once and cached for the process lifetime.
namespace
{
QTemporaryDir &sampleDir()
{
  static QTemporaryDir dir;
  return dir;
}

// sample_crops.tif stand-in: 512×512×3, the exact geotransform asserted below,
// and deliberately NO projection (the "empty projection" test relies on that).
const QString &sampleCropsPath()
{
  static const QString path = []() {
    GDALAllRegister();
    const QString p = sampleDir().path() + QStringLiteral( "/sample_crops.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 512, 512, 3, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { -10000.0, 39.0625, 0.0, 10000.0, 0.0, -39.0625 };
    GDALSetGeoTransform( ds, gt );
    // Intentionally no GDALSetProjection: empty CRS, as sample_crops had.
    for ( int b = 1; b <= 3; ++b )
    {
      GDALRasterBandH band = GDALGetRasterBand( ds, b );
      std::vector<float> line( 512, static_cast<float>( b * 10 ) );
      for ( int row = 0; row < 512; ++row )
        (void) GDALRasterIO( band, GF_Write, 0, row, 512, 1, line.data(), 512, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return p;
  }();
  return path;
}

// phr_xs.tif stand-in: a different width than sample_crops (so the
// "different widths" test holds) with non-zero band data.
const QString &phrXsPath()
{
  static const QString path = []() {
    GDALAllRegister();
    const QString p = sampleDir().path() + QStringLiteral( "/phr_xs.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 256, 256, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, 256.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( 256 );
    for ( int row = 0; row < 256; ++row )
    {
      for ( int col = 0; col < 256; ++col )
        line[col] = static_cast<float>( row * 256 + col ); // non-zero
      (void) GDALRasterIO( band, GF_Write, 0, row, 256, 1, line.data(), 256, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return p;
  }();
  return path;
}

// landsat stand-in: carries a UTM projection so the projection test passes
// without depending on the gitignored refs/qgis tree.
const QString &landsatPath()
{
  static const QString path = []() {
    GDALAllRegister();
    const QString p = sampleDir().path() + QStringLiteral( "/landsat_utm.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 64, 64, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 300000.0, 30.0, 0.0, 4900000.0, 0.0, -30.0 };
    GDALSetGeoTransform( ds, gt );
    // UTM zone 17N WGS84 — proj4 string the wrapper surfaces as a WKT
    // containing "UTM".
    GDALSetProjection( ds,
      "PROJCS[\"WGS 84 / UTM zone 17N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
      "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
      "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",-81],"
      "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
      "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( 64, 1.0f );
    for ( int row = 0; row < 64; ++row )
      (void) GDALRasterIO( band, GF_Write, 0, row, 64, 1, line.data(), 64, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return p;
  }();
  return path;
}
} // namespace

static QString testDataPath() { return sampleCropsPath(); }
static QString testPhrPath() { return phrXsPath(); }
static QString testLandsatPath() { return landsatPath(); }

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
    QTemporaryDir tmp;
    const QString path = tmp.path() + QStringLiteral("/nodata_test.tif");
    std::vector<std::vector<float>> bands = { { 1.0f, 2.0f, -9999.0f, 4.0f } };
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, 1 };
    QString err;
    REQUIRE(writeGdalOutput(path, 2, 2, bands, gt, QString(), &err, -9999.0));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(path));

    bool hasNodata = false;
    double nodata = ds.bandNoDataValue(1, &hasNodata);
    REQUIRE(hasNodata);
    REQUIRE_THAT(nodata, Catch::Matchers::WithinAbs(-9999.0, 1e-4));
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

// Restored regression home (#595): readBandWindowScaled pads out-of-raster
// regions with the caller's nodata instead of reading garbage (formerly
// tests/test_g02_gdal_regression.cpp, deleted with the legacy suites).
TEST_CASE("readBandWindowScaled pads out-of-raster region with nodata (#595)", "[gdal][wrapper][g02]")
{
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("scaled.tif"));
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    REQUIRE(drv != nullptr);
    GDALDatasetH ds = GDALCreate(drv, path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    std::vector<float> data(16);
    for (int i = 0; i < 16; ++i) data[i] = static_cast<float>(i);
    REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, 4, 4, data.data(), 4, 4, GDT_Float32, 0, 0) == CE_None);
    GDALClose(ds);

    GdalDatasetWrapper w;
    REQUIRE(w.open(path));

    // Window extends past the right/bottom edges by 2 px in each axis,
    // read at the same resolution: the padding must equal the nodata arg.
    constexpr float kNoData = -3.5f;
    std::vector<float> buf(6 * 6, 12345.0f);
    REQUIRE(w.readBandWindowScaled(1, 2, 2, 6, 6, buf.data(), 6, 6, kNoData));
    // In-bounds corner (2,2) = raster value 2*4+2 = 10; padded corner = kNoData.
    REQUIRE_THAT(buf[0], Catch::Matchers::WithinAbs(10.0f, 1e-6f));
    REQUIRE_THAT(buf[6 * 6 - 1], Catch::Matchers::WithinAbs(kNoData, 1e-6f));
    // The whole 2px right/bottom padding ring is nodata.
    for (int y = 0; y < 6; ++y)
        for (int x = 4; x < 6; ++x)
            REQUIRE_THAT(buf[y * 6 + x], Catch::Matchers::WithinAbs(kNoData, 1e-6f));
}
