// tests/test_raster_ndvi.cpp — Test RasterNdviAlgorithm
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsapplication.h>
#include <qgsexception.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>

#include "processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.h"
#include "processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.h"
#include "processing/providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.h"
#include "processing/algorithms/spectral_indices.h"

#include <cmath>
#include <vector>

namespace {

void ensureQgis()
{
    if (QgsApplication::instance())
        return;
    static int argc = 1;
    static char name[] = "test_raster_ndvi";
    static char *argv[] = {name, nullptr};
    static auto *app = new QgsApplication(argc, argv, false);
    (void)app;
    QgsApplication::initQgis();
}

} // namespace

// Helper: create a small GeoTIFF with known float values
static QString createTestRaster(const QString &dir, const QString &name,
                                  int width, int height, const std::vector<float> &data,
                                  double noDataValue = -9999.0,
                                  double pixelSize = 1.0)
{
    QString path = dir + "/" + name;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) return {};

    GDALDatasetH dataset = GDALCreate(driver, path.toUtf8().constData(),
                                       width, height, 1, GDT_Float32, nullptr);
    if (!dataset) return {};

    // Set a simple geotransform and projection
    double geoTransform[6] = {500000.0, pixelSize, 0.0, 4500000.0, 0.0, -pixelSize};
    GDALSetGeoTransform(dataset, geoTransform);
    GDALSetProjection(dataset, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

    GDALRasterBandH band = GDALGetRasterBand(dataset, 1);
    GDALSetRasterNoDataValue(band, noDataValue);

    // Write data row by row
    for (int row = 0; row < height; row++) {
        (void)GDALRasterIO(band, GF_Write, 0, row, width, 1,
                     const_cast<float*>(data.data() + row * width),
                     width, 1, GDT_Float32, 0, 0);
    }

    GDALClose(dataset);
    return path;
}

// Helper: create a small GeoTIFF of an explicit GDAL type with known values.
static QString createTypedRaster(const QString &dir, const QString &name,
                                 int width, int height, GDALDataType type,
                                 const void *data)
{
    QString path = dir + "/" + name;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) return {};

    GDALDatasetH dataset = GDALCreate(driver, path.toUtf8().constData(),
                                      width, height, 1, type, nullptr);
    if (!dataset) return {};

    double geoTransform[6] = {500000.0, 1.0, 0.0, 4500000.0, 0.0, -1.0};
    GDALSetGeoTransform(dataset, geoTransform);

    GDALRasterBandH band = GDALGetRasterBand(dataset, 1);
    for (int row = 0; row < height; row++) {
        const char *rowPtr = static_cast<const char *>(data)
            + static_cast<size_t>(row) * width * GDALGetDataTypeSizeBytes(type);
        (void)GDALRasterIO(band, GF_Write, 0, row, width, 1,
                           const_cast<char *>(rowPtr), width, 1, type, 0, 0);
    }

    GDALClose(dataset);
    return path;
}

TEST_CASE("RasterNdviAlgorithm metadata", "[raster][ndvi][algorithm]") {
    RasterNdviAlgorithm alg;
    CHECK(alg.name() == "raster_ndvi");
    CHECK_FALSE(alg.displayName().isEmpty());
    CHECK(alg.group() == "Raster");
    CHECK(alg.groupId() == "raster");
    CHECK_FALSE(alg.tags().isEmpty());
}

TEST_CASE("SpectralIndices::ndvi with algorithm-like data", "[spectral][ndvi][integration]") {
    // Test that SpectralIndices::ndvi works with the same data the algorithm would use
    std::vector<float> red = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.0f};
    std::vector<float> nir = {0.5f, 0.8f, 0.0f, 0.3f, 0.7f, 0.0f};
    std::vector<float> out(6);

    bool ok = SpectralIndices::ndvi(nir.data(), red.data(), out.data(), 6);
    REQUIRE(ok);

    // Expected NDVI values:
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.4f / 0.6f, 0.001f)); // (0.5-0.1)/(0.5+0.1)
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.6f / 1.0f, 0.001f)); // (0.8-0.2)/(0.8+0.2)
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(-1.0f, 0.001f));        // (0.0-0.3)/(0.0+0.3)
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(-0.1f / 0.7f, 0.001f)); // (0.3-0.4)/(0.3+0.4)
    REQUIRE_THAT(out[4], Catch::Matchers::WithinAbs(0.2f / 1.2f, 0.001f)); // (0.7-0.5)/(0.7+0.5)
    REQUIRE(std::isnan(out[5])); // 0/0 → NaN
}

