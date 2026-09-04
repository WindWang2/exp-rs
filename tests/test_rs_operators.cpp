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
#include "operators/gdal/gdal_operator_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/algorithms/math_utils.h"
#include "raster_bit_compare.h"

#include <limits>

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
    CHECK(registry.hasOperator("rs:ndvi"));
    CHECK(registry.hasOperator("rs:evi"));
    CHECK(registry.hasOperator("rs:ndwi"));
    CHECK(registry.hasOperator("rs:savi"));
    CHECK(registry.hasOperator("rs:ndbi"));
    CHECK(registry.hasOperator("rs:mndwi"));
    CHECK(registry.hasOperator("rs:band_math"));
    CHECK(registry.hasOperator("rs:atmospheric_correction"));
    CHECK(registry.hasOperator("rs:dn_to_radiance"));
    CHECK(registry.hasOperator("rs:atmospheric_dos1"));
    CHECK(registry.hasOperator("rs:atmospheric_dos2"));
    CHECK(registry.hasOperator("rs:atmospheric_quac"));
    CHECK(registry.hasOperator("rs:radiometric_calibration"));
    CHECK(registry.hasOperator("rs:change_detection"));
    CHECK(registry.hasOperator("rs:post_classification_change"));
    CHECK(registry.hasOperator("rs:qa_mask"));
    CHECK(registry.hasOperator("rs:apply_mask"));
    CHECK(registry.hasOperator("rs:image_fusion"));
    CHECK(registry.hasOperator("rs:terrain_analysis"));
    CHECK(registry.hasOperator("rs:sar_calibrate"));
    CHECK(registry.hasOperator("rs:sar_backscatter"));
    CHECK(registry.hasOperator("rs:sar_terrain_flatten"));
    CHECK(registry.hasOperator("rs:sar_terrain_correction"));
    CHECK(registry.hasOperator("rs:sar_speckle"));
    CHECK(registry.hasOperator("rs:sar_ratio"));
    CHECK(registry.hasOperator("rs:sar_texture"));
    CHECK(registry.hasOperator("rs:sar_change"));
    CHECK(registry.hasOperator("rs:temporal_smooth"));
    CHECK(registry.hasOperator("rs:temporal_gap_fill"));
    CHECK(registry.hasOperator("rs:temporal_harmonic_fit"));
    CHECK(registry.hasOperator("rs:temporal_phenology"));
    CHECK(registry.hasOperator("rs:temporal_breakpoints"));
    CHECK(registry.hasOperator("rs:temporal_decompose"));
    CHECK(registry.hasOperator("rs:feature_stack"));
    CHECK(registry.hasOperator("rs:feature_normalize"));
    CHECK(registry.hasOperator("rs:feature_select"));
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

