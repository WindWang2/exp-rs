// Native RS operator tests — verify schema, registration, and execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include <cmath>
#include <vector>

#include <json/json.h>

#include "processing/algorithms/satellite_products.h"

#include <gdal_priv.h>
#include <ogr_api.h>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "analysis/segmentation/rs_otb_segmenter.h"

using namespace sicnu::operators;

namespace {

std::string writeTestRaster(const QString &path, int width, int height,
                            const std::vector<std::vector<float>> &bands) {
    ensureGdalInit();
    std::array<double, 6> geoTransform = {0, 1, 0, 0, 0, -1};
    QString error;
    if (!writeGdalOutput(path, width, height, bands, geoTransform, "EPSG:4326", &error)) {
        return error.toStdString();
    }
    return {};
}

/// Create a single-layer GPKG with one integer field and one axis-aligned
/// square feature. Coordinates are map units of a raster with GT
/// {0,1,0,0,0,-1} (i.e. rows run into negative y).
void writeTestVector(const QString &path, const QString &fieldName, int classId,
                     double x0, double y0, double x1, double y1, bool append = false) {
    GDALDriverH drv = GDALGetDriverByName("GPKG");
    REQUIRE(drv != nullptr);
    GDALDatasetH ds = append
        ? GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_UPDATE,
                     nullptr, nullptr, nullptr)
        : GDALCreate(drv, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
    REQUIRE(ds != nullptr);
    OGRLayerH lyr = append
        ? GDALDatasetGetLayer(ds, 0)
        : GDALDatasetCreateLayer(ds, "training", nullptr, wkbPolygon, nullptr);
    REQUIRE(lyr != nullptr);

    int fieldIdx = 0;
    if (!append) {
        OGRFieldDefnH fld = OGR_Fld_Create(fieldName.toUtf8().constData(), OFTInteger);
        REQUIRE(OGR_L_CreateField(lyr, fld, true) == OGRERR_NONE);
        OGR_Fld_Destroy(fld);
    }

    OGRGeometryH ring = OGR_G_CreateGeometry(wkbLinearRing);
    OGR_G_AddPoint_2D(ring, x0, y0);
    OGR_G_AddPoint_2D(ring, x1, y0);
    OGR_G_AddPoint_2D(ring, x1, y1);
    OGR_G_AddPoint_2D(ring, x0, y1);
    OGR_G_AddPoint_2D(ring, x0, y0);
    OGRGeometryH poly = OGR_G_CreateGeometry(wkbPolygon);
    REQUIRE(OGR_G_AddGeometryDirectly(poly, ring) == OGRERR_NONE);

    OGRFeatureH feat = OGR_F_Create(OGR_L_GetLayerDefn(lyr));
    OGR_F_SetFieldInteger(feat, fieldIdx, classId);
    REQUIRE(OGR_F_SetGeometryDirectly(feat, poly) == OGRERR_NONE);
    REQUIRE(OGR_L_CreateFeature(lyr, feat) == OGRERR_NONE);
    OGR_F_Destroy(feat);
    GDALClose(ds);
}

/// Two-class 16x16 raster (left half dark, right half bright) + matching
/// training polygons (class 1 left, class 2 right).
void makeTwoClassFixture(const QString &rasterPath, const QString &vectorPath) {
    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<std::vector<float>> bands(2);
    for (int b = 0; b < 2; ++b) {
        bands[b].resize(W * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                bands[b][y * W + x] = (x < W / 2)
                    ? static_cast<float>(10 * (b + 1))
                    : static_cast<float>(100 * (b + 1));
    }
    REQUIRE(writeTestRaster(rasterPath, W, H, bands).empty());
    writeTestVector(vectorPath, QStringLiteral("class_id"), 1, 0, -16, 8, 0);
    writeTestVector(vectorPath, QStringLiteral("class_id"), 2, 8, -16, 16, 0, true);
}

} // namespace

TEST_CASE("Native RS operators are registered", "[operators][rs]") {
    auto& registry = RSOperatorRegistry::instance();

    CHECK(registry.hasOperator("rs:spectral_index"));
    CHECK(registry.hasOperator("rs:band_math"));
    CHECK(registry.hasOperator("rs:atmospheric_correction"));
    CHECK(registry.hasOperator("rs:radiometric_calibration"));
    CHECK(registry.hasOperator("rs:change_detection"));
    CHECK(registry.hasOperator("rs:post_classification_change"));
    CHECK(registry.hasOperator("rs:qa_mask"));
    CHECK(registry.hasOperator("rs:apply_mask"));
    CHECK(registry.hasOperator("rs:image_fusion"));
    CHECK(registry.hasOperator("rs:terrain_analysis"));
    CHECK(registry.hasOperator("rs:pca"));
    CHECK(registry.hasOperator("rs:mosaic"));
    CHECK(registry.hasOperator("rs:sam_classify"));
    CHECK(registry.hasOperator("rs:continuum_removal"));
#ifdef SICNU_HAS_OPENCV
    CHECK(registry.hasOperator("rs:kmeans_classification"));
    CHECK(registry.hasOperator("rs:supervised_classification"));
    CHECK(registry.hasOperator("rs:obia_segment"));
    CHECK(registry.hasOperator("rs:obia_classify"));
    CHECK(registry.hasOperator("rs:obia_hierarchy")); // ADR 0060: registration + smoke
    CHECK(registry.hasOperator("rs:segment_stats"));
    CHECK(registry.hasOperator("rs:infer")); // Edge-AI tracer bullet (#141)
#endif
}

TEST_CASE("RS radiometric calibration operator execution", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/calibrated.tif";
    const QString mtlPath = tmp.path() + "/LC08_MTL.txt";

    // 2x2 single-band raster, band described as "B4", DN = [100, 200, 50, 80]
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(inputPath, 2, 2, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> dn = {100.0f, 200.0f, 50.0f, 80.0f};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, 1), GF_Write, 0, 0, 2, 2,
                         dn.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(GDALGetRasterBand(srcDs, 1), "B4");
    GDALClose(srcDs);

    {
        QFile mtl(mtlPath);
        REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ts(&mtl);
        ts << "SPACECRAFT_ID = \"LANDSAT_8\"\n"
           << "SUN_ELEVATION = 45.0\n"
           << "RADIANCE_MULT_BAND_4 = 0.01\n"
           << "RADIANCE_ADD_BAND_4 = 0.0\n";
    }

    auto op = RSOperatorRegistry::instance().create("rs:radiometric_calibration");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["metadata_path"] = mtlPath.toStdString();
    params["unit"] = "radiance";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["unit"].asString() == "radiance");
    CHECK(result["bandCount"].asInt() == 1);
    CHECK(QFile::exists(outputPath));

    // L = 0.01 * DN (streamed tile path)
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.width() == 2);
    CHECK(ds.height() == 2);
    std::vector<float> out(4);
    REQUIRE(ds.readBandData(1, out.data(), 2, 2));
    CHECK(out[0] == Catch::Approx(1.0f).epsilon(1e-3f)); // 0.01*100
    CHECK(out[1] == Catch::Approx(2.0f).epsilon(1e-3f)); // 0.01*200
    CHECK(out[2] == Catch::Approx(0.5f).epsilon(1e-3f)); // 0.01*50
    CHECK(out[3] == Catch::Approx(0.8f).epsilon(1e-3f)); // 0.01*80

    // The calibrated output records its radiometric state (radiance).
    CHECK(SatelliteProducts::readRadiometricState(outputPath)
          == QString::fromUtf8(SatelliteProducts::kRadiometricStateRadiance));
}

