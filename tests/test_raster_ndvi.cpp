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

#include "processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.h"
#include "processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.h"
#include "processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_merge.h"
#include "processing/providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "operators/framework/rs_operator_error.h"

#include <processing/qgsprocessingprovider.h>
#include <processing/qgsprocessingregistry.h>
#include <qgsvectorlayer.h>
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>

#include <cmath>
#include <stdexcept>
#include <vector>
#include <atomic>

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

static QString createByteRaster(const QString &dir, const QString &name,
                                 int width, int height, const std::vector<unsigned char> &data,
                                 double pixelSize = 1.0)
{
    QString path = dir + "/" + name;
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) return {};

    GDALDatasetH dataset = GDALCreate(driver, path.toUtf8().constData(),
                                       width, height, 1, GDT_Byte, nullptr);
    if (!dataset) return {};

    double geoTransform[6] = {500000.0, pixelSize, 0.0, 4500000.0, 0.0, -pixelSize};
    GDALSetGeoTransform(dataset, geoTransform);
    GDALSetProjection(dataset, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

    GDALRasterBandH band = GDALGetRasterBand(dataset, 1);
    for (int row = 0; row < height; row++) {
        (void)GDALRasterIO(band, GF_Write, 0, row, width, 1,
                     const_cast<unsigned char*>(data.data() + row * width),
                     width, 1, GDT_Byte, 0, 0);
    }
    GDALClose(dataset);
    return path;
}

TEST_CASE("RasterMergeBandsAlgorithm mixed Byte+Float32 does not OOB (#1035)",
          "[raster][merge_bands][algorithm]")
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<unsigned char> bytes = {10, 20, 30, 40};
    // Little-endian float 1.0 is 00 00 80 3f — first byte 0. If eBufType is
    // wrongly Byte for this buffer, the written band becomes 0 not 1.
    const std::vector<float> floats = {1.0f, 2.0f, 3.0f, 4.0f};
    const QString bytePath = createByteRaster(dir.path(), "byte.tif", 2, 2, bytes);
    const QString floatPath = createTestRaster(dir.path(), "float.tif", 2, 2, floats);
    REQUIRE_FALSE(bytePath.isEmpty());
    REQUIRE_FALSE(floatPath.isEmpty());

    auto runMerge = [&](const QStringList &inputs, const QString &outName) -> QString {
        const QString outPath = dir.path() + "/" + outName;
        RasterMergeBandsAlgorithm alg;
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QVariantMap params;
        params.insert(QStringLiteral("INPUT_LAYERS"), inputs);
        params.insert(QStringLiteral("OUTPUT"), outPath);
        bool ok = true;
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        REQUIRE(ok);
        REQUIRE(QFile::exists(outPath));
        return outPath;
    };

    SECTION("Float32 then Byte — previously OOB-read the Byte heap block")
    {
        const QString outPath = runMerge({floatPath, bytePath}, QStringLiteral("merged_float_byte.tif"));
        GDALDatasetH ds = GDALOpen(outPath.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        REQUIRE(GDALGetRasterCount(ds) == 2);
        float band1[4] = {};
        float band2[4] = {};
        REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 2, 2, band1, 2, 2, GDT_Float32, 0, 0) == CE_None);
        REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 2, 2, band2, 2, 2, GDT_Float32, 0, 0) == CE_None);
        GDALClose(ds);
        CHECK(band1[0] == 1.0f);
        CHECK(band1[3] == 4.0f);
        CHECK(band2[0] == 10.0f);
        CHECK(band2[3] == 40.0f);
    }

    SECTION("Byte then Float32 — previously silently truncated the float buffer")
    {
        const QString outPath = runMerge({bytePath, floatPath}, QStringLiteral("merged_byte_float.tif"));
        GDALDatasetH ds = GDALOpen(outPath.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        REQUIRE(GDALGetRasterCount(ds) == 2);
        unsigned char band1[4] = {};
        unsigned char band2[4] = {};
        REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read, 0, 0, 2, 2, band1, 2, 2, GDT_Byte, 0, 0) == CE_None);
        REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read, 0, 0, 2, 2, band2, 2, 2, GDT_Byte, 0, 0) == CE_None);
        GDALClose(ds);
        CHECK(band1[0] == 10);
        CHECK(band1[3] == 40);
        CHECK(band2[0] == 1);
        CHECK(band2[3] == 4);
    }
}

