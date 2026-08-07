// test_spectral_anomaly.cpp — Reed-Xiaoli anomaly detector kernel + operator
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include <array>
#include <cmath>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/spectral_anomaly.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_anomaly";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

} // namespace

TEST_CASE("RX detector flags an injected outlier", "[rx][kernel]")
{
    // 2-band background cluster around (0,0) plus one strong outlier.
    constexpr size_t backgroundPixels = 64;
    constexpr size_t totalPixels = backgroundPixels + 1;
    std::vector<float> pixels(totalPixels * 2, 0.0f);
    for (size_t p = 0; p < backgroundPixels; ++p)
    {
        // Deterministic pseudo-random small offsets.
        pixels[p * 2 + 0] = static_cast<float>(((p * 1103515245u + 12345u) % 1000u) / 1000.0 - 0.5);
        pixels[p * 2 + 1] = static_cast<float>(((p * 2654435761u + 54321u) % 1000u) / 1000.0 - 0.5);
    }
    const size_t outlier = backgroundPixels;
    pixels[outlier * 2 + 0] = 10.0f;
    pixels[outlier * 2 + 1] = 10.0f;

    std::vector<float> rx;
    QString err;
    REQUIRE(SpectralAnomaly::rxDetector(pixels.data(), totalPixels, 2, &rx, &err));
    REQUIRE(rx.size() == totalPixels);

    // The outlier dominates the background scores (its inclusion slightly
    // inflates the background covariance, a known RX sensitivity).
    for (size_t p = 0; p < backgroundPixels; ++p)
        CHECK(rx[p] < 25.0f);
    CHECK(rx[outlier] > 20.0f);
    // The maximum score is the outlier's.
    const float maxScore = *std::max_element(rx.begin(), rx.end());
    CHECK(maxScore == rx[outlier]);
}

TEST_CASE("RX detector guards invalid arguments", "[rx][kernel]")
{
    std::vector<float> pixels = {0.0f, 0.0f, 1.0f, 1.0f};
    std::vector<float> rx;
    QString err;
    CHECK_FALSE(SpectralAnomaly::rxDetector(pixels.data(), 0, 2, &rx, &err));
    CHECK_FALSE(SpectralAnomaly::rxDetector(pixels.data(), 2, 0, &rx, &err));
    CHECK_FALSE(SpectralAnomaly::rxDetector(nullptr, 2, 2, &rx, &err));
}

TEST_CASE("rs:rx_anomaly writes a single-band score raster", "[operators][rs][rx]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/rx.tif";

    constexpr int W = 9;
    constexpr int H = 1;
    // 8 background pixels near 0, one outlier at pixel 8.
    std::vector<std::vector<float>> bands(2, std::vector<float>(W * H, 0.0f));
    for (int p = 0; p < 8; ++p)
    {
        bands[0][p] = static_cast<float>(((p * 1103515245u + 12345u) % 1000u) / 1000.0 - 0.5);
        bands[1][p] = static_cast<float>(((p * 2654435761u + 54321u) % 1000u) / 1000.0 - 0.5);
    }
    bands[0][8] = 8.0f;
    bands[1][8] = 8.0f;

    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:rx_anomaly");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(QFile::exists(outputPath));
    CHECK(result["max"].asDouble() > result["mean"].asDouble());

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.bandCount() == 1);
    std::vector<float> scores(W * H);
    REQUIRE(ds.readBandData(1, scores.data(), W, H));
    // The outlier pixel carries the maximum score (score magnitude is damped
    // because the small background sample is contaminated by the outlier).
    const float maxScore = *std::max_element(scores.begin(), scores.end());
    CHECK(scores[8] == maxScore);
    CHECK(maxScore > 5.0f);
}
