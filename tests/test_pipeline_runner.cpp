// Pipeline Runner tests — verify JSON validation and execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>

#include <qgsapplication.h>

#include <json/json.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "cli/rs_pipeline_runner.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
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
        new QCoreApplication(pipelineAppArgc(), pipelineAppArgv);
    // Initialize the QGIS runtime profiler on the main thread. Pipeline
    // execution logs through QgsMessageLog from the JobEngine worker thread;
    // without a main-thread QgsApplication/profiler, QgsRuntimeProfiler's
    // threadLocalInstance() leaves sMainProfiler null and the Debug-build
    // Q_ASSERT aborts the process.
    QgsApplication::profiler();
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
    CHECK((result.errorMessage.find("not registered") != std::string::npos
           || result.errorMessage.find("Unknown") != std::string::npos
           || result.errorMessage.find("failed") != std::string::npos));
    REQUIRE(result.steps.size() == 1);
    CHECK(result.steps[0].success == false);
}

TEST_CASE("Pipeline runner two-step DAG resolves $stepId.output via TaskCenter", "[cli][pipeline][task_center]") {
    ensurePipelineApp();
    auto& engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    std::string observedStep2Input;
    engine.registerExecutor("cli:step1", [](const sicnu::jobs::JobRequest& req, sicnu::operators::RSOperatorContext& ctx) {
        ctx.logInfo("cli step1");
        Json::Value result(Json::objectValue);
        if (req.params.isMember("output") && req.params["output"].isString())
            result["output"] = req.params["output"].asString();
        else
            result["output"] = "/tmp/cli_step1.tif";
        return result;
    });
    engine.registerExecutor("cli:step2", [&](const sicnu::jobs::JobRequest& req, sicnu::operators::RSOperatorContext& ctx) {
        ctx.logInfo("cli step2");
        if (req.params.isMember("input") && req.params["input"].isString())
            observedStep2Input = req.params["input"].asString();
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/cli_step2.tif";
        return result;
    });

    Json::Value root(Json::objectValue);
    root["name"] = "two-step TaskCenter CLI";
    Json::Value s1(Json::objectValue);
    s1["id"] = "s1";
    s1["operator"] = "cli:step1";
    s1["params"]["output"] = "/tmp/cli_step1.tif";
    Json::Value s2(Json::objectValue);
    s2["id"] = "s2";
    s2["operator"] = "cli:step2";
    s2["params"]["input"] = "$s1.output";
    s2["params"]["output"] = "/tmp/cli_step2.tif";
    root["steps"].append(s1);
    root["steps"].append(s2);

    RsPipelineRunner runner;
    const auto result = runner.runFromJson(root);

    REQUIRE(result.success == true);
    REQUIRE(result.steps.size() == 2);
    CHECK(result.steps[0].success == true);
    CHECK(result.steps[1].success == true);
    CHECK(observedStep2Input == "/tmp/cli_step1.tif");

    engine.clearExecutors();
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

TEST_CASE( "RsPipelineRunner addPythonPluginDirectory validates plugin directories", "[cli][pipeline_runner][python]" )
{
    ensurePipelineApp();
    RsPipelineRunner runner;
    std::string error;
    CHECK( runner.addPythonPluginDirectory( "/nonexistent/plugin/dir", &error ) == false );
    CHECK( !error.empty() );

    error.clear();
    const QString fixtureDir = QDir( QStringLiteral( TEST_DATA_DIR ) ).filePath(
        QStringLiteral( "plugins/echo_plugin" ) );
    CHECK( runner.addPythonPluginDirectory( fixtureDir.toStdString(), &error ) == true );
}

TEST_CASE( "Mixed Python/C++ pipeline end-to-end", "[cli][pipeline_runner][python]" )
{
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    ensurePipelineApp();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

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
    REQUIRE( writeTestRaster(inputPath, W, H, bands).empty() );

    sicnu::data::DataManager dataManager;

    int progressCount = 0;
    std::vector<std::string> logMessages;
    RsPipelineRunner runner(
        [&progressCount](int, int, double, const std::string&) { progressCount++; },
        [&logMessages](const std::string&, const std::string& message) { logMessages.push_back(message); }
    );
    runner.setAssetRegistry( &dataManager );

    const QString pluginDir = QDir( QStringLiteral( TEST_DATA_DIR ) ).filePath(
        QStringLiteral( "plugins/echo_plugin" ) );
    std::string pluginError;
    REQUIRE( runner.addPythonPluginDirectory( pluginDir.toStdString(), &pluginError ) );

    // The two operators cannot chain data directly — py:echo_plugin echoes its
    // params and produces no raster output — so they run as independent steps
    // in one pipeline.
    Json::Value root(Json::objectValue);
    root["name"] = "mixed python/cpp pipeline";
    Json::Value s1(Json::objectValue);
    s1["id"] = "s1";
    s1["operator"] = "py:echo_plugin";
    s1["params"]["value"] = 7; // working shape per test_python_plugin_host.cpp
    Json::Value s2(Json::objectValue);
    s2["id"] = "s2";
    s2["operator"] = "rs:spectral_index";
    s2["params"]["input"] = inputPath.toStdString();
    s2["params"]["output"] = outputPath.toStdString();
    s2["params"]["index"] = "NDVI";
    s2["params"]["nir"] = 4;
    s2["params"]["red"] = 3;
    root["steps"].append(s1);
    root["steps"].append(s2);

    const auto result = runner.runFromJson(root);
    INFO( result.errorMessage );
    REQUIRE( result.success == true );
    REQUIRE( result.steps.size() == 2 );
    CHECK( result.steps[0].success == true );
    CHECK( result.steps[1].success == true );

    // Progress/log callbacks captured the plugin load and the step messages.
    CHECK( progressCount > 0 );
    const auto hasLog = [&logMessages](const std::string &needle) {
        return std::any_of( logMessages.begin(), logMessages.end(),
                            [&](const std::string &m) { return m.find(needle) != std::string::npos; } );
    };
    CHECK( hasLog( "Loaded Python plugin" ) );
    CHECK( hasLog( "Starting pipeline" ) );
    CHECK( hasLog( "Pipeline completed successfully" ) );

    // The spectral_index step output is registered as a Data Asset (the echo
    // step has no output path and registers nothing).
    const auto assets = dataManager.assets();
    REQUIRE( assets.size() == 1 );
    CHECK( assets[0].source().canonicalSource == outputPath );
#endif
}