TEST_CASE("Raster merge/calculator remove partial dest on cancel (#1043)",
          "[raster][algorithm][partial_output]")
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
    const std::vector<float> b = {10.0f, 20.0f, 30.0f, 40.0f};
    const QString aPath = createTestRaster(dir.path(), "a.tif", 2, 2, a);
    const QString bPath = createTestRaster(dir.path(), "b.tif", 2, 2, b);
    REQUIRE_FALSE(aPath.isEmpty());
    REQUIRE_FALSE(bPath.isEmpty());

    SECTION("raster_merge_bands unlinks dest when canceled mid-write")
    {
        const QString outPath = dir.path() + QStringLiteral("/merge_partial.tif");
        RasterMergeBandsAlgorithm alg;
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QObject::connect(&feedback, &QgsFeedback::progressChanged, [&feedback](double progress) {
            if (progress > 50.0)
                feedback.cancel();
        });
        QVariantMap params;
        params.insert(QStringLiteral("INPUT_LAYERS"), QStringList{aPath, bPath});
        params.insert(QStringLiteral("OUTPUT"), outPath);
        bool ok = true;
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        CHECK_FALSE(QFile::exists(outPath));
    }

    SECTION("raster_calculator unlinks dest when canceled during write")
    {
        const QString outPath = dir.path() + QStringLiteral("/calc_partial.tif");
        RasterCalculatorAlgorithm alg;
        QgsProcessingContext context;
        QgsProcessingFeedback feedback;
        QObject::connect(&feedback, &QgsFeedback::progressChanged, [&feedback](double progress) {
            if (progress >= 70.0)
                feedback.cancel();
        });
        QVariantMap params;
        params.insert(QStringLiteral("INPUT_LAYERS"), QStringList{aPath});
        params.insert(QStringLiteral("EXPRESSION"), QStringLiteral("b1 * 2"));
        params.insert(QStringLiteral("OUTPUT"), outPath);
        bool ok = true;
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        CHECK_FALSE(QFile::exists(outPath));
    }
}

TEST_CASE("VectorMergeAlgorithm throws when addFeature fails (#1043)",
          "[vector][merge][algorithm]")
{
    ensureQgis();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QgsProject project;
    auto *pts = new QgsVectorLayer(QStringLiteral("Point?crs=EPSG:4326"), QStringLiteral("pts"), QStringLiteral("memory"));
    auto *polys = new QgsVectorLayer(QStringLiteral("Polygon?crs=EPSG:4326"), QStringLiteral("polys"), QStringLiteral("memory"));
    REQUIRE(pts->isValid());
    REQUIRE(polys->isValid());
    QgsFeature pt;
    pt.setGeometry(QgsGeometry::fromWkt(QStringLiteral("POINT(0 0)")));
    REQUIRE(pts->dataProvider()->addFeature(pt));
    QgsFeature poly;
    poly.setGeometry(QgsGeometry::fromWkt(QStringLiteral("POLYGON((0 0,1 0,1 1,0 1,0 0))")));
    REQUIRE(polys->dataProvider()->addFeature(poly));
    project.addMapLayer(pts);
    project.addMapLayer(polys);

    VectorMergeAlgorithm alg;
    QgsProcessingContext context;
    context.setProject(&project);
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params.insert(QStringLiteral("INPUT_LAYERS"), QVariant::fromValue(QList<QgsMapLayer *>{pts, polys}));
    params.insert(QStringLiteral("OUTPUT"), dir.path() + QStringLiteral("/merged.shp"));
    bool ok = true;
    try
    {
        (void)alg.run(params, context, &feedback, &ok, QVariantMap(), false);
        FAIL("expected QgsProcessingException when a polygon cannot be written to a point sink");
    }
    catch (const QgsProcessingException &e)
    {
        const QString message = e.what();
        CHECK_FALSE(message.isEmpty());
    }
}