TEST_CASE("Streaming spectral index output is bit-exact against the full-raster kernel",
          "[operators][rs][spectral][streaming]") {
    // ADR 0124 anchor: the streaming implementation (#664) decomposes the
    // raster into row blocks; because the NDVI kernel is strictly
    // element-wise, the streamed result must be BIT-identical to one
    // full-raster kernel invocation over the same bands. >256 rows forces
    // multiple row blocks through the streaming path.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/in.tif";
    const QString outputPath = tmp.path() + "/out.tif";
    const QString expectedPath = tmp.path() + "/expected.tif";

    constexpr int W = 5;
    constexpr int H = 600;
    std::vector<std::vector<float>> bands(2);
    bands[0].resize(static_cast<size_t>(W) * H);
    bands[1].resize(static_cast<size_t>(W) * H);
    for (int i = 0; i < W * H; ++i) {
        bands[0][i] = 10.0f + static_cast<float>(i % 23);
        bands[1][i] = 40.0f + static_cast<float>((i * 7) % 31);
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 2;
    params["red"] = 1;
    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(result["output"].asString() == outputPath.toStdString());

    // Serial anchor: a single full-raster kernel invocation.
    std::vector<float> expected(static_cast<size_t>(W) * H);
    REQUIRE(MathUtils::normalizedDifference(
        bands[1].data(), bands[0].data(), expected.data(), expected.size()));

    // Persist the anchor with the same grid and NaN nodata the operator emits.
    GdalDatasetWrapper in;
    REQUIRE(in.open(inputPath));
    QString err;
    GDALDatasetH expDs = createOutputTiff(expectedPath, W, H, 1,
                                          static_cast<int>(GDT_Float32),
                                          in.geoTransform(), in.projection(), &err);
    REQUIRE(expDs != nullptr);
    REQUIRE(GDALRasterIO(GDALGetRasterBand(expDs, 1), GF_Write, 0, 0, W, H,
                         expected.data(), W, H, GDT_Float32, 0, 0) == CE_None);
    GDALSetRasterNoDataValue(GDALGetRasterBand(expDs, 1),
                             std::numeric_limits<double>::quiet_NaN());
    GDALClose(expDs);

    const auto report = sicnu::testing::compareRastersBitExact(
        outputPath.toStdString(), expectedPath.toStdString());
    if (!report.identical)
        FAIL(report.detail);
}

TEST_CASE("Atomic spectral index operators execution and equivalence", "[operators][rs][spectral]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString ndviDirectPath = tmp.path() + "/ndvi_direct.tif";
    const QString ndviFacadePath = tmp.path() + "/ndvi_facade.tif";
    const QString eviPath = tmp.path() + "/evi.tif";
    const QString ndwiPath = tmp.path() + "/ndwi.tif";
    const QString saviPath = tmp.path() + "/savi.tif";
    const QString ndbiPath = tmp.path() + "/ndbi.tif";
    const QString mndwiPath = tmp.path() + "/mndwi.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(6);
    for (auto &b : bands) b.assign(W * H, 0.0f);

    // Band 1: Blue, Band 2: Green, Band 3: Red, Band 4: NIR, Band 5: SWIR1, Band 6: SWIR2
    for (size_t i = 0; i < W * H; ++i) {
        bands[0][i] = 10.0f;  // Blue
        bands[1][i] = 20.0f;  // Green
        bands[2][i] = 30.0f;  // Red
        bands[3][i] = 100.0f; // NIR
        bands[4][i] = 60.0f;  // SWIR1
        bands[5][i] = 50.0f;  // SWIR2
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // 1. NDVI Atomic vs Facade Equivalence
    auto ndviOp = RSOperatorRegistry::instance().create("rs:ndvi");
    REQUIRE(ndviOp != nullptr);
    auto facadeOp = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(facadeOp != nullptr);

    Json::Value ndviParams(Json::objectValue);
    ndviParams["input"] = inputPath.toStdString();
    ndviParams["output"] = ndviDirectPath.toStdString();
    ndviParams["nir"] = 4;
    ndviParams["red"] = 3;

    RSOperatorContext ctx;
    Json::Value ndviRes = ndviOp->run(ndviParams, ctx);
    CHECK(ndviRes["index"].asString() == "NDVI");
    CHECK(QFile::exists(ndviDirectPath));

    Json::Value facadeParams(Json::objectValue);
    facadeParams["input"] = inputPath.toStdString();
    facadeParams["output"] = ndviFacadePath.toStdString();
    facadeParams["index"] = "NDVI";
    facadeParams["nir"] = 4;
    facadeParams["red"] = 3;

    Json::Value facadeRes = facadeOp->run(facadeParams, ctx);
    CHECK(QFile::exists(ndviFacadePath));

    GdalDatasetWrapper dsDirect, dsFacade;
    REQUIRE(dsDirect.open(ndviDirectPath));
    REQUIRE(dsFacade.open(ndviFacadePath));
    std::vector<float> directPixels(W * H), facadePixels(W * H);
    REQUIRE(dsDirect.readBandData(1, directPixels.data(), W, H));
    REQUIRE(dsFacade.readBandData(1, facadePixels.data(), W, H));

    const float expectedNdvi = (100.0f - 30.0f) / (100.0f + 30.0f); // 70 / 130 ≈ 0.53846
    for (size_t i = 0; i < W * H; ++i) {
        CHECK(directPixels[i] == Catch::Approx(expectedNdvi).epsilon(1e-4f));
        CHECK(directPixels[i] == Catch::Approx(facadePixels[i]).margin(1e-6f));
    }

    // 2. EVI Atomic Operator
    auto eviOp = RSOperatorRegistry::instance().create("rs:evi");
    REQUIRE(eviOp != nullptr);
    Json::Value eviParams(Json::objectValue);
    eviParams["input"] = inputPath.toStdString();
    eviParams["output"] = eviPath.toStdString();
    eviParams["nir"] = 4;
    eviParams["red"] = 3;
    eviParams["blue"] = 1;
    Json::Value eviRes = eviOp->run(eviParams, ctx);
    CHECK(eviRes["index"].asString() == "EVI");
    CHECK(QFile::exists(eviPath));

    // 3. NDWI Atomic Operator
    auto ndwiOp = RSOperatorRegistry::instance().create("rs:ndwi");
    REQUIRE(ndwiOp != nullptr);
    Json::Value ndwiParams(Json::objectValue);
    ndwiParams["input"] = inputPath.toStdString();
    ndwiParams["output"] = ndwiPath.toStdString();
    ndwiParams["green"] = 2;
    ndwiParams["nir"] = 4;
    Json::Value ndwiRes = ndwiOp->run(ndwiParams, ctx);
    CHECK(ndwiRes["index"].asString() == "NDWI");
    CHECK(QFile::exists(ndwiPath));

    // 4. SAVI Atomic Operator
    auto saviOp = RSOperatorRegistry::instance().create("rs:savi");
    REQUIRE(saviOp != nullptr);
    Json::Value saviParams(Json::objectValue);
    saviParams["input"] = inputPath.toStdString();
    saviParams["output"] = saviPath.toStdString();
    saviParams["nir"] = 4;
    saviParams["red"] = 3;
    Json::Value saviRes = saviOp->run(saviParams, ctx);
    CHECK(saviRes["index"].asString() == "SAVI");
    CHECK(QFile::exists(saviPath));

    // 5. NDBI Atomic Operator
    auto ndbiOp = RSOperatorRegistry::instance().create("rs:ndbi");
    REQUIRE(ndbiOp != nullptr);
    Json::Value ndbiParams(Json::objectValue);
    ndbiParams["input"] = inputPath.toStdString();
    ndbiParams["output"] = ndbiPath.toStdString();
    ndbiParams["swir"] = 5;
    ndbiParams["nir"] = 4;
    Json::Value ndbiRes = ndbiOp->run(ndbiParams, ctx);
    CHECK(ndbiRes["index"].asString() == "NDBI");
    CHECK(QFile::exists(ndbiPath));

    // 6. MNDWI Atomic Operator
    auto mndwiOp = RSOperatorRegistry::instance().create("rs:mndwi");
    REQUIRE(mndwiOp != nullptr);
    Json::Value mndwiParams(Json::objectValue);
    mndwiParams["input"] = inputPath.toStdString();
    mndwiParams["output"] = mndwiPath.toStdString();
    mndwiParams["green"] = 2;
    mndwiParams["swir"] = 5;
    Json::Value mndwiRes = mndwiOp->run(mndwiParams, ctx);
    CHECK(mndwiRes["index"].asString() == "MNDWI");
    CHECK(QFile::exists(mndwiPath));
}

TEST_CASE("Atomic atmospheric correction operators execution and equivalence", "[operators][rs][atmospheric]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/atmos_input.tif";
    const QString dos1DirectPath = tmp.path() + "/dos1_direct.tif";
    const QString dos1FacadePath = tmp.path() + "/dos1_facade.tif";
    const QString dos2Path = tmp.path() + "/dos2.tif";
    const QString dnRadPath = tmp.path() + "/dn_rad.tif";

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<std::vector<float>> bands(2);
    bands[0].resize(W * H);
    bands[1].resize(W * H);
    for (size_t i = 0; i < W * H; ++i) {
        bands[0][i] = 100.0f + static_cast<float>(i);
        bands[1][i] = 200.0f + static_cast<float>(i);
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // 1. dn_to_radiance atomic operator
    auto dnRadOp = RSOperatorRegistry::instance().create("rs:dn_to_radiance");
    REQUIRE(dnRadOp != nullptr);
    Json::Value dnParams(Json::objectValue);
    dnParams["input"] = inputPath.toStdString();
    dnParams["output"] = dnRadPath.toStdString();
    dnParams["band"] = 1;
    dnParams["gain"] = 0.5;
    dnParams["bias"] = 2.0;

    RSOperatorContext ctx;
    Json::Value dnRes = dnRadOp->run(dnParams, ctx);
    CHECK(dnRes["method"].asString() == "dn_to_radiance");
    CHECK(QFile::exists(dnRadPath));
    CHECK(SatelliteProducts::readRadiometricState(dnRadPath) == QString::fromUtf8(SatelliteProducts::kRadiometricStateRadiance));

    // 2. DOS1/DOS2 run in TOA-reflectance space (#610) and REQUIRE product
    // metadata with reflectance coefficients - the radiance-space output was
    // mislabeled surface reflectance. First assert the fail-closed path.
    auto atmosFacadeOp = RSOperatorRegistry::instance().create("rs:atmospheric_correction");
    REQUIRE(atmosFacadeOp != nullptr);
    {
        Json::Value noMetaParams(Json::objectValue);
        noMetaParams["input"] = inputPath.toStdString();
        noMetaParams["output"] = dos1FacadePath.toStdString();
        noMetaParams["method"] = "dos1";
        noMetaParams["band"] = 1;
        REQUIRE_THROWS_WITH(atmosFacadeOp->run(noMetaParams, ctx),
                            Catch::Matchers::ContainsSubstring("reflectance coefficients"));
    }

    // Landsat-style MTL next to the input: reflMult=1e-4, sunEl=30deg, so
    // rho_TOA = 1e-4*DN/sin(30) = 2e-4*DN. Dark level (tiny scene -> global
    // min fallback) = 2e-4*100 = 0.02; Chavez 1%: rho_surf = 2e-4*DN - 0.01.
    QFile mtlFile(tmp.path() + "/atmos_input_MTL.txt");
    REQUIRE(mtlFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream mtlStream(&mtlFile);
    mtlStream << "SPACECRAFT_ID = \"LANDSAT_8\"\n"
              << "SUN_ELEVATION = 30.0\n"
              << "REFLECTANCE_MULT_BAND_1 = 0.0001\n"
              << "REFLECTANCE_ADD_BAND_1 = 0.0\n";
    mtlFile.close();

    auto dos1Op = RSOperatorRegistry::instance().create("rs:atmospheric_dos1");
    REQUIRE(dos1Op != nullptr);

    Json::Value dos1DirectParams(Json::objectValue);
    dos1DirectParams["input"] = inputPath.toStdString();
    dos1DirectParams["output"] = dos1DirectPath.toStdString();
    dos1DirectParams["band"] = 1;
    dos1Op->run(dos1DirectParams, ctx);

    Json::Value dos1FacadeParams(Json::objectValue);
    dos1FacadeParams["input"] = inputPath.toStdString();
    dos1FacadeParams["output"] = dos1FacadePath.toStdString();
    dos1FacadeParams["method"] = "dos1";
    dos1FacadeParams["band"] = 1;
    atmosFacadeOp->run(dos1FacadeParams, ctx);

    GdalDatasetWrapper dsDos1Direct, dsDos1Facade;
    REQUIRE(dsDos1Direct.open(dos1DirectPath));
    REQUIRE(dsDos1Facade.open(dos1FacadePath));
    std::vector<float> directDos1(W * H), facadeDos1(W * H);
    REQUIRE(dsDos1Direct.readBandData(1, directDos1.data(), W, H));
    REQUIRE(dsDos1Facade.readBandData(1, facadeDos1.data(), W, H));
    for (size_t i = 0; i < W * H; ++i) {
        CHECK(directDos1[i] == Catch::Approx(facadeDos1[i]).margin(1e-6f));
        // Reflectance-scale values (the old radiance-space output was 2-3
        // orders of magnitude larger and mislabeled).
        const float expected = 2e-4f * bands[0][i] - 0.01f;
        CHECK(facadeDos1[i] == Catch::Approx(expected).margin(1e-4f));
    }
    CHECK(SatelliteProducts::readRadiometricState(dos1DirectPath) == QString::fromUtf8(SatelliteProducts::kRadiometricStateSurfaceReflectance));

    // 3. DOS2 atomic operator: same scene divided by T = exp(-0.1*1.2).
    auto dos2Op = RSOperatorRegistry::instance().create("rs:atmospheric_dos2");
    REQUIRE(dos2Op != nullptr);
    Json::Value dos2Params(Json::objectValue);
    dos2Params["input"] = inputPath.toStdString();
    dos2Params["output"] = dos2Path.toStdString();
    dos2Params["band"] = 1;
    dos2Params["airmass"] = 1.2;
    dos2Op->run(dos2Params, ctx);
    CHECK(QFile::exists(dos2Path));
    CHECK(SatelliteProducts::readRadiometricState(dos2Path) == QString::fromUtf8(SatelliteProducts::kRadiometricStateSurfaceReflectance));
    {
        GdalDatasetWrapper dsDos2;
        REQUIRE(dsDos2.open(dos2Path));
        std::vector<float> dos2Out(W * H);
        REQUIRE(dsDos2.readBandData(1, dos2Out.data(), W, H));
        const float transmittance = std::exp(-0.12f);
        for (size_t i = 0; i < W * H; ++i) {
            const float expected = (2e-4f * bands[0][i] - 0.01f) / transmittance;
            CHECK(dos2Out[i] == Catch::Approx(expected).margin(1e-4f));
        }
    }
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

TEST_CASE("RS mosaic operator single input preserves metadata and values", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 64;
    constexpr int H = 64;
    const QString inPath = tmp.path() + "/single.tif";
    const QString outPath = tmp.path() + "/single_mosaic.tif";

    std::vector<std::vector<float>> band(1);
    band[0].resize(W * H);
    for (int i = 0; i < W * H; ++i) {
        band[0][i] = static_cast<float>(i + 1);
    }
    std::array<double, 6> gt = {100.0, 0.5, 0.0, 200.0, 0.0, -0.5};
    QString err;
    REQUIRE(writeGdalOutput(inPath, W, H, band, gt, "EPSG:4326", &err));

    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(inPath.toStdString());
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outPath.toStdString());
    CHECK(result["inputCount"].asInt() == 1);
    CHECK(result["width"].asInt() == W);
    CHECK(result["height"].asInt() == H);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outPath));
    CHECK(ds.width() == W);
    CHECK(ds.height() == H);
    const auto outGt = ds.geoTransform();
    CHECK(outGt[0] == Catch::Approx(100.0));
    CHECK(outGt[1] == Catch::Approx(0.5));
    CHECK(outGt[3] == Catch::Approx(200.0));
    CHECK(outGt[5] == Catch::Approx(-0.5));

    std::vector<float> outData(W * H);
    REQUIRE(ds.readBandData(1, outData.data(), W, H));
    for (int i = 0; i < W * H; ++i) {
        CHECK(outData[i] == Catch::Approx(static_cast<float>(i + 1)));
    }
}

TEST_CASE("RS mosaic operator overlap precedence and nodata handling", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 4;
    constexpr int H = 4;

    // Tile 1: 4x4 at (0, 4), values 10.0
    const QString p1 = tmp.path() + "/t1.tif";
    std::vector<std::vector<float>> b1(1);
    b1[0].assign(W * H, 10.0f);
    std::array<double, 6> gt1 = {0.0, 1.0, 0.0, 4.0, 0.0, -1.0};
    QString err;
    REQUIRE(writeGdalOutput(p1, W, H, b1, gt1, "EPSG:4326", &err));

    // Tile 2: 4x4 at (2, 4) — 2 columns overlap with Tile 1.
    // In overlap: some pixels are 20.0, some are -9999.0 (NoData), some are NaN
    const QString p2 = tmp.path() + "/t2.tif";
    std::vector<std::vector<float>> b2(1);
    b2[0].assign(W * H, 20.0f);
    // Set nodata in top-left pixel of tile 2 (which is inside overlap)
    b2[0][0] = -9999.0f;
    // Set NaN in second row, first col (inside overlap)
    b2[0][W] = std::numeric_limits<float>::quiet_NaN();
    std::array<double, 6> gt2 = {2.0, 1.0, 0.0, 4.0, 0.0, -1.0};
    REQUIRE(writeGdalOutput(p2, W, H, b2, gt2, "EPSG:4326", &err));

    // Set declared NoData on tile 2
    {
        GdalDatasetWrapper ds2;
        REQUIRE(ds2.open(p2));
        // Re-open with write access or wrapper to set nodata
    }
    GDALDatasetH gds2 = GDALOpen(p2.toUtf8().constData(), GA_Update);
    REQUIRE(gds2 != nullptr);
    GDALSetRasterNoDataValue(GDALGetRasterBand(gds2, 1), -9999.0);
    GDALClose(gds2);

    const QString outPath = tmp.path() + "/out_overlap.tif";
    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(p1.toStdString());
    params["inputs"].append(p2.toStdString());
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(result["width"].asInt() == 6);
    CHECK(result["height"].asInt() == 4);

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outPath));
    std::vector<float> outData(6 * 4);
    REQUIRE(outDs.readBandData(1, outData.data(), 6, 4));

    // Col 0,1: non-overlapping tile 1 -> should be 10.0
    CHECK(outData[0] == Catch::Approx(10.0f));
    CHECK(outData[1] == Catch::Approx(10.0f));

    // In overlap at (x=2, y=0): tile 2 had -9999.0 (NoData) -> tile 1's 10.0 should be preserved!
    CHECK(outData[2] == Catch::Approx(10.0f));

    // In overlap at (x=3, y=0): tile 2 had 20.0 (valid) -> tile 2's 20.0 should overwrite tile 1!
    CHECK(outData[3] == Catch::Approx(20.0f));

    // In overlap at (x=2, y=1): tile 2 had NaN (NoData) -> tile 1's 10.0 should be preserved!
    CHECK(outData[6 + 2] == Catch::Approx(10.0f));

    // Col 4,5: non-overlapping tile 2 -> should be 20.0
    CHECK(outData[4] == Catch::Approx(20.0f));
    CHECK(outData[5] == Catch::Approx(20.0f));
}