TEST_CASE("RS radiometric calibration auto-detects a sibling MTL", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // The input raster and its MTL live in the same directory; the operator
    // must resolve the metadata without an explicit metadata_path.
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/calibrated.tif";
    const QString mtlPath = tmp.path() + "/LC08_L1TP_MTL.txt";

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(inputPath, 2, 2, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> dn = {100.0f, 200.0f, 50.0f, 80.0f};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, 1), GF_Write, 0, 0, 2, 2,
                         dn.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(GDALGetRasterBand(srcDs, 1), "B4");
    GDALClose(srcDs);

    {
        QFile mtl(mtlPath);
        REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ts(&mtl);
        ts << "SPACECRAFT_ID = \"LANDSAT_8\"\n"
           << "SUN_ELEVATION = 45.0\n"
           << "RADIANCE_MULT_BAND_4 = 0.01\n"
           << "RADIANCE_ADD_BAND_4 = 0.0\n";
    }

    auto op = RSOperatorRegistry::instance().create("rs:radiometric_calibration");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["unit"] = "radiance";
    // No metadata_path: resolved from the sibling *_MTL.txt.

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["unit"].asString() == "radiance");
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(4);
    REQUIRE(ds.readBandData(1, out.data(), 2, 2));
    CHECK(out[0] == Catch::Approx(1.0f).epsilon(1e-3f)); // 0.01*100 via auto-detected MTL
    CHECK(out[3] == Catch::Approx(0.8f).epsilon(1e-3f)); // 0.01*80
}

TEST_CASE("RS atmospheric correction resolves gain/bias from sibling MTL", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Band 1 is described as B4; its radiance coefficients live in a sibling
    // MTL. The operator must resolve them without explicit gain/bias params.
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/corrected.tif";
    const QString mtlPath = tmp.path() + "/LC08_L1TP_MTL.txt";

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(inputPath, 2, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> dn = {100.0f, 200.0f};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, 1), GF_Write, 0, 0, 2, 1,
                         dn.data(), 2, 1, GDT_Float32, 0, 0) == CE_None);
    GDALSetDescription(GDALGetRasterBand(srcDs, 1), "B4");
    GDALClose(srcDs);

    {
        QFile mtl(mtlPath);
        REQUIRE(mtl.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ts(&mtl);
        ts << "SPACECRAFT_ID = \"LANDSAT_8\"\n"
           << "SUN_ELEVATION = 45.0\n"
           << "RADIANCE_MULT_BAND_4 = 0.01\n"
           << "RADIANCE_ADD_BAND_4 = 0.0\n";
    }

    auto op = RSOperatorRegistry::instance().create("rs:atmospheric_correction");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["band"] = 1;
    params["method"] = "dn_to_radiance";
    // No gain/bias/metadata_path: resolved from the sibling MTL.

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["method"].asString() == "dn_to_radiance");
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(2);
    REQUIRE(ds.readBandData(1, out.data(), 2, 1));
    CHECK(out[0] == Catch::Approx(1.0f).epsilon(1e-3f)); // 0.01*100
    CHECK(out[1] == Catch::Approx(2.0f).epsilon(1e-3f)); // 0.01*200

    // dn_to_radiance records the radiance state.
    CHECK(SatelliteProducts::readRadiometricState(outputPath)
          == QString::fromUtf8(SatelliteProducts::kRadiometricStateRadiance));
}

TEST_CASE("RS atmospheric correction explicit gain/bias still win", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/corrected.tif";

    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    GDALDatasetH srcDs = createOutputTiff(inputPath, 2, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> dn = {100.0f, 200.0f};
    REQUIRE(GDALRasterIO(GDALGetRasterBand(srcDs, 1), GF_Write, 0, 0, 2, 1,
                         dn.data(), 2, 1, GDT_Float32, 0, 0) == CE_None);
    GDALClose(srcDs);

    auto op = RSOperatorRegistry::instance().create("rs:atmospheric_correction");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["band"] = 1;
    params["method"] = "dn_to_radiance";
    params["gain"] = 0.5;
    params["bias"] = 1.0;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(2);
    REQUIRE(ds.readBandData(1, out.data(), 2, 1));
    CHECK(out[0] == Catch::Approx(51.0f).epsilon(1e-3f)); // 0.5*100 + 1
    CHECK(out[1] == Catch::Approx(101.0f).epsilon(1e-3f)); // 0.5*200 + 1
}

TEST_CASE("RS spectral index operator schema and metadata", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:spectral_index");
    CHECK(op->group() == "spectral");

    auto schema = op->schema();
    CHECK(schema.isObject());
    CHECK(schema.isMember("properties"));
    CHECK(schema["properties"].isMember("index"));

    auto metadata = op->metadata();
    CHECK(metadata.isObject());
    CHECK(metadata.isMember("tags"));
}

#ifdef SICNU_HAS_OPENCV
// rs:infer tracer bullet (#141): run an identity ONNX model end-to-end through
// the operator chain — raster in → cv::dnn load → forward → raster out — and
// verify the output raster is written with the right dimensions and (for an
// identity model) faithfully preserves the input band. The model lives under
// tests/data so the slice proves the chain against a real, committed ONNX file.
TEST_CASE("rs:infer runs an identity ONNX model end-to-end", "[operators][rs][infer]") {
    auto op = RSOperatorRegistry::instance().create("rs:infer");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:infer");
    CHECK(op->group() == "ml");

    // Resolve the committed identity model relative to this test source file.
    const QString modelPath = QFileInfo(__FILE__).absolutePath() +
                              QStringLiteral("/data/test_infer_identity.onnx");
    REQUIRE(QFile::exists(modelPath));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("input.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));

    // A 4x3 single-band raster with known values; identity must preserve them.
    std::vector<std::vector<float>> bands = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
    };
    REQUIRE(writeTestRaster(inputPath, 4, 3, bands).empty());

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["model"] = modelPath.toStdString();
    params["output"] = outputPath.toStdString();

    RSOperatorContext context;
    const Json::Value result = op->run(params, context);

    REQUIRE(result.isObject());
    CHECK(result["backend"].asString() == "opencv_dnn");
    CHECK(result["width"].asInt() == 4);
    CHECK(result["height"].asInt() == 3);
    CHECK(QFile::exists(outputPath));

    // Read the output back and confirm identity preserved the input band.
    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    REQUIRE(outDs.bandCount() >= 1);
    REQUIRE(outDs.width() == 4);
    REQUIRE(outDs.height() == 3);
    std::vector<float> outPixels(12);
    REQUIRE(outDs.readBandData(1, outPixels.data(), 4, 3));
    for (size_t i = 0; i < outPixels.size(); ++i)
        CHECK(outPixels[i] == Catch::Approx(bands[0][i]).margin(1e-4));
}
#endif

TEST_CASE("RS spectral index NDVI execution", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/ndvi.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> bands(4);
    for (auto &b : bands) b.assign(W * H, 0.0f);

    // NIR = band 4, Red = band 3
    for (size_t i = 0; i < W * H; ++i) {
        bands[3][i] = 100.0f; // NIR
        bands[2][i] = 50.0f;  // Red
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["index"].asString() == "NDVI");
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.width() == W);
    CHECK(ds.height() == H);

    std::vector<float> out(W * H);
    REQUIRE(ds.readBandData(1, out.data(), W, H));
    // NDVI = (100 - 50) / (100 + 50) = 0.333...
    CHECK(out[0] == Catch::Approx(1.0f / 3.0f).epsilon(0.001));
}

