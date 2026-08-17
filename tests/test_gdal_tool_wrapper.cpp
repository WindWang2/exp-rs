// tests/test_gdal_tool_wrapper.cpp — Test GDAL tool wrapper error handling
#include <catch2/catch_test_macros.hpp>

#include "processing/providers/generic_cli/generic_cli_algorithm.h"
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include "qgsexception.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>

// Test that MergedChannels mode captures stderr in readAllStandardOutput
TEST_CASE("QProcess MergedChannels captures stderr", "[gdal][tool][error]") {
    // Run a command that writes to stderr and fails
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With MergedChannels, stderr should be in readAllStandardOutput
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stdout, not stderr
    CHECK(stdoutOutput.contains("error message"));
    CHECK(stderrOutput.isEmpty()); // stderr is empty because channels are merged
}

// Test that SeparateChannels mode keeps stderr separate
TEST_CASE("QProcess SeparateChannels keeps stderr separate", "[gdal][tool][error]") {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start("bash", {"-c", "echo 'error message' >&2; exit 1"});

    REQUIRE(proc.waitForStarted(5000));
    proc.waitForFinished(5000);

    // With SeparateChannels, stderr should be in readAllStandardError
    QByteArray stdoutOutput = proc.readAllStandardOutput();
    QByteArray stderrOutput = proc.readAllStandardError();

    // The error message should be in stderr
    CHECK(stdoutOutput.isEmpty());
    CHECK(stderrOutput.contains("error message"));
}

TEST_CASE("GenericCliAlgorithm initializes parameters and preview", "[gdal][tool][error]") {
    QJsonObject config;
    config["id"] = "test_custom_gdal";
    config["name"] = "Custom GDAL Tool";
    config["command"] = "gdalinfo";
    QJsonArray params;
    QJsonObject p1;
    p1["name"] = "INPUT";
    p1["type"] = "raster";
    params.append(p1);
    config["parameters"] = params;
    config["args"] = QJsonArray{"-stats", "{INPUT}"};

    GenericCliAlgorithm alg(config, "gdal");
    REQUIRE(alg.name() == "test_custom_gdal");
    REQUIRE(alg.displayName() == "Custom GDAL Tool");

    QgsProcessingContext context;
    QVariantMap inputParams;
    inputParams["INPUT"] = "/tmp/fake_input.tif";
    QString preview = alg.commandLinePreview(inputParams, context);
    CHECK(preview.contains("gdalinfo"));
    CHECK(preview.contains("-stats"));
    CHECK(preview.contains("/tmp/fake_input.tif"));
}

TEST_CASE("GenericCliAlgorithm catches exit failures with QgsProcessingException", "[gdal][tool][error]") {
    QJsonObject config;
    config["id"] = "test_failing_cli";
    config["name"] = "Failing Tool";
    config["command"] = "bash";
    config["args"] = QJsonArray{"-c", "echo 'critical gdal failure' >&2; exit 3"};

    GenericCliAlgorithm alg(config, "gdal");
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    bool ok = true;
    bool threw = false;
    QString caughtMessage;
    try {
        alg.run(params, context, &feedback, &ok, {}, false);
    } catch (const QgsProcessingException &e) {
        threw = true;
        caughtMessage = e.what();
    } catch (const std::exception &e) {
        threw = true;
        caughtMessage = QString::fromUtf8(e.what());
    }

    CHECK(threw);
    CHECK(caughtMessage.contains("3"));
}