TEST_CASE("RS mosaic operator rejects mismatched CRS and pixel size", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString p1 = tmp.path() + "/t1.tif";
    const QString p2Crs = tmp.path() + "/t2_crs.tif";
    const QString p2Res = tmp.path() + "/t2_res.tif";
    const QString outPath = tmp.path() + "/out_err.tif";

    std::vector<std::vector<float>> b(1);
    b[0].assign(16, 1.0f);
    QString err;

    std::array<double, 6> gt1 = {0.0, 1.0, 0.0, 4.0, 0.0, -1.0};
    REQUIRE(writeGdalOutput(p1, 4, 4, b, gt1, "EPSG:4326", &err));

    // Mismatched CRS
    std::array<double, 6> gt2 = {4.0, 1.0, 0.0, 4.0, 0.0, -1.0};
    REQUIRE(writeGdalOutput(p2Crs, 4, 4, b, gt2, "EPSG:3857", &err));

    // Mismatched Pixel Size (resolution 2.0 vs 1.0)
    std::array<double, 6> gtRes = {4.0, 2.0, 0.0, 4.0, 0.0, -2.0};
    REQUIRE(writeGdalOutput(p2Res, 4, 4, b, gtRes, "EPSG:4326", &err));

    RSOperatorContext ctx;

    // Test CRS mismatch
    {
        Json::Value params(Json::objectValue);
        params["inputs"] = Json::Value(Json::arrayValue);
        params["inputs"].append(p1.toStdString());
        params["inputs"].append(p2Crs.toStdString());
        params["output"] = outPath.toStdString();

        try {
            op->run(params, ctx);
            FAIL("Expected CRS mismatch error");
        } catch (const RSOperatorError &e) {
            CHECK(e.code() == ErrorCode::InvalidInputData);
        }
    }

    // Test Pixel size mismatch
    {
        Json::Value params(Json::objectValue);
        params["inputs"] = Json::Value(Json::arrayValue);
        params["inputs"].append(p1.toStdString());
        params["inputs"].append(p2Res.toStdString());
        params["output"] = outPath.toStdString();

        try {
            op->run(params, ctx);
            FAIL("Expected pixel size mismatch error");
        } catch (const RSOperatorError &e) {
            CHECK(e.code() == ErrorCode::InvalidInputData);
        }
    }
}