TEST_CASE("RS spectral index resolves bands from product roles", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/role_input.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> bands(5);
    for (auto &b : bands) b.assign(W * H, 0.0f);
    for (size_t i = 0; i < W * H; ++i) {
        bands[0][i] = 50.0f;  // Red  (role -> band 1)
        bands[1][i] = 100.0f; // NIR  (role -> band 2)
        bands[2][i] = 100.0f; // NIR  (second NIR role)
        bands[4][i] = 150.0f; // SWIR1 (role -> band 5)
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // Stamp semantic band roles like a stacked product output. Roles point at
    // bands 1/2 for NIR/Red — deliberately NOT the positional defaults 4/3 —
    // so a positional fallback would compute 0/0 instead of the expected 1/3.
    {
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(ds, 1), "SICNU_BAND_ROLE", "red", nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(ds, 2), "SICNU_BAND_ROLE", "nir", nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(ds, 3), "SICNU_BAND_ROLE", "nir", nullptr);
        GDALSetMetadataItem(GDALGetRasterBand(ds, 5), "SICNU_BAND_ROLE", "swir1", nullptr);
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);

    SECTION("NDVI resolves NIR/Red from roles when band params are omitted") {
        const QString outputPath = tmp.path() + "/role_ndvi.tif";
        Json::Value params(Json::objectValue);
        params["input"] = inputPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["index"] = "NDVI";
        // No nir/red params: resolved from the SICNU_BAND_ROLE metadata.

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["output"].asString() == outputPath.toStdString());
        CHECK(QFile::exists(outputPath));

        GdalDatasetWrapper ds;
        REQUIRE(ds.open(outputPath));
        std::vector<float> out(W * H);
        REQUIRE(ds.readBandData(1, out.data(), W, H));
        // NDVI = (band2 NIR 100 - band1 Red 50) / (100 + 50) = 1/3.
        CHECK(out[0] == Catch::Approx(1.0f / 3.0f).epsilon(0.001));
    }

    SECTION("NDBI resolves SWIR1 role and explicit params still win") {
        const QString outputPath = tmp.path() + "/role_ndbi.tif";
        Json::Value params(Json::objectValue);
        params["input"] = inputPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["index"] = "NDBI";

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(QFile::exists(outputPath));

        GdalDatasetWrapper ds;
        REQUIRE(ds.open(outputPath));
        std::vector<float> out(W * H);
        REQUIRE(ds.readBandData(1, out.data(), W, H));
        // NDBI = (SWIR1 150 - NIR 100) / (150 + 100) = 0.2.
        CHECK(out[0] == Catch::Approx(0.2f).epsilon(0.001));
    }
}

TEST_CASE("RS change detection validates the shared pixel grid", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/diff.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> band(1, std::vector<float>(W * H, 50.0f));

    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);

    SECTION("Differing pixel grids are rejected with an actionable error") {
        // Before: 30 m grid; after: 60 m grid, same origin/CRS.
        std::array<double, 6> gt30 = {500000, 30, 0, 4500000, 0, -30};
        std::array<double, 6> gt60 = {500000, 60, 0, 4500000, 0, -60};
        QString err;
        REQUIRE(writeGdalOutput(beforePath, W, H, band, gt30, "EPSG:32648", &err));
        REQUIRE(writeGdalOutput(afterPath, W, H, band, gt60, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["method"] = "difference";

        RSOperatorContext ctx;
        // The message names the pixel grids and the required action, and the
        // run fails before any pixel work (no output file).
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("pixel grids"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("Sub-pixel origin offset is rejected") {
        std::array<double, 6> gtA = {500000, 30, 0, 4500000, 0, -30};
        std::array<double, 6> gtB = {500015, 30, 0, 4500000, 0, -30};
        QString err;
        REQUIRE(writeGdalOutput(beforePath, W, H, band, gtA, "EPSG:32648", &err));
        REQUIRE(writeGdalOutput(afterPath, W, H, band, gtB, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["method"] = "difference";

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("sub-pixel"));
    }

    SECTION("Identical grids still run") {
        std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
        QString err;
        REQUIRE(writeGdalOutput(beforePath, W, H, band, gt, "EPSG:32648", &err));
        REQUIRE(writeGdalOutput(afterPath, W, H, band, gt, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["method"] = "difference";

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["output"].asString() == outputPath.toStdString());
        CHECK(QFile::exists(outputPath));
    }
}

TEST_CASE("RS change detection ratio and CVA methods", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/out.tif";

    constexpr int W = 4;
    constexpr int H = 1;
    // Two bands; band 1: before 100 -> after 200; band 2: before 0 -> after 5.
    std::vector<std::vector<float>> beforeBands(2, std::vector<float>(W * H, 0.0f));
    std::vector<std::vector<float>> afterBands(2, std::vector<float>(W * H, 0.0f));
    for (int i = 0; i < W * H; ++i) {
        beforeBands[0][i] = 100.0f;
        afterBands[0][i] = 200.0f;
        beforeBands[1][i] = 0.0f;
        afterBands[1][i] = 5.0f;
    }
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, beforeBands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, afterBands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);

    SECTION("ratio outputs after/before") {
        const QString out = tmp.path() + "/ratio.tif";
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = out.toStdString();
        params["method"] = "ratio";
        params["band"] = 1;

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["method"].asString() == "ratio");
        CHECK(QFile::exists(out));

        GdalDatasetWrapper ds;
        REQUIRE(ds.open(out));
        std::vector<float> values(W * H);
        REQUIRE(ds.readBandData(1, values.data(), W, H));
        for (float v : values)
            CHECK(v == Catch::Approx(2.0f).epsilon(1e-3f));
    }

    SECTION("cva computes multi-band change magnitude") {
        const QString out = tmp.path() + "/cva.tif";
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = out.toStdString();
        params["method"] = "cva";

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        CHECK(result["method"].asString() == "cva");
        CHECK(QFile::exists(out));

        GdalDatasetWrapper ds;
        REQUIRE(ds.open(out));
        std::vector<float> values(W * H);
        REQUIRE(ds.readBandData(1, values.data(), W, H));
        // magnitude = sqrt((200-100)^2 + (5-0)^2) = sqrt(10025)
        const float expected = std::sqrt(10025.0f);
        for (float v : values)
            CHECK(v == Catch::Approx(expected).epsilon(1e-3f));
    }
}

TEST_CASE("RS change detection mask path: Otsu threshold, cleanup, area", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString maskPath = tmp.path() + "/mask.tif";

    // Bimodal differences: [100, 100, 10, 10] on band 1.
    constexpr int W = 4;
    constexpr int H = 1;
    std::vector<std::vector<float>> beforeBands(1, std::vector<float>(W * H, 0.0f));
    std::vector<std::vector<float>> afterBands(1, std::vector<float>(W * H, 0.0f));
    beforeBands[0][0] = 100.0f; afterBands[0][0] = 200.0f; // diff 100
    beforeBands[0][1] = 100.0f; afterBands[0][1] = 200.0f; // diff 100
    beforeBands[0][2] = 50.0f;  afterBands[0][2] = 60.0f;  // diff 10
    beforeBands[0][3] = 50.0f;  afterBands[0][3] = 60.0f;  // diff 10
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, beforeBands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, afterBands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = maskPath.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "otsu";
    params["cleanup"] = "open";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    // Otsu separates the 100-cluster from the 10-cluster (two-point data:
    // any split between the clusters is optimal, so assert it lands above the
    // low cluster and the mask picks the two high pixels).
    CHECK(result["thresholdUsed"].asDouble() > 10.0);
    CHECK(result["thresholdUsed"].asDouble() < 100.0);
    CHECK(result["changedPixels"].asUInt64() == 2);
    CHECK(result["totalPixels"].asUInt64() == 4);
    CHECK(result["changedPercent"].asDouble() == Catch::Approx(50.0));
    // 2 pixels x (30 m x 30 m) = 1800 m^2.
    CHECK(result["changedArea"].asDouble() == Catch::Approx(1800.0));
    CHECK(QFile::exists(maskPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(maskPath));
    std::vector<float> mask(W * H);
    REQUIRE(ds.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);
    CHECK(mask[1] == 1.0f);
    CHECK(mask[2] == 0.0f);
    CHECK(mask[3] == 0.0f);
}

TEST_CASE("RS band math operator execution", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/bandmath.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(2);
    for (auto &b : bands) b.assign(W * H, 0.0f);
    for (size_t i = 0; i < W * H; ++i) {
        bands[0][i] = 10.0f; // b1
        bands[1][i] = 5.0f;  // b2
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:band_math");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["expression"] = "(b1 - b2) / (b1 + b2)";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(W * H);
    REQUIRE(ds.readBandData(1, out.data(), W, H));
    CHECK(out[0] == Catch::Approx(1.0f / 3.0f).epsilon(0.001));
}

TEST_CASE("RS terrain analysis slope execution", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/dem.tif";
    const QString outputPath = tmp.path() + "/slope.tif";

    constexpr int W = 5;
    constexpr int H = 5;
    std::vector<std::vector<float>> bands(1);
    bands[0].assign(W * H, 0.0f);

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:terrain_analysis");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["product"] = "slope";
    params["cellSize"] = 30.0;
    params["nodata"] = -9999.0;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["product"].asString() == "slope");
    CHECK(QFile::exists(outputPath));
}

TEST_CASE("RS operator invalid parameters raise typed errors", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);

    RSOperatorContext ctx;

    SECTION("Missing input") {
        Json::Value params(Json::objectValue);
        params["output"] = "out.tif";
        params["index"] = "NDVI";
        REQUIRE_THROWS_AS(op->run(params, ctx), RSOperatorError);
    }

    SECTION("Invalid index enum") {
        Json::Value params(Json::objectValue);
        params["input"] = "dummy.tif";
        params["output"] = "out.tif";
        params["index"] = "INVALID";
        try {
            op->run(params, ctx);
            FAIL("Expected RSOperatorError");
        } catch (const RSOperatorError &e) {
            CHECK(e.code() == ErrorCode::InvalidEnumValue);
        }
    }
}

TEST_CASE("RS PCA operator schema and execution", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:pca");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:pca");
    CHECK(op->group() == "enhancement");

    auto schema = op->schema();
    CHECK(schema["properties"].isMember("numComponents"));

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/multi.tif";
    const QString outputPath = tmp.path() + "/pca.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> bands(3);
    for (int b = 0; b < 3; ++b) {
        bands[b].resize(W * H);
        for (size_t i = 0; i < W * H; ++i) {
            bands[b][i] = static_cast<float>(10 * (b + 1) + (i % 5));
        }
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["numComponents"] = 2;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["numComponents"].asInt() == 2);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.bandCount() == 2);
}

TEST_CASE("RS mosaic operator merges two tiles", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:mosaic");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 4;
    constexpr int H = 4;

    // Tile A: origin (0,0), values 1
    const QString pathA = tmp.path() + "/a.tif";
    std::vector<std::vector<float>> bandA(1);
    bandA[0].assign(W * H, 1.0f);
    std::array<double, 6> gtA = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(pathA, W, H, bandA, gtA, "EPSG:4326", &err));

    // Tile B: origin (4,0) — adjacent east, values 2
    const QString pathB = tmp.path() + "/b.tif";
    std::vector<std::vector<float>> bandB(1);
    bandB[0].assign(W * H, 2.0f);
    std::array<double, 6> gtB = {4, 1, 0, 0, 0, -1};
    REQUIRE(writeGdalOutput(pathB, W, H, bandB, gtB, "EPSG:4326", &err));

    const QString outputPath = tmp.path() + "/mosaic.tif";

    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(pathA.toStdString());
    params["inputs"].append(pathB.toStdString());
    params["output"] = outputPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["inputCount"].asInt() == 2);
    CHECK(result["width"].asInt() == 8);
    CHECK(result["height"].asInt() == 4);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.width() == 8);
    CHECK(ds.height() == 4);
}