TEST_CASE("SpectralIndices::ndvi range validation", "[spectral][ndvi][range]") {
    // All NDVI values should be in [-1, 1] for non-NaN pixels
    std::vector<float> red = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f};
    std::vector<float> nir = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f};
    std::vector<float> out(9);

    bool ok = SpectralIndices::ndvi(nir.data(), red.data(), out.data(), 9);
    REQUIRE(ok);

    for (int i = 0; i < 9; i++) {
        if (!std::isnan(out[i])) {
            REQUIRE(out[i] >= -1.0f);
            REQUIRE(out[i] <= 1.0f);
        }
    }
}

TEST_CASE("RasterNdviAlgorithm refuses mismatched pixel size without warping (#935)",
          "[raster][ndvi][algorithm][grid]")
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<float> red = {0.1f, 0.2f, 0.3f, 0.4f};
    const std::vector<float> nir = {0.5f, 0.8f, 0.3f, 0.7f};
    const QString redPath = createTestRaster(dir.path(), "red_30m.tif", 2, 2, red, -9999.0, 30.0);
    const QString nirPath = createTestRaster(dir.path(), "nir_15m.tif", 2, 2, nir, -9999.0, 15.0);
    REQUIRE_FALSE(redPath.isEmpty());
    REQUIRE_FALSE(nirPath.isEmpty());
    const QString outPath = dir.path() + QStringLiteral("/ndvi_warped.tif");

    RasterNdviAlgorithm alg;
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params.insert(QStringLiteral("RED_BAND"), redPath);
    params.insert(QStringLiteral("NIR_BAND"), nirPath);
    params.insert(QStringLiteral("OUTPUT"), outPath);

    bool ok = true;
    try
    {
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        FAIL("expected QgsProcessingException for pixel-size mismatch");
    }
    catch (const QgsProcessingException &e)
    {
        const QString message = e.what();
        CHECK(message.contains(QStringLiteral("mismatch"), Qt::CaseInsensitive));
        CHECK((message.contains(QStringLiteral("pixel"), Qt::CaseInsensitive) ||
               message.contains(QStringLiteral("grid"), Qt::CaseInsensitive)));
    }
    CHECK_FALSE(QFile::exists(outPath));
}