TEST_CASE("RS mosaic operator streaming across tile boundaries", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Create 2 rasters of size 400x400 offset such that union is 700x700 (> 512 tile size)
    constexpr int W = 400;
    constexpr int H = 400;

    const QString p1 = tmp.path() + "/tile_a.tif";
    const QString p2 = tmp.path() + "/tile_b.tif";
    const QString outPath = tmp.path() + "/out_large_tiles.tif";

    std::vector<std::vector<float>> b1(1);
    b1[0].assign(W * H, 42.0f);
    std::array<double, 6> gt1 = {0.0, 1.0, 0.0, 700.0, 0.0, -1.0};
    QString err;
    REQUIRE(writeGdalOutput(p1, W, H, b1, gt1, "EPSG:4326", &err));

    std::vector<std::vector<float>> b2(1);
    b2[0].assign(W * H, 84.0f);
    std::array<double, 6> gt2 = {300.0, 1.0, 0.0, 400.0, 0.0, -1.0};
    REQUIRE(writeGdalOutput(p2, W, H, b2, gt2, "EPSG:4326", &err));

    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(p1.toStdString());
    params["inputs"].append(p2.toStdString());
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["width"].asInt() == 700);
    CHECK(result["height"].asInt() == 700);

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outPath));
    CHECK(outDs.width() == 700);
    CHECK(outDs.height() == 700);

    // Check pixel in tile 1 only (e.g. x=50, y=50) -> 42.0
    float val = 0.0f;
    REQUIRE(outDs.readPixel(1, 50, 50, &val));
    CHECK(val == Catch::Approx(42.0f));

    // Check pixel in overlap (e.g. x=350, y=350) -> tile 2 overwrites tile 1 -> 84.0
    REQUIRE(outDs.readPixel(1, 350, 350, &val));
    CHECK(val == Catch::Approx(84.0f));

    // Check pixel in tile 2 only (e.g. x=600, y=600) -> 84.0
    REQUIRE(outDs.readPixel(1, 600, 600, &val));
    CHECK(val == Catch::Approx(84.0f));

    // Check pixel outside both tiles (e.g. x=600, y=50) -> NaN
    REQUIRE(outDs.readPixel(1, 600, 50, &val));
    CHECK(std::isnan(val));
}

