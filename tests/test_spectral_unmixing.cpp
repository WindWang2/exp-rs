// test_spectral_unmixing.cpp — linear spectral unmixing kernel + operator
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
#include "processing/algorithms/spectral_unmixing.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_unmixing";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

} // namespace

TEST_CASE("Spectral unmixing recovers known abundances", "[unmixing][kernel]")
{
    // Two endmembers over three bands.
    const std::vector<float> endmembers = {
        0.6f, 0.2f, 0.2f,   // E1
        0.1f, 0.4f, 0.5f,   // E2
    };

    // Pixel 0: 0.6 E1 + 0.4 E2 (exact mixture, sums to 1).
    // Pixel 1: 0.2 E1 + 0.8 E2.
    std::vector<float> pixels;
    for (int b = 0; b < 3; ++b)
        pixels.push_back(0.6f * endmembers[b] + 0.4f * endmembers[3 + b]);
    for (int b = 0; b < 3; ++b)
        pixels.push_back(0.2f * endmembers[b] + 0.8f * endmembers[3 + b]);

    SpectralUnmixing::UnmixResult result;
    QString err;
    REQUIRE(SpectralUnmixing::unmix(pixels.data(), 2, 3, endmembers.data(), 2,
                                    &result, &err));
    REQUIRE(result.abundances.size() == 4);

    // Pixel 0 abundances ≈ [0.6, 0.4]; pixel 1 ≈ [0.2, 0.8].
    CHECK(result.abundances[0] == Approx(0.6f).margin(0.02f));
    CHECK(result.abundances[1] == Approx(0.4f).margin(0.02f));
    CHECK(result.abundances[2] == Approx(0.2f).margin(0.02f));
    CHECK(result.abundances[3] == Approx(0.8f).margin(0.02f));

    // Exact mixtures reconstruct almost perfectly.
    CHECK(result.reconstructionError[0] == Approx(0.0f).margin(1e-3f));
    CHECK(result.reconstructionError[1] == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("Spectral unmixing reports a positive error off the simplex", "[unmixing][kernel]")
{
    const std::vector<float> endmembers = {
        0.6f, 0.2f, 0.2f,
        0.1f, 0.4f, 0.5f,
    };
    // A pixel unlike both endmembers (e.g. a brightness outlier).
    const std::vector<float> pixel = {0.9f, 0.05f, 0.05f};

    SpectralUnmixing::UnmixResult result;
    QString err;
    REQUIRE(SpectralUnmixing::unmix(pixel.data(), 1, 3, endmembers.data(), 2,
                                    &result, &err));
    REQUIRE(result.abundances.size() == 2);
    // Abundances are clipped + normalized to unit sum.
    CHECK(result.abundances[0] + result.abundances[1] == Approx(1.0f).margin(1e-4f));
    CHECK(result.reconstructionError[0] > 1e-2f);
}

TEST_CASE("Spectral unmixing guards invalid arguments", "[unmixing][kernel]")
{
    const std::vector<float> endmembers = {0.6f, 0.2f, 0.2f};
    const std::vector<float> pixel = {0.1f, 0.2f, 0.3f};
    SpectralUnmixing::UnmixResult result;
    QString err;

    CHECK_FALSE(SpectralUnmixing::unmix(pixel.data(), 0, 3, endmembers.data(), 1,
                                        &result, &err));
    CHECK_FALSE(SpectralUnmixing::unmix(pixel.data(), 1, 0, endmembers.data(), 1,
                                        &result, &err));
    CHECK_FALSE(SpectralUnmixing::unmix(pixel.data(), 1, 3, endmembers.data(), 0,
                                        &result, &err));
    // More endmembers than bands is invalid.
    const std::vector<float> twoEnds = {0.6f, 0.2f, 0.2f, 0.1f, 0.4f, 0.5f};
    CHECK_FALSE(SpectralUnmixing::unmix(pixel.data(), 1, 2, twoEnds.data(), 3,
                                        &result, &err));
}

TEST_CASE("rs:spectral_unmixing writes abundance bands", "[operators][rs][unmixing]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/abundance.tif";
    const QString errorPath = tmp.path() + "/error.tif";

    constexpr int W = 2;
    constexpr int H = 1;
    const std::vector<float> e1 = {0.6f, 0.2f, 0.2f};
    const std::vector<float> e2 = {0.1f, 0.4f, 0.5f};
    std::vector<std::vector<float>> bands(3, std::vector<float>(W * H, 0.0f));
    for (int p = 0; p < W * H; ++p)
    {
        const float a1 = (p == 0) ? 0.6f : 0.2f;
        const float a2 = 1.0f - a1;
        for (int b = 0; b < 3; ++b)
            bands[b][p] = a1 * e1[b] + a2 * e2[b];
    }
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:spectral_unmixing");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["errorOut"] = errorPath.toStdString();
    Json::Value ends(Json::arrayValue);
    Json::Value em1(Json::arrayValue);
    em1.append(0.6); em1.append(0.2); em1.append(0.2);
    Json::Value em2(Json::arrayValue);
    em2.append(0.1); em2.append(0.4); em2.append(0.5);
    ends.append(em1);
    ends.append(em2);
    params["endmembers"] = ends;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["endmembers"].asInt() == 2);
    CHECK(result["meanError"].asDouble() < 1e-3);
    CHECK(QFile::exists(outputPath));
    CHECK(QFile::exists(errorPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.bandCount() == 2);
    std::vector<float> a1(W * H);
    REQUIRE(ds.readBandData(1, a1.data(), W, H));
    CHECK(a1[0] == Approx(0.6f).margin(0.02f));
    CHECK(a1[1] == Approx(0.2f).margin(0.02f));
}