TEST_CASE("RS mosaic operator rejects empty inputs", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["output"] = "out.tif";

    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::MissingRequiredParameter);
    }
}


#ifdef SICNU_HAS_OPENCV
TEST_CASE("RS kmeans classification runs on multi-band raster", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:kmeans_classification");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:kmeans_classification");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/multi.tif";
    const QString outputPath = tmp.path() + "/km.tif";

    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<std::vector<float>> bands(3);
    for (int b = 0; b < 3; ++b) {
        bands[b].resize(W * H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                // Two spectral clusters: left/right
                bands[b][y * W + x] = (x < W / 2)
                    ? static_cast<float>(10 * (b + 1))
                    : static_cast<float>(100 * (b + 1));
            }
        }
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["k"] = 2;
    params["maxSamples"] = 0;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["k"].asInt() == 2);
    CHECK(result["samplesUsed"].asInt() == W * H);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.width() == W);
    CHECK(ds.height() == H);
    CHECK(ds.bandCount() == 1);

    // ADR 0019 S4 — the adapter routes through RsClassificationPipeline with
    // an RsClassifierKMeans backend. With two spectrally disjoint halves the
    // clustering is seed-independent: left gets one 1-based cluster id, right
    // the other.
    std::vector<float> px(W * H);
    REQUIRE(ds.readBandData(1, px.data(), W, H));
    const float leftLabel = px[0];
    const float rightLabel = px[W * H - 1];
    CHECK(leftLabel >= 1.0f);
    CHECK(leftLabel <= 2.0f);
    CHECK(rightLabel >= 1.0f);
    CHECK(rightLabel <= 2.0f);
    CHECK(leftLabel != rightLabel);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float expect = (x < W / 2) ? leftLabel : rightLabel;
            CHECK(px[y * W + x] == Catch::Approx(expect));
        }
    }

    // Subsample policy unchanged: maxSamples caps the centroid-fit samples.
    const QString outputPath2 = tmp.path() + "/km_sub.tif";
    Json::Value params2 = params;
    params2["output"] = outputPath2.toStdString();
    params2["maxSamples"] = 64;
    RSOperatorContext ctx2;
    Json::Value result2 = op->run(params2, ctx2);
    CHECK(result2["samplesUsed"].asInt() == 64);
    CHECK(QFile::exists(outputPath2));
}
#endif


#ifdef SICNU_HAS_OPENCV
TEST_CASE("RS supervised classification schema registered", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:supervised_classification");
    auto schema = op->schema();
    CHECK(schema["properties"].isMember("training"));
    CHECK(schema["properties"].isMember("method"));
    CHECK(schema["properties"].isMember("classField"));
    // ADR 0019 S3 — parity params
    CHECK(schema["properties"].isMember("scale"));
    CHECK(schema["properties"].isMember("testSplit"));
    CHECK(schema["properties"]["scale"]["default"].asBool() == true);
    CHECK(schema["properties"]["testSplit"]["default"].asDouble() == 0.0);
}

TEST_CASE("RS supervised classification rejects missing training", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = "missing.tif";
    params["output"] = "out.tif";
    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::MissingRequiredParameter);
    }
}

TEST_CASE("RS supervised classification testSplit returns accuracy", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/map.tif";
    makeTwoClassFixture(inputPath, trainingPath);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["classField"] = "class_id";
    params["testSplit"] = 0.3;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["mode"].asString() == "train_predict");
    CHECK(result["classes"].asInt() == 2);
    CHECK(result["trainSamples"].asInt() > 0);
    REQUIRE(result.isMember("overallAccuracy"));
    REQUIRE(result.isMember("kappa"));
    REQUIRE(result.isMember("confusionMatrix"));
    // Spectrally separable classes: held-out accuracy should be perfect.
    CHECK(result["overallAccuracy"].asDouble() == Catch::Approx(1.0));
    CHECK(result["kappa"].asDouble() == Catch::Approx(1.0));
    CHECK(result["confusionMatrix"].size() == 2);
    CHECK(result["confusionMatrix"][0].size() == 2);
    CHECK(QFile::exists(outputPath));

    // Professional outputs: per-class validation metrics + sample counts.
    REQUIRE(result.isMember("perClassMetrics"));
    REQUIRE(result["perClassMetrics"].size() == 2);
    CHECK(result["perClassMetrics"][0]["f1"].asDouble() == Catch::Approx(1.0));
    REQUIRE(result.isMember("trainSamplesByClass"));
    REQUIRE(result["trainSamplesByClass"].size() == 2);
    CHECK(result["trainSamplesByClass"][0]["classId"].asInt() != result["trainSamplesByClass"][1]["classId"].asInt());
    // Balanced fixture: no imbalance warnings.
    CHECK_FALSE(result.isMember("imbalanceWarnings"));
}