TEST_CASE("RS mosaic operator cancellation cleans up incomplete output", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString p1 = tmp.path() + "/t1.tif";
    const QString outPath = tmp.path() + "/out_cancelled.tif";

    std::vector<std::vector<float>> b(1);
    b[0].assign(100 * 100, 1.0f);
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 100.0, 0.0, -1.0};
    QString err;
    REQUIRE(writeGdalOutput(p1, 100, 100, b, gt, "EPSG:4326", &err));

    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(p1.toStdString());
    params["output"] = outPath.toStdString();

    std::atomic<bool> cancelFlag{true};
    RSOperatorContext ctx;
    ctx.setCancelFlag(&cancelFlag);

    try {
        op->run(params, ctx);
        FAIL("Expected operator cancellation");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::Cancelled);
    }

    // Crucial check: incomplete output file must be deleted upon cancellation
    CHECK_FALSE(QFile::exists(outPath));
}

TEST_CASE("RS change detection operator cancellation cleans up incomplete output", "[operators][rs][change_detection]") {
    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString p1 = tmp.path() + "/t1.tif";
    const QString p2 = tmp.path() + "/t2.tif";
    const QString outPath = tmp.path() + "/change_cancelled.tif";

    std::vector<std::vector<float>> b1(1), b2(1);
    b1[0].assign(64 * 64, 10.0f);
    b2[0].assign(64 * 64, 20.0f);
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 64.0, 0.0, -1.0};
    QString err;
    REQUIRE(writeGdalOutput(p1, 64, 64, b1, gt, "EPSG:4326", &err));
    REQUIRE(writeGdalOutput(p2, 64, 64, b2, gt, "EPSG:4326", &err));

    Json::Value params(Json::objectValue);
    params["before"] = p1.toStdString();
    params["after"] = p2.toStdString();
    params["method"] = "difference";
    params["output"] = outPath.toStdString();

    std::atomic<bool> cancelFlag{true};
    RSOperatorContext ctx;
    ctx.setCancelFlag(&cancelFlag);

    try {
        op->run(params, ctx);
        FAIL("Expected operator cancellation");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::Cancelled);
    }

    CHECK_FALSE(QFile::exists(outPath));
}

TEST_CASE("RS mosaic operator rejects rotated and sheared rasters", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString pRot = tmp.path() + "/rotated.tif";
    const QString outPath = tmp.path() + "/out_rot.tif";

    std::vector<std::vector<float>> b(1);
    b[0].assign(16, 1.0f);
    // gt with non-zero rotation terms: gt[2]=0.1, gt[4]=0.1
    std::array<double, 6> gtRot = {0.0, 1.0, 0.1, 4.0, 0.1, -1.0};
    QString err;
    REQUIRE(writeGdalOutput(pRot, 4, 4, b, gtRot, "EPSG:4326", &err));

    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(pRot.toStdString());
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    try {
        op->run(params, ctx);
        FAIL("Expected rotated raster error");
    } catch (const RSOperatorError &e) {
        CHECK(e.code() == ErrorCode::InvalidInputData);
    }
}

TEST_CASE("RS mosaic operator handles bottom-up rasters (positive pixel height)", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:mosaic");
    REQUIRE(op != nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Tile A: bottom-up origin (0, 0), pixelH = +1.0
    const QString pA = tmp.path() + "/bu_a.tif";
    std::vector<std::vector<float>> bA(1);
    bA[0].assign(16, 10.0f);
    std::array<double, 6> gtA = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    QString err;
    REQUIRE(writeGdalOutput(pA, 4, 4, bA, gtA, "EPSG:4326", &err));

    // Tile B: bottom-up origin (2, 2), pixelH = +1.0 (2x2 overlap with Tile A)
    const QString pB = tmp.path() + "/bu_b.tif";
    std::vector<std::vector<float>> bB(1);
    bB[0].assign(16, 20.0f);
    std::array<double, 6> gtB = {2.0, 1.0, 0.0, 2.0, 0.0, 1.0};
    REQUIRE(writeGdalOutput(pB, 4, 4, bB, gtB, "EPSG:4326", &err));

    const QString outPath = tmp.path() + "/out_bu.tif";
    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    params["inputs"].append(pA.toStdString());
    params["inputs"].append(pB.toStdString());
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["width"].asInt() == 6);
    CHECK(result["height"].asInt() == 6);

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outPath));
    CHECK(outDs.width() == 6);
    CHECK(outDs.height() == 6);
    const auto outGt = outDs.geoTransform();
    CHECK(outGt[0] == Catch::Approx(0.0));
    CHECK(outGt[3] == Catch::Approx(0.0));
    CHECK(outGt[5] == Catch::Approx(1.0));
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

