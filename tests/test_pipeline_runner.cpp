// Pipeline Runner tests — verify JSON validation and execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include <array>
#include <vector>

#include "cli/rs_pipeline_runner.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::cli;

namespace {
// RSOperationLogger uses QDateTime; GDAL paths use Qt strings — need an app.
int &pipelineAppArgc()
{
    static int argc = 1;
    return argc;
}
char pipelineAppArgv0[] = "test_pipeline_runner";
char *pipelineAppArgv[] = {pipelineAppArgv0, nullptr};

QCoreApplication *ensurePipelineApp()
{
    if (!QCoreApplication::instance())
        return new QCoreApplication(pipelineAppArgc(), pipelineAppArgv);
    return static_cast<QCoreApplication *>(QCoreApplication::instance());
}
} // namespace

namespace {

Json::Value makeNdviPipeline(const QString& input, const QString& output) {
    Json::Value root(Json::objectValue);
    root["name"] = "NDVI test pipeline";
    root["version"] = "1.0";

    Json::Value step(Json::objectValue);
    step["operator"] = "rs:spectral_index";
    step["params"]["input"] = input.toStdString();
    step["params"]["output"] = output.toStdString();
    step["params"]["index"] = "NDVI";
    step["params"]["nir"] = 4;
    step["params"]["red"] = 3;

    root["steps"].append(step);
    return root;
}

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

TEST_CASE("Pipeline JSON validation", "[cli][pipeline]") {
    SECTION("Valid pipeline passes") {
        Json::Value root(Json::objectValue);
        root["steps"].append(Json::Value(Json::objectValue));
        root["steps"][0]["operator"] = "test:op";
        std::string error;
        CHECK(RsPipelineRunner::validatePipelineJson(root, &error) == true);
    }

    SECTION("Missing steps fails") {
        Json::Value root(Json::objectValue);
        std::string error;
        CHECK(RsPipelineRunner::validatePipelineJson(root, &error) == false);
        CHECK(error.find("steps") != std::string::npos);
    }

    SECTION("Missing operator fails") {
        Json::Value root(Json::objectValue);
        root["steps"].append(Json::Value(Json::objectValue));
        std::string error;
        CHECK(RsPipelineRunner::validatePipelineJson(root, &error) == false);
    }
}

TEST_CASE("Pipeline runner executes NDVI step", "[cli][pipeline]") {
    ensurePipelineApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/ndvi.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(4);
    for (auto &b : bands) b.assign(W * H, 0.0f);
    for (size_t i = 0; i < W * H; ++i) {
        bands[3][i] = 100.0f; // NIR
        bands[2][i] = 50.0f;  // Red
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    RsPipelineRunner runner;
    const auto result = runner.runFromJson(makeNdviPipeline(inputPath, outputPath));

    REQUIRE(result.success == true);
    REQUIRE(result.steps.size() == 1);
    CHECK(result.steps[0].success == true);
    CHECK(QFile::exists(outputPath));
}

TEST_CASE("Pipeline runner reports progress and logs", "[cli][pipeline]") {
    ensurePipelineApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/ndvi.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(4);
    for (auto &b : bands) b.assign(W * H, 0.0f);
    for (size_t i = 0; i < W * H; ++i) {
        bands[3][i] = 100.0f;
        bands[2][i] = 50.0f;
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    int progressCount = 0;
    int logCount = 0;

    RsPipelineRunner runner(
        [&progressCount](int, int, double, const std::string&) { progressCount++; },
        [&logCount](const std::string&, const std::string&) { logCount++; }
    );

    const auto result = runner.runFromJson(makeNdviPipeline(inputPath, outputPath));

    REQUIRE(result.success == true);
    CHECK(progressCount > 0);
    CHECK(logCount > 0);
}

TEST_CASE("Pipeline runner fails on unknown operator", "[cli][pipeline]") {
    Json::Value root(Json::objectValue);
    root["steps"][0]["operator"] = "unknown:operator";
    root["steps"][0]["params"] = Json::Value(Json::objectValue);

    RsPipelineRunner runner;
    const auto result = runner.runFromJson(root);

    CHECK(result.success == false);
    CHECK(result.errorMessage.find("not registered") != std::string::npos);
    REQUIRE(result.steps.size() == 1);
    CHECK(result.steps[0].success == false);
}

TEST_CASE("Pipeline runner fails on missing input", "[cli][pipeline]") {
    ensurePipelineApp();
    Json::Value root(Json::objectValue);
    root["steps"][0]["operator"] = "rs:spectral_index";
    root["steps"][0]["params"]["input"] = "nonexistent.tif";
    root["steps"][0]["params"]["output"] = "out.tif";
    root["steps"][0]["params"]["index"] = "NDVI";

    RsPipelineRunner runner;
    const auto result = runner.runFromJson(root);

    CHECK(result.success == false);
    CHECK(result.errorMessage.find("failed") != std::string::npos);
}

TEST_CASE("Pipeline runner from file", "[cli][pipeline]") {
    ensurePipelineApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString pipelinePath = tmp.path() + "/pipeline.json";
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/ndvi.tif";

    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<std::vector<float>> bands(4);
    for (auto &b : bands) b.assign(W * H, 0.0f);
    for (size_t i = 0; i < W * H; ++i) {
        bands[3][i] = 100.0f;
        bands[2][i] = 50.0f;
    }

    REQUIRE(writeTestRaster(inputPath, W, H, bands).empty());

    // Write pipeline file
    Json::Value pipeline = makeNdviPipeline(inputPath, outputPath);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    const std::string jsonText = Json::writeString(builder, pipeline);

    QFile file(pipelinePath);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(jsonText.c_str());
    file.close();

    RsPipelineRunner runner;
    const auto result = runner.runFromFile(pipelinePath.toStdString());

    REQUIRE(result.success == true);
    CHECK(QFile::exists(outputPath));
}
