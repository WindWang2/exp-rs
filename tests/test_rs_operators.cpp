// Native RS operator tests — verify schema, registration, and execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

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

} // namespace

TEST_CASE("Native RS operators are registered", "[operators][rs]") {
    auto& registry = RSOperatorRegistry::instance();

    CHECK(registry.hasOperator("rs:spectral_index"));
    CHECK(registry.hasOperator("rs:band_math"));
    CHECK(registry.hasOperator("rs:atmospheric_correction"));
    CHECK(registry.hasOperator("rs:change_detection"));
    CHECK(registry.hasOperator("rs:image_fusion"));
    CHECK(registry.hasOperator("rs:terrain_analysis"));
    CHECK(registry.hasOperator("rs:pca"));
    CHECK(registry.hasOperator("rs:mosaic"));
#ifdef SICNU_HAS_OPENCV
    CHECK(registry.hasOperator("rs:kmeans_classification"));
    CHECK(registry.hasOperator("rs:supervised_classification"));
    CHECK(registry.hasOperator("rs:obia_segment"));
    CHECK(registry.hasOperator("rs:obia_classify"));
    CHECK(registry.hasOperator("rs:segment_stats"));
#endif
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
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.width() == W);
    CHECK(ds.height() == H);
    CHECK(ds.bandCount() == 1);
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
TEST_CASE("RS obia_classify schema", "[operators][rs]") {
    auto op = RSOperatorRegistry::instance().create("rs:obia_classify");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:obia_classify");
    auto schema = op->schema();
    CHECK(schema["properties"].isMember("training"));
    CHECK(schema["properties"].isMember("minLabelPixels"));
}
#endif