TEST_CASE("RS supervised classification writes a probability output (rf)", "[operators][rs]") {
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
    params["method"] = "rf";
    params["probabilityOutput"] = probPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    REQUIRE(QFile::exists(outputPath));
    REQUIRE(QFile::exists(probPath));
    CHECK(result["meanConfidence"].asDouble() > 0.5);
    CHECK(result["meanConfidence"].asDouble() <= 1.0);
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

TEST_CASE("RS change detection statistical threshold degrades on invariant input", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString outputPath = tmp.path() + "/diff.tif";

    // before == after: the difference magnitude is identically zero, so a
    // statistical threshold has stddev == 0 and must fall back to the manual
    // threshold instead of flagging the whole raster as changed (0 >= 0).
    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<float> band(W * H, 100.0f);
    std::array<double, 6> gt = {500000, 10, 0, 4500000, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(beforePath, W, H, {band}, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(afterPath, W, H, {band}, gt, "EPSG:32648", &err));

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "statistical";
    params["threshold"] = 0.5; // fallback

    RSOperatorContext ctx;
    auto op = RSOperatorRegistry::instance().create("rs:change_detection");
    REQUIRE(op != nullptr);
    const Json::Value result = op->run(params, ctx);

    // Not the whole raster flagged as changed.
    CHECK(result["changedPixels"].asUInt64() == 0);
    CHECK(QFile::exists(outputPath));
}

TEST_CASE("RS apply_mask carries the radiometric state through", "[operators][rs]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/product.tif";
    const QString maskPath = tmp.path() + "/mask.tif";
    const QString outputPath = tmp.path() + "/masked.tif";

    std::vector<std::vector<float>> bands(2, std::vector<float>(4, 100.0f));
    std::vector<std::vector<float>> maskBands(1, std::vector<float>(4, 0.0f));
    std::array<double, 6> gt = {500000, 10, 0, 4500000, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 2, 2, bands, gt, "EPSG:32648", &err));
    REQUIRE(writeGdalOutput(maskPath, 2, 2, maskBands, gt, "EPSG:32648", &err));
    REQUIRE(SatelliteProducts::setRadiometricState(
        inputPath, SatelliteProducts::kRadiometricStateToaReflectance));

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["mask"] = maskPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["no_data"] = -9999.0;

    RSOperatorContext ctx;
    auto op = RSOperatorRegistry::instance().create("rs:apply_mask");
    REQUIRE(op != nullptr);
    op->run(params, ctx);

    CHECK(QFile::exists(outputPath));
    // The dataset-level radiometric state survives the mask so downstream
    // change detection keeps its comparability check (ADR 0114).
    CHECK(SatelliteProducts::readRadiometricState(outputPath)
          == QString::fromUtf8(SatelliteProducts::kRadiometricStateToaReflectance));
}

TEST_CASE("GDAL reproject enforces nearest resampling on categorical rasters", "[operators][gdal][categorical]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/categorical.tif";
    const QString outputPath = tmp.path() + "/reprojected.tif";

    // 4x4 raster with discrete class IDs: 10 on left half, 50 on right half
    std::vector<std::vector<float>> bands(1, std::vector<float>(16, 10.0f));
    for (int y = 0; y < 4; ++y) {
        for (int x = 2; x < 4; ++x) {
            bands[0][y * 4 + x] = 50.0f;
        }
    }

    std::array<double, 6> gt = {0, 10, 0, 0, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 4, 4, bands, gt, "EPSG:4326", &err));

    // Set GCI_PaletteIndex color interpretation and CATEGORICAL metadata on band 1
    GDALDatasetH hDS = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
    REQUIRE(hDS != nullptr);
    GDALSetMetadataItem(hDS, "CATEGORICAL", "1", nullptr);
    GDALRasterBandH hBand = GDALGetRasterBand(hDS, 1);
    REQUIRE(hBand != nullptr);
    GDALSetMetadataItem(hBand, "CATEGORICAL", "1", nullptr);
    // GDAL/QGIS convention: "thematic" declares a categorical (classified)
    // layer; "athematic" would declare a continuous one.
    GDALSetMetadataItem(hBand, "LAYER_TYPE", "thematic", nullptr);
    GDALFlushCache(hDS);
    GDALClose(hDS);

    // Verify read-only handle sees dataset as categorical
    GDALDatasetH checkDS = GDALOpen(inputPath.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(checkDS != nullptr);
    CHECK(sicnu::operators::gdal::util::isCategoricalDataset(checkDS));
    GDALClose(checkDS);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["dstCrs"] = "EPSG:3857";
    params["resampling"] = "bilinear"; // Request continuous bilinear resampling

    RSOperatorContext ctx;
    std::vector<std::string> warnings;
    ctx.setLogCallback([&warnings](const std::string& msg, const std::string& level) {
        if (level == "warning") {
            warnings.push_back(msg);
        }
    });

    auto op = RSOperatorRegistry::instance().create("gdal:reproject");
    REQUIRE(op != nullptr);
    op->run(params, ctx);

    CHECK(QFile::exists(outputPath));
    bool hasWarning = false;
    for (const auto& w : warnings) {
        if (w.find("Categorical raster detected") != std::string::npos) {
            hasWarning = true;
            break;
        }
    }
    CHECK(hasWarning);

    // Verify output raster contains only valid discrete class values (10 or 50) and no fractional interpolation
    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    std::vector<float> outData(outDs.width() * outDs.height());
    REQUIRE(outDs.readBandData(1, outData.data(), outDs.width(), outDs.height()));
    for (float val : outData) {
        if (val != 0.0f) { // Nodata or padding
            CHECK((val == 10.0f || val == 50.0f));
        }
    }
}

TEST_CASE("GDAL athematic LAYER_TYPE is not treated as categorical", "[operators][gdal][categorical]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/continuous.tif";

    // Continuous (non-classified) float raster with no palette / RAT /
    // category names: LAYER_TYPE=athematic alone must NOT flag it categorical.
    std::vector<std::vector<float>> bands(1, std::vector<float>(16, 1.5f));
    std::array<double, 6> gt = {0, 10, 0, 0, 0, -10};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 4, 4, bands, gt, "EPSG:4326", &err));

    GDALDatasetH hDS = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
    REQUIRE(hDS != nullptr);
    GDALSetMetadataItem(hDS, "LAYER_TYPE", "athematic", nullptr);
    GDALFlushCache(hDS);
    GDALClose(hDS);

    GDALDatasetH checkDS = GDALOpen(inputPath.toUtf8().constData(), GA_ReadOnly);
    REQUIRE(checkDS != nullptr);
    CHECK_FALSE(sicnu::operators::gdal::util::isCategoricalDataset(checkDS));
    GDALClose(checkDS);
}