TEST_CASE("RS supervised classification reports class-imbalance warnings", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/imbalanced.gpkg";
    const QString outputPath = tmp.path() + "/map.tif";

    // Imbalanced but separable raster: the bottom row is dark (class 1), the
    // other 15 rows are bright (class 2) -> 16 vs 240 training samples.
    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<std::vector<float>> bands(2, std::vector<float>(W * H, 100.0f));
    for (int x = 0; x < W; ++x)
    {
        bands[0][(H - 1) * W + x] = 10.0f;
        bands[1][(H - 1) * W + x] = 10.0f;
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // Class 1 covers one raster row (16 px); class 2 covers the other 15 rows
    // (240 px) -> a ~6.7% ratio that must trigger an imbalance warning.
    writeTestVector(trainingPath, QStringLiteral("class_id"), 1, 0, -16, 16, -15);
    writeTestVector(trainingPath, QStringLiteral("class_id"), 2, 0, -15, 16, 0, true);

    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["classField"] = "class_id";
    params["testSplit"] = 0.3;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(result.isMember("trainSamplesByClass"));
    REQUIRE(result.isMember("imbalanceWarnings"));
    REQUIRE(result["imbalanceWarnings"].size() >= 1);
    CHECK(result["imbalanceWarnings"][0].asString().find("Class 1") != std::string::npos);
    CHECK(QFile::exists(outputPath));
}

TEST_CASE("RS supervised classification rejects out-of-range testSplit", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["input"] = "missing.tif";
    params["output"] = "out.tif";
    params["training"] = "missing.gpkg";
    params["testSplit"] = 0.95;
    try {
        op->run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::OutOfRange);
    }
}

TEST_CASE("RS supervised classification scale model round-trip", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString trainOut = tmp.path() + "/train_map.tif";
    const QString predictOut = tmp.path() + "/predict_map.tif";
    const QString modelPath = tmp.path() + "/model.xml";
    makeTwoClassFixture(inputPath, trainingPath);

    // Train with scaling + modelOut.
    Json::Value trainParams(Json::objectValue);
    trainParams["input"] = inputPath.toStdString();
    trainParams["output"] = trainOut.toStdString();
    trainParams["training"] = trainingPath.toStdString();
    trainParams["classField"] = "class_id";
    trainParams["scale"] = true;
    trainParams["modelOut"] = modelPath.toStdString();

    RSOperatorContext ctx;
    Json::Value trainResult = op->run(trainParams, ctx);
    CHECK(trainResult["modelOut"].asString() == modelPath.toStdString());
    REQUIRE(QFile::exists(modelPath));

    // Superset sidecar next to the model carries the fitted scaler.
    const QString sidecarPath = tmp.path() + "/model.meta.json";
    REQUIRE(QFile::exists(sidecarPath));
    QFile sidecar(sidecarPath);
    REQUIRE(sidecar.open(QIODevice::ReadOnly));
    CHECK(sidecar.readAll().contains("\"scaler\""));

    // Predict-only via modelIn: sidecar scaler applied automatically.
    Json::Value predictParams(Json::objectValue);
    predictParams["input"] = inputPath.toStdString();
    predictParams["output"] = predictOut.toStdString();
    predictParams["modelIn"] = modelPath.toStdString();

    RSOperatorContext ctx2;
    Json::Value predictResult = op->run(predictParams, ctx2);
    CHECK(predictResult["mode"].asString() == "predict_only");
    CHECK(predictResult["method"].asString() == "svm");
    REQUIRE(QFile::exists(predictOut));

    // Same class map from both runs.
    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<float> a(W * H), b(W * H);
    GdalDatasetWrapper dsA, dsB;
    REQUIRE(dsA.open(trainOut));
    REQUIRE(dsB.open(predictOut));
    REQUIRE(dsA.readBandData(1, a.data(), W, H));
    REQUIRE(dsB.readBandData(1, b.data(), W, H));
    CHECK(a == b);
    // Sanity: both classes present, left = 1, right = 2.
    CHECK(a[0] == Catch::Approx(1.0f));
    CHECK(a[W * H - 1] == Catch::Approx(2.0f));
}

TEST_CASE("RS supervised classification writes a probability output (normal_bayes)", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/map.tif";
    const QString probPath = tmp.path() + "/confidence.tif";
    makeTwoClassFixture(inputPath, trainingPath);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["classField"] = "class_id";
    params["method"] = "normal_bayes";
    params["probabilityOutput"] = probPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(QFile::exists(outputPath));
    REQUIRE(QFile::exists(probPath));
    CHECK(result["meanConfidence"].asDouble() > 0.5);
    CHECK(result["meanConfidence"].asDouble() <= 1.0);

    // The probability raster is Float32 with NoData -1; separable classes
    // produce near-1 posteriors on the class map's valid pixels.
    GdalDatasetWrapper prob;
    REQUIRE(prob.open(probPath));
    CHECK(prob.bandDataType(1) == GDT_Float32);
    std::vector<float> px(16 * 16);
    REQUIRE(prob.readBandData(1, px.data(), 16, 16));
    bool hasNoData = false;
    prob.bandNoDataValue(1, &hasNoData);
    CHECK(hasNoData);
    CHECK(px[0] > 0.9f);
    CHECK(px[16 * 16 - 1] > 0.9f);
}

TEST_CASE("RS supervised classification rejects probability output for SVM", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/map.tif";
    makeTwoClassFixture(inputPath, trainingPath);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["method"] = "svm"; // default
    params["probabilityOutput"] = (tmp.path() + "/confidence.tif").toStdString();

    RSOperatorContext ctx;
    REQUIRE_THROWS_WITH(op->run(params, ctx),
                        Catch::Matchers::ContainsSubstring("normal_bayes"));
    CHECK_FALSE(QFile::exists(tmp.path() + "/confidence.tif"));
}

TEST_CASE("RS supervised classification escalates dtype for large class ids", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:supervised_classification");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/map16.tif";
    makeTwoClassFixture(inputPath, trainingPath);
    // Re-tag the second class with an id > 255: old operator clamped to 255;
    // the pipeline escalates the output dtype instead.
    {
        GDALDatasetH ds = GDALOpenEx(trainingPath.toUtf8().constData(),
                                     GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr, nullptr, nullptr);
        REQUIRE(ds != nullptr);
        OGRLayerH lyr = GDALDatasetGetLayer(ds, 0);
        REQUIRE(lyr != nullptr);
        OGRFeatureH feat;
        while ((feat = OGR_L_GetNextFeature(lyr)) != nullptr) {
            if (OGR_F_GetFieldAsInteger(feat, 0) == 2)
                OGR_F_SetFieldInteger(feat, 0, 300);
            REQUIRE(OGR_L_SetFeature(lyr, feat) == OGRERR_NONE);
            OGR_F_Destroy(feat);
        }
        GDALClose(ds);
    }

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["classField"] = "class_id";

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["classes"].asInt() == 2);
    REQUIRE(QFile::exists(outputPath));

    // Output band is UInt16 (escalated, not clamped) and class 300 survives.
    GDALDatasetH out = GDALOpenEx(outputPath.toUtf8().constData(), GDAL_OF_RASTER,
                                  nullptr, nullptr, nullptr);
    REQUIRE(out != nullptr);
    CHECK(GDALGetRasterDataType(GDALGetRasterBand(out, 1)) == GDT_UInt16);
    std::vector<float> px(16 * 16);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(out, 1), GF_Read, 0, 0, 16, 16,
                         px.data(), 16, 16, GDT_Float32, 0, 0) == CE_None);
    GDALClose(out);
    CHECK(px.front() == Catch::Approx(1.0f));
    CHECK(px.back() == Catch::Approx(300.0f));
}
#endif


