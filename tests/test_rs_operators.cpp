// Native RS operator tests — verify schema, registration, and execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

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