TEST_CASE("RS spectral index masks large-sentinel NoData (#444)", "[operators][rs][nodata][444]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("nd_sentinel.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("ndvi_out.tif"));

    // 2x1 raster, NIR band 1, red band 2. Pixel 1 carries the large float
    // sentinel in BOTH bands (NoData pixel); pixel 0 is valid.
    constexpr int W = 2, H = 1;
    const float sentinel = -3.4028235e+38f;
    std::vector<std::vector<float>> bands(2, std::vector<float>(W * H));
    bands[0] = {100.0f, sentinel};  // NIR
    bands[1] = {30.0f, sentinel};   // red
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // Declare the large sentinel as NoData on both bands.
    {
        ensureGdalInit();
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        for (int b = 1; b <= 2; ++b)
            GDALSetRasterNoDataValue(GDALGetRasterBand(ds, b), static_cast<double>(sentinel));
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 1;
    params["red"] = 2;
    RSOperatorContext ctx;
    op->run(params, ctx);

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> px(W);
    REQUIRE(out.readBandData(1, px.data(), W, H));
    const float expected = (100.0f - 30.0f) / (100.0f + 30.0f);
    CHECK(px[0] == Catch::Approx(expected).epsilon(1e-4f));
    // The sentinel pixel must be NaN (masked), not a garbage 0-ish index from
    // (-3.4e38 - -3.4e38) / (-3.4e38 + -3.4e38).
    CHECK(std::isnan(px[1]));
}

TEST_CASE("RS terrain analysis preserves absent NoData without fabricating default tag (#465)", "[operators][rs][terrain][465]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("dem_nodata_absent.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("slope_out.tif"));

    constexpr int W = 4, H = 4;
    std::vector<std::vector<float>> bands(1, std::vector<float>(W * H, 100.0f));
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:terrain_analysis");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["product"] = "slope";
    RSOperatorContext ctx;
    op->run(params, ctx);

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    bool hasNodata = false;
    out.bandNoDataValue(1, &hasNodata);
    CHECK_FALSE(hasNodata);
}

TEST_CASE("RS endmember PPI ignores large sentinel NoData (#467)", "[operators][rs][endmember][467]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("ppi_sentinel.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("ppi_out.tif"));

    constexpr int W = 4, H = 4, B = 4;
    const float sentinel = -3.4028235e+38f;
    std::vector<std::vector<float>> bands(B, std::vector<float>(W * H, 10.0f));
    for (int b = 0; b < B; ++b) {
        bands[b][0] = sentinel; // Pixel 0 is NoData
    }
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    {
        ensureGdalInit();
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        for (int b = 1; b <= B; ++b)
            GDALSetRasterNoDataValue(GDALGetRasterBand(ds, b), static_cast<double>(sentinel));
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:endmember_extraction");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["nEndmembers"] = 2;
    params["projections"] = 16;
    params["threshold"] = 0.5;
    RSOperatorContext ctx;
    Json::Value res = op->run(params, ctx);
    CHECK(res["endmembers"].isArray());
    CHECK(res["endmembers"].size() == 2);
}

TEST_CASE("RS RX anomaly detection ignores large sentinel NoData (#470)", "[operators][rs][rx][470]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("rx_sentinel.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("rx_out.tif"));

    constexpr int W = 4, H = 4, B = 4;
    const float sentinel = -3.4028235e+38f;
    std::vector<std::vector<float>> bands(B, std::vector<float>(W * H, 10.0f));
    for (int b = 0; b < B; ++b) {
        bands[b][0] = sentinel; // Pixel 0 is NoData
    }
    // Make valid pixels have variation so covariance is not singular
    bands[0][1] = 12.0f;
    bands[1][2] = 15.0f;
    bands[2][3] = 8.0f;
    bands[3][4] = 20.0f;
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    {
        ensureGdalInit();
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        for (int b = 1; b <= B; ++b)
            GDALSetRasterNoDataValue(GDALGetRasterBand(ds, b), static_cast<double>(sentinel));
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:rx_anomaly");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    RSOperatorContext ctx;
    Json::Value res = op->run(params, ctx);
    CHECK(res["output"].asString() == outputPath.toStdString());
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> outData(W * H);
    REQUIRE(out.readBandWindow(1, 0, 0, W, H, outData.data()));
    CHECK(std::isnan(outData[0])); // Pixel 0 must be NaN
}

TEST_CASE("RS continuum removal preserves absent NoData without fabricating default tag (#473)", "[operators][rs][continuum][473]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("cr_nodata_absent.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("cr_out.tif"));

    constexpr int W = 4, H = 4, B = 4;
    std::vector<std::vector<float>> bands(B, std::vector<float>(W * H, 0.5f));
    bands[1] = std::vector<float>(W * H, 0.8f);
    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    auto op = RSOperatorRegistry::instance().create("rs:continuum_removal");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    RSOperatorContext ctx;
    op->run(params, ctx);

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    bool hasNodata = false;
    out.bandNoDataValue(1, &hasNodata);
    CHECK_FALSE(hasNodata);
}




// ---------------------------------------------------------------------------
// #691: rs:terrain_analysis streams the DEM in 2048x2048 halo-1 tiles through
// GdalBlockStream and writes via GdalStreamingOutput. Every product must match
// the full-frame TerrainAnalysis kernel, including at tile boundaries and
// raster edges, with the (== + isnan) NoData echo semantics preserved.
// ---------------------------------------------------------------------------
#include "processing/algorithms/terrain_analysis.h"

#include <algorithm>
#include <limits>

namespace {

/// Deterministic DEM with gradients and steps; with withNodata, scattered
/// sentinel pixels including all four raster corners.
std::vector<float> streamingTerrainDem(int w, int h, float sentinel, bool withNodata) {
    std::vector<float> dem(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            dem[static_cast<size_t>(y) * w + x] =
                100.0f + 1.5f * static_cast<float>((x * 7 + y * 11) % 97)
                + 0.25f * static_cast<float>((x / 32) % 2);
            if (withNodata && (x * 31 + y * 17) % 211 == 0)
                dem[static_cast<size_t>(y) * w + x] = sentinel;
        }
    }
    if (withNodata) {
        dem[0] = sentinel;
        dem[w - 1] = sentinel;
        dem[static_cast<size_t>(h - 1) * w] = sentinel;
        dem[static_cast<size_t>(h - 1) * w + w - 1] = sentinel;
    }
    return dem;
}

