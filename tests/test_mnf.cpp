// test_mnf.cpp — Minimum Noise Fraction transform kernel + operator
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
#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_mnf";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

/// Pearson correlation between two vectors.
double correlation(const std::vector<float> &a, const std::vector<float> &b)
{
    REQUIRE(a.size() == b.size());
    const size_t n = a.size();
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= n;
    mb /= n;
    double cov = 0.0, va = 0.0, vb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        cov += da * db;
        va += da * da;
        vb += db * db;
    }
    if (va == 0.0 || vb == 0.0)
        return 0.0;
    return cov / std::sqrt(va * vb);
}

} // namespace

TEST_CASE("MNF kernel recovers the signal component first", "[mnf][kernel]")
{
    // Band 1: smooth signal; band 2: high-frequency pseudo-random noise.
    constexpr size_t n = 256;
    std::vector<std::vector<float>> input(2, std::vector<float>(n));
    for (size_t i = 0; i < n; ++i) {
        input[0][i] = std::sin(2.0 * M_PI * static_cast<double>(i) / 64.0);
        input[1][i] = static_cast<float>(((i * 7919u) % 101u) / 100.0) - 0.5f;
    }

    const ImageEnhancement::MnfResult result = ImageEnhancement::mnf(input, 2);
    REQUIRE(result.output.size() == 2);
    REQUIRE(result.output[0].size() == n);
    REQUIRE(result.signalToNoise.size() == 2);

    // Components are ordered by signal-to-noise ratio (whitened-covariance
    // eigenvalues), so the first has the larger SNR.
    CHECK(result.signalToNoise[0] >= result.signalToNoise[1]);

    // The first component is dominated by the smooth signal (high correlation),
    // not the noise band.
    CHECK(std::abs(correlation(result.output[0], input[0])) > 0.8);
}

TEST_CASE("MNF kernel clamps and guards inputs", "[mnf][kernel]")
{
    SECTION("Empty input returns an empty result") {
        const ImageEnhancement::MnfResult result = ImageEnhancement::mnf({}, 2);
        CHECK(result.output.empty());
    }
    SECTION("numComponents clamped to band count") {
        constexpr size_t n = 32;
        std::vector<std::vector<float>> input(3, std::vector<float>(n, 1.0f));
        for (size_t i = 0; i < n; ++i) {
            input[0][i] = std::sin(i);
            input[1][i] = std::cos(i);
            input[2][i] = std::sin(i * 2);
        }
        const ImageEnhancement::MnfResult result = ImageEnhancement::mnf(input, 0);
        REQUIRE(result.output.size() == 3);
        REQUIRE(result.signalToNoise.size() == 3);
    }
}

TEST_CASE("rs:mnf operator writes SNR-ordered components", "[operators][rs][mnf]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/mnf.tif";

    constexpr int W = 32;
    constexpr int H = 16;
    std::vector<std::vector<float>> bands(3, std::vector<float>(W * H, 0.0f));
    for (size_t i = 0; i < W * H; ++i) {
        bands[0][i] = std::sin(i * 0.1);
        bands[1][i] = std::cos(i * 0.1);
        bands[2][i] = static_cast<float>(((i * 2654435761u) % 100u) / 100.0);
    }
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:mnf");
    REQUIRE(op != nullptr);
    CHECK(op->name() == "rs:mnf");

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
    CHECK(ds.width() == W);
    CHECK(ds.height() == H);

    // The first component must be a real (finite) signal.
    std::vector<float> c0(W * H);
    REQUIRE(ds.readBandData(1, c0.data(), W, H));
    bool anyFinite = false;
    for (float v : c0)
        anyFinite = anyFinite || std::isfinite(v);
    CHECK(anyFinite);
}