#ifdef SICNU_HAS_OPENCV
TEST_CASE("RS obia_segment produces labels", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_segment");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/in.tif";
    const QString outputPath = tmp.path() + "/seg.tif";

    constexpr int W = 32;
    constexpr int H = 32;
    std::vector<std::vector<float>> bands(1);
    bands[0].resize(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            bands[0][y * W + x] = (x < W / 2) ? 10.0f : 200.0f;
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["minRegionSize"] = 10;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["segments"].asInt() >= 1);
    CHECK(QFile::exists(outputPath));
}
#endif


#ifdef SICNU_HAS_OPENCV
TEST_CASE("RS obia_hierarchy schema and OTB-gated execution", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_hierarchy");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:obia_hierarchy");
    auto schema = op->schema();
    CHECK(schema["properties"].isMember("outputFine"));
    CHECK(schema["properties"].isMember("training"));

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/in.tif";
    const QString outputPath = tmp.path() + "/fine.tif";

    // The run() gates on OTB after the input-exists check, so the fail-closed
    // path still needs a real (any) raster.
    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<std::vector<float>> bands(1);
    bands[0].assign(static_cast<size_t>(W) * H, 50.0f);
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["outputFine"] = outputPath.toStdString();

    RSOperatorContext ctx;
    if (!RsOtbSegmenter::isAvailable()) {
        // No OTB: the operator must fail closed with a clear OtbError before
        // touching the raster (its primary segmenters require OTB).
        try {
            op->run(params, ctx);
            FAIL("Expected OtbError when OTB is unavailable");
        } catch (const RSOperatorError& e) {
            CHECK(e.code() == ErrorCode::OtbError);
        }
        return;
    }

    // OTB present: smoke the real two-level path (segment-only) on the
    // two-class fixture; the training/classify stage stays untested here.
    const QString trainingPath = tmp.path() + "/train.gpkg";
    makeTwoClassFixture(inputPath, trainingPath);
    Json::Value result = op->run(params, ctx);
    CHECK(result.isMember("fineSegments"));
    CHECK(result.isMember("coarseSegments"));
    CHECK(QFile::exists(outputPath));
}
#endif

#ifdef SICNU_HAS_OPENCV
TEST_CASE("RS obia_classify schema", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_classify");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:obia_classify");
    auto schema = op->schema();
    CHECK(schema["properties"].isMember("training"));
    CHECK(schema["properties"].isMember("minLabelPixels"));
}

TEST_CASE("RS obia_classify runs end-to-end on grid segments", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_classify");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/obia_map.tif";
    makeTwoClassFixture(inputPath, trainingPath);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["classField"] = "class_id";
    params["segmentMethod"] = "grid";
    params["cellSize"] = 8;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["segments"].asInt() == 4); // 16x16 raster, 8px cells
    CHECK(result["labeledSegments"].asInt() == 4);
    CHECK(result["classes"].asInt() == 2);
    REQUIRE(QFile::exists(outputPath));

    // Segments align with the spectral halves: left = class 1, right = 2.
    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<float> px(W * H);
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    REQUIRE(ds.readBandData(1, px.data(), W, H));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float expect = (x < W / 2) ? 1.0f : 2.0f;
            CHECK(px[y * W + x] == Catch::Approx(expect));
        }
    }
}

TEST_CASE("RS obia_classify escalates dtype for large class ids", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_classify");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString trainingPath = tmp.path() + "/training.gpkg";
    const QString outputPath = tmp.path() + "/obia_map16.tif";
    makeTwoClassFixture(inputPath, trainingPath);
    // Re-tag the second class with an id > 255: the pre-ADR-0019 operator
    // silently clamped to 255; the pipeline dtype policy escalates instead.
    {
        GDALDatasetH ds = GDALOpenEx(trainingPath.toUtf8().constData(),
                                     GDAL_OF_VECTOR | GDAL_OF_UPDATE, nullptr, nullptr, nullptr);
        REQUIRE(ds != nullptr);
        OGRLayerH lyr = GDALDatasetGetLayer(ds, 0);
        REQUIRE(lyr != nullptr);
        OGRFeatureH feat;
        while ((feat = OGR_L_GetNextFeature(lyr)) != nullptr) {
            if (OGR_F_GetFieldAsInteger(feat, 0) == 2)
                OGR_F_SetFieldInteger(feat, 0, 300);
            REQUIRE(OGR_L_SetFeature(lyr, feat) == OGRERR_NONE);
            OGR_F_Destroy(feat);
        }
        GDALClose(ds);
    }

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["training"] = trainingPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["classField"] = "class_id";
    params["segmentMethod"] = "grid";
    params["cellSize"] = 8;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["classes"].asInt() == 2);
    REQUIRE(QFile::exists(outputPath));

    GDALDatasetH out = GDALOpenEx(outputPath.toUtf8().constData(), GDAL_OF_RASTER,
                                  nullptr, nullptr, nullptr);
    REQUIRE(out != nullptr);
    CHECK(GDALGetRasterDataType(GDALGetRasterBand(out, 1)) == GDT_UInt16);
    std::vector<float> px(16 * 16);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(out, 1), GF_Read, 0, 0, 16, 16,
                         px.data(), 16, 16, GDT_Float32, 0, 0) == CE_None);
    GDALClose(out);
    CHECK(px.front() == Catch::Approx(1.0f));
    CHECK(px.back() == Catch::Approx(300.0f));
}
#endif

TEST_CASE("RS apply_mask sets masked pixels to NoData (same grid)", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/product.tif";
    const QString maskPath = tmp.path() + "/mask.tif";
    const QString outputPath = tmp.path() + "/masked.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(2, std::vector<float>(W * H, 100.0f));
    // Mask: a 2x2 cloud block in the top-left corner.
    std::vector<std::vector<float>> mask(1, std::vector<float>(W * H, 0.0f));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            mask[0][y * W + x] = 1.0f;

    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(maskPath, W, H, mask, gt, "EPSG:32648", &err));

    // Mark band 1 as NIR with a wavelength so the test pins metadata pass-through.
    GDALDatasetH inDs = GDALOpenEx(inputPath.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_UPDATE,
                                   nullptr, nullptr, nullptr);
    REQUIRE(inDs != nullptr);
    GDALSetMetadataItem(GDALGetRasterBand(inDs, 1), "SICNU_BAND_ROLE", "nir", nullptr);
    GDALSetMetadataItem(GDALGetRasterBand(inDs, 1), "WAVELENGTH", "833", nullptr);
    GDALClose(inDs);

    auto op = RSOperatorRegistry::instance().create("rs:apply_mask");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["mask"] = maskPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["no_data"] = -9999.0;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(QFile::exists(outputPath));

    CHECK(result["maskedPixels"].asInt() == 4);
    CHECK(result["totalPixels"].asInt() == 16);
    CHECK(result["maskedPercent"].asDouble() == Catch::Approx(25.0));
    CHECK(result["aligned"].asBool() == false);

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.bandCount() == 2);
    std::vector<float> b1(W * H), b2(W * H);
    REQUIRE(out.readBandData(1, b1.data(), W, H));
    REQUIRE(out.readBandData(2, b2.data(), W, H));
    for (int i = 0; i < W * H; ++i) {
        const bool inCloud = (i / W) < 2 && (i % W) < 2;
        CHECK(b1[i] == Catch::Approx(inCloud ? -9999.0f : 100.0f));
        CHECK(b2[i] == Catch::Approx(inCloud ? -9999.0f : 100.0f));
    }
    // Band semantics pass through to the masked product.
    CHECK(out.bandMetadataItem(1, "SICNU_BAND_ROLE") == QStringLiteral("nir"));
    CHECK(out.bandMetadataItem(1, "WAVELENGTH") == QStringLiteral("833"));
    bool hasNoData = false;
    CHECK(out.bandNoDataValue(1, &hasNoData) == Catch::Approx(-9999.0));
    CHECK(hasNoData);
}