TEST_CASE("RasterMergeBandsAlgorithm merges mixed-type bands with per-band buffer types (#1035)",
          "[raster][merge_bands][algorithm][dtype]")
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<GByte> byteData = {10, 20, 30, 40};
    const std::vector<float> floatData = {1.2f, 2.7f, 3.1f, 4.9f};
    const QString bytePath = createTypedRaster(dir.path(), "byte.tif", 2, 2, GDT_Byte, byteData.data());
    const QString floatPath = createTypedRaster(dir.path(), "float.tif", 2, 2, GDT_Float32, floatData.data());
    REQUIRE_FALSE(bytePath.isEmpty());
    REQUIRE_FALSE(floatPath.isEmpty());

    auto byteLayer = std::make_unique<QgsRasterLayer>(bytePath, "byte", "gdal");
    auto floatLayer = std::make_unique<QgsRasterLayer>(floatPath, "float", "gdal");
    REQUIRE(byteLayer->isValid());
    REQUIRE(floatLayer->isValid());

    QVariantList layersByteFirst = {
        QVariant::fromValue(static_cast<QgsMapLayer *>(byteLayer.get())),
        QVariant::fromValue(static_cast<QgsMapLayer *>(floatLayer.get()))
    };
    QVariantList layersFloatFirst = {
        QVariant::fromValue(static_cast<QgsMapLayer *>(floatLayer.get())),
        QVariant::fromValue(static_cast<QgsMapLayer *>(byteLayer.get()))
    };

    // Order A: Byte first. The output band type is the GDALDataTypeUnion of
    // every input band type (1d241e599, #1035): Byte+Float32 ⇒ GDT_Float32,
    // so no input band is silently truncated — byte values survive exactly
    // and float values survive exactly. The historical bug passed blocks[0]'s
    // GDT_Byte as the buffer type for the float block: GDAL then read one
    // byte per pixel out of a 4-byte-per-pixel buffer (out-of-bounds read);
    // per-band buffer types fixed the read, and the union contract fixed the
    // silent precision loss. The dedicated suite for this contract is
    // test_raster_merge_bands.cpp.
    const QString outA = dir.path() + QStringLiteral("/merged_a.tif");
    {
        RasterMergeBandsAlgorithm alg;
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params.insert(QStringLiteral("INPUT_LAYERS"), layersByteFirst);
        params.insert(QStringLiteral("OUTPUT"), outA);

        bool ok = false;
        const QVariantMap res = alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        REQUIRE(ok);
        REQUIRE(res.value(QStringLiteral("OUTPUT")).toString() == outA);

        GDALDatasetH ds = GDALOpen(outA.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        CHECK(GDALGetRasterCount(ds) == 2);
        CHECK(GDALGetRasterDataType(GDALGetRasterBand(ds, 1)) == GDT_Float32);
        std::vector<float> band1(4), band2(4);
        CHECK(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 2, 2, band1.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
        CHECK(GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 2, 2, band2.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
        GDALClose(ds);
        for (int i = 0; i < 4; ++i)
        {
            CHECK_THAT(band1[i], Catch::Matchers::WithinAbs(static_cast<float>(byteData[i]), 1e-4));
            CHECK_THAT(band2[i], Catch::Matchers::WithinAbs(floatData[i], 1e-4));
        }
    }

    // Order B: Float32 first ⇒ output GDT_Float32. Band 2 (a BYTE block) must
    // convert exactly (10/20/30/40 as floats). The historical bug declared the
    // byte buffer as GDT_Float32: GDAL read 4× the bytes out of a 1-byte-per-
    // pixel heap block (out-of-bounds read).
    const QString outB = dir.path() + QStringLiteral("/merged_b.tif");
    {
        RasterMergeBandsAlgorithm alg;
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params.insert(QStringLiteral("INPUT_LAYERS"), layersFloatFirst);
        params.insert(QStringLiteral("OUTPUT"), outB);

        bool ok = false;
        const QVariantMap res = alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        REQUIRE(ok);
        REQUIRE(res.value(QStringLiteral("OUTPUT")).toString() == outB);

        GDALDatasetH ds = GDALOpen(outB.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        CHECK(GDALGetRasterCount(ds) == 2);
        CHECK(GDALGetRasterDataType(GDALGetRasterBand(ds, 1)) == GDT_Float32);
        std::vector<float> band1(4), band2(4);
        CHECK(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 2, 2, band1.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
        CHECK(GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 2, 2, band2.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
        GDALClose(ds);
        for (int i = 0; i < 4; ++i)
        {
            CHECK_THAT(band1[i], Catch::Matchers::WithinAbs(floatData[i], 1e-4));
            CHECK_THAT(band2[i], Catch::Matchers::WithinAbs(static_cast<float>(byteData[i]), 1e-4));
        }
    }
}

TEST_CASE("SpectralIndexAlgorithm refuses mismatched pixel size without warping (#935)",
          "[raster][spectral_index][algorithm][grid]")
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<float> red = {0.1f, 0.2f, 0.3f, 0.4f};
    const std::vector<float> nir = {0.5f, 0.8f, 0.3f, 0.7f};
    const QString redPath = createTestRaster(dir.path(), "si_red_30m.tif", 2, 2, red, -9999.0, 30.0);
    const QString nirPath = createTestRaster(dir.path(), "si_nir_15m.tif", 2, 2, nir, -9999.0, 15.0);
    REQUIRE_FALSE(redPath.isEmpty());
    REQUIRE_FALSE(nirPath.isEmpty());
    const QString outPath = dir.path() + QStringLiteral("/si_warped.tif");

    SpectralIndexAlgorithm alg;
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params.insert(QStringLiteral("INDEX"), 0); // NDVI
    params.insert(QStringLiteral("RED_BAND"), redPath);
    params.insert(QStringLiteral("NIR_BAND"), nirPath);
    params.insert(QStringLiteral("OUTPUT"), outPath);

    bool ok = true;
    try
    {
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        FAIL("expected QgsProcessingException for pixel-size mismatch");
    }
    catch (const QgsProcessingException &e)
    {
        const QString message = e.what();
        CHECK(message.contains(QStringLiteral("mismatch"), Qt::CaseInsensitive));
        CHECK((message.contains(QStringLiteral("pixel"), Qt::CaseInsensitive) ||
               message.contains(QStringLiteral("grid"), Qt::CaseInsensitive)));
    }
    CHECK_FALSE(QFile::exists(outPath));
}