namespace {

std::atomic<int> g_probeStage{0};

class Issue1043ProbeAlgorithm : public QgsProcessingAlgorithm
{
public:
    QString name() const override { return QStringLiteral("issue1043_probe"); }
    QString displayName() const override { return QStringLiteral("issue1043 probe"); }
    QString group() const override { return QStringLiteral("test"); }
    QString groupId() const override { return QStringLiteral("test"); }
    QgsProcessingAlgorithm *createInstance() const override { return new Issue1043ProbeAlgorithm(); }

protected:
    void initAlgorithm(const QVariantMap &) override {}
    QVariantMap processAlgorithm(const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback *) override
    {
        g_probeStage.store(1);
        return {};
    }
};

class Issue1043ThrowAlgorithm : public QgsProcessingAlgorithm
{
public:
    QString name() const override { return QStringLiteral("issue1043_throw"); }
    QString displayName() const override { return QStringLiteral("issue1043 throw"); }
    QString group() const override { return QStringLiteral("test"); }
    QString groupId() const override { return QStringLiteral("test"); }
    QgsProcessingAlgorithm *createInstance() const override { return new Issue1043ThrowAlgorithm(); }

protected:
    void initAlgorithm(const QVariantMap &) override {}
    QVariantMap processAlgorithm(const QVariantMap &, QgsProcessingContext &, QgsProcessingFeedback *) override
    {
        throw std::runtime_error("boom");
    }
};

class Issue1043ProbeProvider : public QgsProcessingProvider
{
public:
    QString id() const override { return QStringLiteral("issue1043_probe"); }
    QString name() const override { return QStringLiteral("issue1043_probe"); }
    void loadAlgorithms() override
    {
        addAlgorithm(new Issue1043ProbeAlgorithm());
        addAlgorithm(new Issue1043ThrowAlgorithm());
    }
};

void ensureIssue1043ProbeProvider()
{
    auto *registry = QgsApplication::processingRegistry();
    if (!registry->providerById(QStringLiteral("issue1043_probe")))
        registry->addProvider(new Issue1043ProbeProvider());
}

} // namespace

TEST_CASE("ProviderAlgorithmAdapter postProcess(false) on cancel and std::exception (#1043)",
          "[processing][adapter][postprocess]")
{
    ensureQgis();
    ensureIssue1043ProbeProvider();
    auto *registry = QgsApplication::processingRegistry();
    const QgsProcessingAlgorithm *throwAlg = registry->algorithmById(QStringLiteral("issue1043_probe:issue1043_throw"));
    const QgsProcessingAlgorithm *probeAlg = registry->algorithmById(QStringLiteral("issue1043_probe:issue1043_probe"));
    REQUIRE(throwAlg != nullptr);
    REQUIRE(probeAlg != nullptr);

    SECTION("non-QgsProcessingException still runs postProcess(false) then rethrows")
    {
        sicnu::processing::ProviderAlgorithmAdapter adapter(*throwAlg);
        REQUIRE_THROWS_AS(adapter.execute(Json::Value(Json::objectValue)), std::runtime_error);
    }

    SECTION("cancel after runPrepared calls postProcess(false) then Cancelled")
    {
        g_probeStage.store(0);
        sicnu::processing::ProviderAlgorithmAdapter adapter(*probeAlg);
        try
        {
            (void)adapter.execute(Json::Value(Json::objectValue), nullptr,
                                  []() { return g_probeStage.load() > 0; });
            FAIL("expected RSOperatorError Cancelled");
        }
        catch (const sicnu::operators::RSOperatorError &e)
        {
            CHECK(e.code() == sicnu::operators::ErrorCode::Cancelled);
        }
    }
}