TEST_CASE("RS apply_mask auto-aligns a coarser mask onto the input grid", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/product.tif";
    const QString maskPath = tmp.path() + "/mask60.tif";
    const QString outputPath = tmp.path() + "/masked.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(1, std::vector<float>(W * H, 200.0f));

    // Input: 30 m grid; mask: 60 m grid covering the same footprint (2x2).
    std::array<double, 6> gt30 = {500000, 30, 0, 4500000, 0, -30};
    std::array<double, 6> gt60 = {500000, 60, 0, 4500000, 0, -60};
    std::vector<std::vector<float>> mask(1, std::vector<float>(2 * 2, 0.0f));
    mask[0][0] = 1.0f; // one 60 m cell -> four 30 m pixels

    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt30, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(maskPath, 2, 2, mask, gt60, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:apply_mask");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["mask"] = maskPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["no_data"] = -1.0f;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(QFile::exists(outputPath));
    CHECK(result["maskedPixels"].asInt() == 4);
    CHECK(result["aligned"].asBool() == true);

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> px(W * H);
    REQUIRE(out.readBandData(1, px.data(), W, H));
    // The masked 60 m cell covers input pixels (0,0),(1,0),(0,1),(1,1).
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            CHECK(px[y * W + x] == Catch::Approx((x < 2 && y < 2) ? -1.0f : 200.0f));
}

TEST_CASE("RS apply_mask rejects incompatible grids and missing NoData", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(1, std::vector<float>(W * H, 100.0f));
    std::vector<std::vector<float>> mask(1, std::vector<float>(W * H, 0.0f));

    auto op = RSOperatorRegistry::instance().create("rs:apply_mask");
    REQUIRE(op != nullptr);

    SECTION("CRS mismatch is never auto-corrected") {
        const QString inputPath = tmp.path() + "/in4326.tif";
        const QString maskPath = tmp.path() + "/mask32648.tif";
        const QString outputPath = tmp.path() + "/out.tif";
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        QString err;
        REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:4326", &err));
        REQUIRE(writeGdalOutput(maskPath, W, H, mask, gt, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["input"] = inputPath.toStdString();
        params["mask"] = maskPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["no_data"] = -9999.0;
        params["align_mask"] = true;

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("coordinate reference systems"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("align_mask=false rejects a pixel-size mismatch") {
        const QString inputPath = tmp.path() + "/in30.tif";
        const QString maskPath = tmp.path() + "/mask60.tif";
        const QString outputPath = tmp.path() + "/out.tif";
        std::array<double, 6> gt30 = {500000, 30, 0, 4500000, 0, -30};
        std::array<double, 6> gt60 = {500000, 60, 0, 4500000, 0, -60};
        QString err;
        REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt30, "EPSG:32648", &err));
        REQUIRE(writeGdalOutput(maskPath, 2, 2, mask, gt60, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["input"] = inputPath.toStdString();
        params["mask"] = maskPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["no_data"] = -9999.0;
        params["align_mask"] = false;

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("pixel grids"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("bands without NoData require the no_data parameter") {
        const QString inputPath = tmp.path() + "/in.tif";
        const QString maskPath = tmp.path() + "/mask.tif";
        const QString outputPath = tmp.path() + "/out.tif";
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        QString err;
        REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:4326", &err));
        REQUIRE(writeGdalOutput(maskPath, W, H, mask, gt, "EPSG:4326", &err));

        Json::Value params(Json::objectValue);
        params["input"] = inputPath.toStdString();
        params["mask"] = maskPath.toStdString();
        params["output"] = outputPath.toStdString();

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("NoData"));
        CHECK_FALSE(QFile::exists(outputPath));
    }
}

TEST_CASE("RS post-classification change builds the transition matrix", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before_class.tif";
    const QString afterPath = tmp.path() + "/after_class.tif";
    const QString outputPath = tmp.path() + "/change_map.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    // before:      after:
    //  0 0 1 1      0 0 1 1
    //  0 0 1 1      0 0 2 2     (1 -> 2 change in the bottom-right 2x2 of the
    //  2 2 2 2      2 2 2 2      top band; everything else stable)
    //  2 2 2 2      2 2 2 2
    const std::vector<int32_t> beforeClasses = {
        0, 0, 1, 1,  0, 0, 1, 1,  2, 2, 2, 2,  2, 2, 2, 2};
    const std::vector<int32_t> afterClasses = {
        0, 0, 1, 1,  0, 0, 2, 2,  2, 2, 2, 2,  2, 2, 2, 2};
    std::vector<std::vector<float>> beforeBands(
        1, std::vector<float>(W * H, 0.0f));
    std::vector<std::vector<float>> afterBands(
        1, std::vector<float>(W * H, 0.0f));
    for (int i = 0; i < W * H; ++i) {
        beforeBands[0][i] = static_cast<float>(beforeClasses[i]);
        afterBands[0][i] = static_cast<float>(afterClasses[i]);
    }
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, beforeBands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, afterBands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:post_classification_change");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outputPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(QFile::exists(outputPath));

    // Auto class_count = max observed (2) + 1 = 3.
    CHECK(result["classCount"].asInt() == 3);
    REQUIRE(result["transitionMatrix"].size() == 3);
    REQUIRE(result["transitionMatrix"][0].size() == 3);
    CHECK(result["transitionMatrix"][0][0].asUInt64() == 4); // 0 -> 0
    CHECK(result["transitionMatrix"][1][1].asUInt64() == 2); // 1 -> 1
    CHECK(result["transitionMatrix"][1][2].asUInt64() == 2); // 1 -> 2 (changed)
    CHECK(result["transitionMatrix"][2][2].asUInt64() == 8); // 2 -> 2
    CHECK(result["changedPixels"].asUInt64() == 2);
    CHECK(result["unchangedPixels"].asUInt64() == 14);
    CHECK(result["totalPixels"].asUInt64() == 16);
    CHECK(result["changedPercent"].asDouble() == Catch::Approx(12.5));

    // Change map encodes before * classCount + after; the changed pixel at
    // (row 1, col 3) is 1 * 3 + 2 = 5; the stable 2->2 pixels are 2*3+2 = 8.
    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    CHECK(out.bandDataType(1) == GDT_UInt16);
    std::vector<float> px(W * H);
    REQUIRE(out.readBandData(1, px.data(), W, H));
    CHECK(px[1 * W + 3] == Catch::Approx(5.0f));
    CHECK(px[2 * W + 0] == Catch::Approx(8.0f));
    bool hasNoData = false;
    out.bandNoDataValue(1, &hasNoData);
    CHECK(hasNoData);
}

TEST_CASE("RS post-classification change validates inputs", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before_class.tif";
    const QString afterPath = tmp.path() + "/after_class.tif";
    const QString outputPath = tmp.path() + "/change_map.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> beforeBands(1, std::vector<float>(W * H, 1.0f));
    std::vector<std::vector<float>> afterBands(1, std::vector<float>(W * H, 2.0f));
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, beforeBands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, afterBands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:post_classification_change");
    REQUIRE(op != nullptr);

    SECTION("class_count too small for the observed classes is rejected") {
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();
        params["class_count"] = 2; // observed class 2 needs class_count >= 3

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("class_count"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("differing pixel grids are rejected with an actionable error") {
        const QString before30 = tmp.path() + "/before30.tif";
        std::array<double, 6> gt30 = {500000, 30, 0, 4500000, 0, -30};
        std::array<double, 6> gt60 = {500000, 60, 0, 4500000, 0, -60};
        REQUIRE(writeGdalOutput(before30, W, H, beforeBands, gt30, "EPSG:32648", &err));
        REQUIRE(writeGdalOutput(afterPath, 2, 2, afterBands, gt60, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["before"] = before30.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("pixel grids"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("auto class_count over the UInt16 change-code limit is rejected") {
        // A stray large band value (e.g. undeclared NoData) must not drive an
        // unbounded matrix allocation or corrupt UInt16 codes (ADR 0105).
        std::vector<std::vector<float>> big(1, std::vector<float>(W * H, 300.0f));
        const QString bigPath = tmp.path() + "/big.tif";
        REQUIRE(writeGdalOutput(bigPath, W, H, big, gt, "EPSG:32648", &err));

        Json::Value params(Json::objectValue);
        params["before"] = bigPath.toStdString();
        params["after"] = bigPath.toStdString();
        params["output"] = outputPath.toStdString();

        RSOperatorContext ctx;
        REQUIRE_THROWS_WITH(op->run(params, ctx),
                            Catch::Matchers::ContainsSubstring("UInt16 change-code limit"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("NoData pixels are excluded from the matrix and the change map") {
        // before: left half 1.0 (declared NoData), right half 0.0 (valid);
        // after: 2.0 everywhere. Only the right half counts (0 -> 2).
        std::vector<std::vector<float>> mixedBands(
            1, std::vector<float>(W * H, 0.0f));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W / 2; ++x)
                mixedBands[0][y * W + x] = 1.0f;
        REQUIRE(writeGdalOutput(beforePath, W, H, mixedBands, gt, "EPSG:32648", &err));

        GDALDatasetH inDs = GDALOpenEx(beforePath.toUtf8().constData(),
                                       GDAL_OF_RASTER | GDAL_OF_UPDATE,
                                       nullptr, nullptr, nullptr);
        REQUIRE(inDs != nullptr);
        GDALSetRasterNoDataValue(GDALGetRasterBand(inDs, 1), 1.0);
        GDALClose(inDs);

        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = outputPath.toStdString();

        RSOperatorContext ctx;
        Json::Value result = op->run(params, ctx);
        REQUIRE(QFile::exists(outputPath));
        CHECK(result["totalPixels"].asUInt64() == 8);
        CHECK(result["transitionMatrix"][0][2].asUInt64() == 8);

        GdalDatasetWrapper out;
        REQUIRE(out.open(outputPath));
        std::vector<float> px(W * H);
        REQUIRE(out.readBandData(1, px.data(), W, H));
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float expected = x < W / 2 ? 65535.0f : 2.0f; // NoData / code 0*3+2
                CHECK(px[y * W + x] == Catch::Approx(expected));
            }
        }
    }
}

TEST_CASE("Radiometric state metadata round-trips and survives missing files",
          "[operators][rs][provenance]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.path() + "/state.tif";

    std::vector<std::vector<float>> band(1, std::vector<float>(4, 10.0f));
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(path, 2, 2, band, gt, "EPSG:32648", &err));

    // Absent before writing.
    CHECK(SatelliteProducts::readRadiometricState(path).isEmpty());

    REQUIRE(SatelliteProducts::setRadiometricState(
        path, SatelliteProducts::kRadiometricStateToaReflectance));
    CHECK(SatelliteProducts::readRadiometricState(path)
          == QString::fromUtf8(SatelliteProducts::kRadiometricStateToaReflectance));

    // Overwrite with a different state.
    REQUIRE(SatelliteProducts::setRadiometricState(
        path, SatelliteProducts::kRadiometricStateSurfaceReflectance));
    CHECK(SatelliteProducts::readRadiometricState(path)
          == QString::fromUtf8(SatelliteProducts::kRadiometricStateSurfaceReflectance));

    // Missing file: read is empty, write fails with a message.
    CHECK(SatelliteProducts::readRadiometricState(path + QStringLiteral( ".nope" )).isEmpty());
    QString writeError;
    CHECK_FALSE(SatelliteProducts::setRadiometricState(
        path + QStringLiteral( ".nope" ), SatelliteProducts::kRadiometricStateRadiance,
        &writeError));
    CHECK_FALSE(writeError.isEmpty());
}

TEST_CASE("RS change detection enforces comparable radiometric states",
          "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/diff.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> band(1, std::vector<float>(W * H, 50.0f));
    std::array<double, 6> gt = {500000, 10, 0, 4500000, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, band, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, band, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);

    auto run = [&](const QString &before, const QString &after) {
        Json::Value params(Json::objectValue);
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = outputPath.toStdString();
        params["method"] = "difference";
        RSOperatorContext ctx;
        return op->run(params, ctx);
    };

    SECTION("Absent states pass (unknown radiometric state is unchecked)") {
        REQUIRE_NOTHROW(run(beforePath, afterPath));
        CHECK(QFile::exists(outputPath));
        QFile::remove(outputPath);
    }

    SECTION("Identical states pass") {
        REQUIRE(SatelliteProducts::setRadiometricState(
            beforePath, SatelliteProducts::kRadiometricStateToaReflectance));
        REQUIRE(SatelliteProducts::setRadiometricState(
            afterPath, SatelliteProducts::kRadiometricStateToaReflectance));
        REQUIRE_NOTHROW(run(beforePath, afterPath));
        CHECK(QFile::exists(outputPath));
    }

    SECTION("Differing states fail with an actionable error") {
        REQUIRE(SatelliteProducts::setRadiometricState(
            beforePath, SatelliteProducts::kRadiometricStateToaReflectance));
        REQUIRE(SatelliteProducts::setRadiometricState(
            afterPath, SatelliteProducts::kRadiometricStateRadiance));
        REQUIRE_THROWS_WITH(
            run(beforePath, afterPath),
            Catch::Matchers::ContainsSubstring("radiometric states"));
        CHECK_FALSE(QFile::exists(outputPath));
    }

    SECTION("One-sided declaration passes (other side unknown)") {
        REQUIRE(SatelliteProducts::setRadiometricState(
            beforePath, SatelliteProducts::kRadiometricStateToaReflectance));
        REQUIRE_NOTHROW(run(beforePath, afterPath));
        CHECK(QFile::exists(outputPath));
    }
}

TEST_CASE("RS change detection statistical threshold uses mean + k*stddev", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/diff.tif";

    // 8x8: baseline difference 1 everywhere except four hot pixels (diff 40).
    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<float> before(W * H, 100.0f);
    std::vector<float> after(W * H, 101.0f);
    for (int i = 0; i < 4; ++i)
        after[i] = 140.0f; // |diff| = 40
    std::array<double, 6> gt = {500000, 10, 0, 4500000, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, {before}, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, {after}, gt, "EPSG:32648", &err));

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "statistical";
    params["statisticalK"] = 2.0;

    RSOperatorContext ctx;
    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);
    const Json::Value result = op->run(params, ctx);

    // mean = (60*1 + 4*40)/64 ~= 3.44, stddev ~= 9.4 => threshold ~= 22.
    const double thresholdUsed = result["thresholdUsed"].asDouble();
    CHECK(thresholdUsed > 10.0);
    CHECK(thresholdUsed < 30.0);
    // The four hot pixels are the only changes at that threshold.
    CHECK(result["changedPixels"].asUInt64() == 4);
}

TEST_CASE("RS change detection minimum mapping unit drops small components", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/diff.tif";

    // 8x8: a 2x2 changed block (top-left) and one isolated hot pixel
    // (bottom-right), everything else unchanged.
    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<float> before(W * H, 100.0f);
    std::vector<float> after = before;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            after[y * W + x] = 140.0f;
    after[(H - 1) * W + (W - 1)] = 150.0f; // isolated dot
    std::array<double, 6> gt = {500000, 10, 0, 4500000, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, {before}, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, {after}, gt, "EPSG:32648", &err));

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "manual";
    params["threshold"] = 30.0;
    params["minAreaPixels"] = 4; // 2x2 block (4 px) survives, dot (1 px) dropped

    RSOperatorContext ctx;
    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);
    const Json::Value result = op->run(params, ctx);

    // Only the 2x2 block remains changed.
    CHECK(result["changedPixels"].asUInt64() == 4);

    // The mask metadata records the MMU.
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> mask(static_cast<size_t>(W) * H, 0.0f);
    REQUIRE(ds.readBandData(1, mask.data(), W, H));
    CHECK(mask[0] == 1.0f);              // block pixel (0,0)
    CHECK(mask[9] == 1.0f);              // block pixel (1,1)
    CHECK(mask[(H - 1) * W + (W - 1)] == 0.0f); // dot removed
}