/// Runs rs:terrain_analysis for one product and reads the single output band.
std::vector<float> runTerrainProduct(const QTemporaryDir &dir, const QString &input,
                                     const std::string &product, double cellSize,
                                     bool passNodata, double nodata, int w, int h) {
    const QString outputPath = dir.filePath(QString::fromStdString(product + "_out.tif"));
    auto op = RSOperatorRegistry::instance().create("rs:terrain_analysis");
    REQUIRE(op != nullptr);
    Json::Value params(Json::objectValue);
    params["input"] = input.toStdString();
    params["output"] = outputPath.toStdString();
    params["product"] = product;
    params["cellSize"] = cellSize;
    if (passNodata)
        params["nodata"] = nodata;
    RSOperatorContext ctx;
    const Json::Value result = op->run(params, ctx);
    CHECK(result["width"].asInt() == w);
    CHECK(result["height"].asInt() == h);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(static_cast<size_t>(w) * h);
    REQUIRE(ds.readBandData(1, out.data(), w, h));
    return out;
}

void requireSameTerrain(const std::vector<float> &expected, const std::vector<float> &actual) {
    REQUIRE(expected.size() == actual.size());
    size_t nanMismatches = 0;
    size_t valueMismatches = 0;
    double worstDiff = 0.0;
    for (size_t i = 0; i < expected.size(); ++i) {
        const float e = expected[i];
        const float a = actual[i];
        if (std::isnan(e) || std::isnan(a)) {
            if (!(std::isnan(e) && std::isnan(a)))
                ++nanMismatches;
            continue;
        }
        if (e != a) {
            ++valueMismatches;
            worstDiff = std::max(worstDiff, std::abs(static_cast<double>(e) - a));
        }
    }
    if (nanMismatches != 0 || worstDiff > 1e-4) {
        FAIL("streamed terrain output differs from full-frame kernel: nanMismatches="
             << nanMismatches << " valueMismatches=" << valueMismatches
             << " worstDiff=" << worstDiff);
    }
}

} // namespace

TEST_CASE("RS terrain analysis streams halo tiles matching the full-frame kernel (#691)",
          "[operators][rs][terrain]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // 2x2 tiles of 2048 with awkward edges: 2300 = 2048+252, 2100 = 2048+52.
    constexpr int W = 2300;
    constexpr int H = 2100;
    constexpr float kNodata = -9999.0f;
    const std::vector<float> dem = streamingTerrainDem(W, H, kNodata, true);
    const QString inputPath = tmp.filePath(QStringLiteral("dem.tif"));
    {
        const std::vector<std::vector<float>> bands(1, dem);
        REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());
        ensureGdalInit();
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        GDALSetRasterNoDataValue(GDALGetRasterBand(ds, 1), kNodata);
        GDALClose(ds);
    }

    // Full-frame reference: the operator reads raw values and hands the
    // compute nodata straight to the kernels, so the kernel called on the raw
    // DEM with the declared sentinel is the exact spec.
    constexpr double kCellSize = 30.0;
    const float cellF = static_cast<float>(kCellSize);

    SECTION("slope") {
        std::vector<float> expected(dem.size());
        REQUIRE(TerrainAnalysis::slope(dem.data(), expected.data(), W, H, cellF, cellF, kNodata));
        const std::vector<float> actual =
            runTerrainProduct(tmp, inputPath, "slope", kCellSize, true, kNodata, W, H);
        requireSameTerrain(expected, actual);
        CHECK(actual[0] == kNodata);                                       // corner echo
        CHECK(actual[static_cast<size_t>(H - 1) * W + W - 1] == kNodata);  // corner echo
    }

    SECTION("hillshade") {
        std::vector<float> expected(dem.size());
        REQUIRE(TerrainAnalysis::hillshade(dem.data(), expected.data(), W, H, cellF, cellF,
                                           kNodata, 315.0f, 45.0f));
        const std::vector<float> actual =
            runTerrainProduct(tmp, inputPath, "hillshade", kCellSize, true, kNodata, W, H);
        requireSameTerrain(expected, actual);
        CHECK(actual[static_cast<size_t>(H / 2) * W + W / 2] ==
              Catch::Approx(expected[static_cast<size_t>(H / 2) * W + W / 2]).margin(1e-6));
    }

    SECTION("tpi") {
        std::vector<float> expected(dem.size());
        REQUIRE(TerrainAnalysis::tpi(dem.data(), expected.data(), W, H, kNodata));
        const std::vector<float> actual =
            runTerrainProduct(tmp, inputPath, "tpi", kCellSize, true, kNodata, W, H);
        requireSameTerrain(expected, actual);

        // Output declares the input's NoData.
        GdalDatasetWrapper out;
        REQUIRE(out.open(tmp.filePath(QStringLiteral("tpi_out.tif"))));
        bool hasNodata = false;
        const double nd = out.bandNoDataValue(1, &hasNodata);
        CHECK(hasNodata);
        CHECK(static_cast<float>(nd) == kNodata);
    }
}

TEST_CASE("RS terrain analysis echoes NaN through the streaming path and keeps absent NoData undeclared (#691)",
          "[operators][rs][terrain][nodata]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Small single-tile DEM with raw NaN cells and NO declared NoData: the
    // compute nodata is NaN, kernels echo it, and the output must stay
    // undeclared (#465 semantics through the streaming path).
    constexpr int W = 64;
    constexpr int H = 48;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> dem = streamingTerrainDem(W, H, nan, false);
    dem[static_cast<size_t>(5) * W + 7] = nan;
    dem[0] = nan;
    const QString inputPath = tmp.filePath(QStringLiteral("dem_nan.tif"));
    {
        const std::vector<std::vector<float>> bands(1, dem);
        REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());
    }

    std::vector<float> expected(dem.size());
    REQUIRE(TerrainAnalysis::roughness(dem.data(), expected.data(), W, H, nan));
    const std::vector<float> actual =
        runTerrainProduct(tmp, inputPath, "roughness", 30.0, false, 0.0, W, H);
    requireSameTerrain(expected, actual);
    CHECK(std::isnan(actual[static_cast<size_t>(5) * W + 7]));
    CHECK(std::isnan(actual[0]));

    GdalDatasetWrapper out;
    REQUIRE(out.open(tmp.filePath(QStringLiteral("roughness_out.tif"))));
    bool hasNodata = false;
    out.bandNoDataValue(1, &hasNodata);
    CHECK_FALSE(hasNodata);
}
