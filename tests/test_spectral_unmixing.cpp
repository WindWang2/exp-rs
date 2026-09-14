// test_spectral_unmixing.cpp — linear spectral unmixing kernel + operator
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include <array>
#include <cmath>
#include <limits>
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

// ── Fully constrained least squares (Hyperspectral Platform 10.0) ─────────

TEST_CASE("FCLS recovers exact abundances for simplex mixtures", "[unmixing][fcls]")
{
    // Three-endmember simplex (matches the PPI fixture geometry).
    const std::vector<float> endmembers = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    // Pure pixels must recover the vertex exactly under the constraints.
    std::vector<float> pixels = endmembers;

    SpectralUnmixing::UnmixResult result;
    QString err;
    REQUIRE(SpectralUnmixing::unmixFcls(pixels.data(), 3, 3, endmembers.data(), 3,
                                        &result, &err));
    REQUIRE(result.abundances.size() == 9);

    // Pure pixel 0 -> [1, 0, 0], pixel 1 -> [0, 1, 0], pixel 2 -> [0, 0, 1].
    CHECK(result.abundances[0] == Approx(1.0f).margin(1e-4f));
    CHECK(result.abundances[1] == Approx(0.0f).margin(1e-4f));
    CHECK(result.abundances[2] == Approx(0.0f).margin(1e-4f));
    CHECK(result.abundances[4] == Approx(1.0f).margin(1e-4f));
    CHECK(result.abundances[8] == Approx(1.0f).margin(1e-4f));

    // Mixture pixel: 0.2 E1 + 0.3 E2 + 0.5 E3.
    std::vector<float> mixture(3);
    for (int b = 0; b < 3; ++b)
        mixture[b] = 0.2f * endmembers[b] + 0.3f * endmembers[3 + b]
                     + 0.5f * endmembers[6 + b];
    REQUIRE(SpectralUnmixing::unmixFcls(mixture.data(), 1, 3, endmembers.data(), 3,
                                        &result, &err));
    CHECK(result.abundances[0] == Approx(0.2f).margin(1e-3f));
    CHECK(result.abundances[1] == Approx(0.3f).margin(1e-3f));
    CHECK(result.abundances[2] == Approx(0.5f).margin(1e-3f));
    // Sum-to-one enforced to the documented penalty accuracy.
    CHECK(result.abundances[0] + result.abundances[1] + result.abundances[2]
          == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("FCLS sums to one where OLS renormalization alone cannot", "[unmixing][fcls]")
{
    // A pixel off the data simplex: unconstrained LS wants a negative
    // abundance; FCLS must clamp to the constrained optimum and still satisfy
    // the sum constraint to the stated accuracy.
    const std::vector<float> endmembers = {
        0.6f, 0.2f, 0.2f,
        0.1f, 0.4f, 0.5f,
    };
    std::vector<float> pixel = { -0.1f, 0.3f, 0.35f }; // off-simplex
    std::vector<float> pixels = pixel;

    SpectralUnmixing::UnmixResult ols;
    QString err;
    REQUIRE(SpectralUnmixing::unmix(pixels.data(), 1, 3, endmembers.data(), 2, &ols, &err));
    SpectralUnmixing::UnmixResult fcls;
    REQUIRE(SpectralUnmixing::unmixFcls(pixels.data(), 1, 3, endmembers.data(), 2, &fcls, &err));

    for (int e = 0; e < 2; ++e)
        CHECK(fcls.abundances[static_cast<size_t>(e)] >= -1e-6f);
    CHECK(fcls.abundances[0] + fcls.abundances[1] == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("FCLS refuses collinear and zero endmembers", "[unmixing][fcls]")
{
    QString err;

    SECTION("collinear endmembers")
    {
        // E2 = 2 * E1: rank-deficient simplex.
        const std::vector<float> collinear = {
            0.5f, 0.3f, 0.2f,
            1.0f, 0.6f, 0.4f,
        };
        std::vector<float> pixel = { 0.4f, 0.3f, 0.3f };
        SpectralUnmixing::UnmixResult result;
        REQUIRE_FALSE(SpectralUnmixing::unmixFcls(pixel.data(), 1, 3, collinear.data(), 2,
                                                  &result, &err));
        CHECK(err.contains("rank-deficient"));
    }
    SECTION("zero endmember")
    {
        const std::vector<float> zeroEm = {
            0.5f, 0.3f, 0.2f,
            0.0f, 0.0f, 0.0f,
        };
        std::vector<float> pixel = { 0.4f, 0.3f, 0.3f };
        SpectralUnmixing::UnmixResult result;
        REQUIRE_FALSE(SpectralUnmixing::unmixFcls(pixel.data(), 1, 3, zeroEm.data(), 2,
                                                  &result, &err));
        CHECK(err.contains("zero vector"));
    }
    SECTION("near-collinear pairs survive the guard when numerically distinct")
    {
        // 2% off collinearity: a legal simplex, must not refuse.
        const std::vector<float> nearCollinear = {
            0.50f, 0.30f, 0.20f,
            0.51f, 0.31f, 0.20f,
        };
        std::vector<float> pixel = { 0.40f, 0.30f, 0.25f };
        SpectralUnmixing::UnmixResult result;
        REQUIRE(SpectralUnmixing::unmixFcls(pixel.data(), 1, 3, nearCollinear.data(), 2,
                                            &result, &err));
    }
}

TEST_CASE("FCLS produces NaN abundances for NaN pixels", "[unmixing][fcls]")
{
    const std::vector<float> endmembers = {
        0.6f, 0.2f, 0.2f,
        0.1f, 0.4f, 0.5f,
    };
    std::vector<float> pixels = {
        0.3f, 0.3f, 0.35f,
        std::numeric_limits<float>::quiet_NaN(), 0.3f, 0.35f,
    };
    SpectralUnmixing::UnmixResult result;
    QString err;
    REQUIRE(SpectralUnmixing::unmixFcls(pixels.data(), 2, 3, endmembers.data(), 2,
                                        &result, &err));
    CHECK(std::isfinite(result.abundances[0]));
    CHECK(std::isnan(result.abundances[2]));
    CHECK(std::isnan(result.reconstructionError[1]));
}

TEST_CASE("FCLS on a brightness-off-simplex pixel equals the analytic constrained optimum",
          "[unmixing][fcls][known-answer]")
{
    // Hand-derived known answer (validation-policy §1: reviewer-derivable).
    //
    // Endmembers e1=[1,0,0], e2=[0,1,0]; pixel x=[2,1,0].
    // Hard-constrained optimum: minimize (2-a1)^2 + (1-a2)^2 s.t.
    // a1+a2=1, a>=0. Lagrange: a1 = 2 - L/2, a2 = 1 - L/2, sum ->
    // 3 - L = 1 -> L = 2 -> a = [1, 0].
    //
    // The degenerate "OLS then clip to [0,1] and renormalize" method this
    // solver replaces gives a_OLS = [2,1] -> [2/3, 1/3], so this assertion
    // fails for the degenerate implementation and pins the real one.
    const std::vector<float> endmembers = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    std::vector<float> pixel = { 2.0f, 1.0f, 0.0f };

    SpectralUnmixing::UnmixResult fcls;
    QString err;
    REQUIRE(SpectralUnmixing::unmixFcls(pixel.data(), 1, 3, endmembers.data(), 2,
                                        &fcls, &err));
    CHECK(fcls.abundances[0] == Approx(1.0f).margin(1e-3f));
    CHECK(fcls.abundances[1] == Approx(0.0f).margin(1e-3f));
}
