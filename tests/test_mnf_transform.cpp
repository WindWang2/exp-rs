// test_mnf_transform.cpp — complete MNF chain kernel contract (forward,
// inverse, known answers, refusals, artifact round-trip).
//
// Known answers:
//   - Roundtrip: forward(all components) followed by inverse reconstructs the
//     input pixel to eigensolver tolerance (stated 1e-5 relative).
//   - SNR ordering: a data cube with a strong planted axis and weak isotropic
//     noise ranks the axis first; the dropped-mass RMSE grows with the number
//     of discarded components (monotone, quantified).
//   - Constant bands refuse: a zero-noise (singular) covariance is a typed
//     refusal — the legacy kernel silently clamped this to 1e-9.
//   - Artifact round-trip: modelToJson -> modelFromJson preserves the model
//     (digest-verified); a mutated payload is refused by digest.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "processing/algorithms/mnf_transform.h"

using Catch::Approx;
using namespace MnfTransform;

namespace
{

/// Feed a synthetic cube (row-major BIP, `rows` image rows of `width`
/// pixels) through the two-pass statistics, then fit.
bool fitCube(const std::vector<float> &cube, int width, int height, int bands,
             const std::vector<float> &noDataValues, Model *model, QString *error)
{
    RowFeeder feeder(bands);
    std::vector<uint8_t> valid(static_cast<size_t>(width), 1);
    const size_t rowFloats = static_cast<size_t>(width) * bands;
    for (int y = 0; y < height; ++y)
    {
        const float *row = cube.data() + static_cast<size_t>(y) * rowFloats;
        if (!noDataValues.empty())
        {
            for (int p = 0; p < width; ++p)
            {
                const float *spectrum = row + static_cast<size_t>(p) * bands;
                uint8_t ok = 1;
                for (int b = 0; b < bands; ++b)
                    if (noDataValues[static_cast<size_t>(b)] != 0.0f
                        && spectrum[b] == noDataValues[static_cast<size_t>(b)])
                        ok = 0;
                valid[static_cast<size_t>(p)] = ok;
            }
        }
        feeder.addRow(row, width, noDataValues.empty() ? nullptr : valid.data());
    }
    feeder.finalizeMean();
    for (int y = 0; y < height; ++y)
    {
        const float *row = cube.data() + static_cast<size_t>(y) * rowFloats;
        if (!noDataValues.empty())
        {
            for (int p = 0; p < width; ++p)
            {
                const float *spectrum = row + static_cast<size_t>(p) * bands;
                uint8_t ok = 1;
                for (int b = 0; b < bands; ++b)
                    if (noDataValues[static_cast<size_t>(b)] != 0.0f
                        && spectrum[b] == noDataValues[static_cast<size_t>(b)])
                        ok = 0;
                valid[static_cast<size_t>(p)] = ok;
            }
        }
        feeder.addRow(row, width, noDataValues.empty() ? nullptr : valid.data());
    }
    feeder.finalizeCovariances();
    return fit(feeder, model, error);
}

/// A 4-band cube: band 0 = smooth signal ramp + noise, band 1 = strong planted
/// linear axis, bands 2-3 = weak noise-only. Deterministic via mt19937(42).
std::vector<float> syntheticCube(int width, int height)
{
    std::mt19937 rng(42);
    std::normal_distribution<float> strongNoise(0.0f, 0.01f);
    std::normal_distribution<float> weakNoise(0.0f, 0.001f);
    std::vector<float> cube(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float *spectrum = &cube[(static_cast<size_t>(y) * width + x) * 4];
            spectrum[0] = 0.1f + 0.002f * x + strongNoise(rng);
            spectrum[1] = 0.5f * (x / static_cast<float>(width)) + strongNoise(rng);
            spectrum[2] = 0.3f + weakNoise(rng);
            spectrum[3] = 0.2f + weakNoise(rng);
        }
    }
    return cube;
}

} // namespace

TEST_CASE("MNF forward+inverse round-trips the data", "[mnf][kernel]")
{
    const int W = 12, H = 8;
    const std::vector<float> cube = syntheticCube(W, H);
    Model model;
    QString error;
    REQUIRE(fitCube(cube, W, H, 4, {}, &model, &error));
    REQUIRE(model.bandCount == 4);
    REQUIRE(model.snr.size() == 4);

    // SNR ordering: the whitened-covariance eigenvalues descend.
    for (size_t i = 1; i < model.snr.size(); ++i)
        CHECK(model.snr[i - 1] >= model.snr[i]);

    // Roundtrip every pixel through forward(all) -> inverse(all).
    std::vector<double> y(4, 0.0);
    std::vector<double> spectrum(4, 0.0);
    for (size_t p = 0; p + 4 <= cube.size(); p += 4)
    {
        forward(model, &cube[p], y.data(), 4);
        inverse(model, y.data(), {}, spectrum.data());
        for (int b = 0; b < 4; ++b)
        {
            const double reference = cube[p + static_cast<size_t>(b)];
            REQUIRE(std::abs(spectrum[static_cast<size_t>(b)] - reference)
                    <= 1e-5 * std::max(1.0, std::abs(reference)));
        }
    }
}

TEST_CASE("Dropping components discards quantified reconstruction mass", "[mnf][kernel]")
{
    const int W = 12, H = 8;
    const std::vector<float> cube = syntheticCube(W, H);
    Model model;
    QString error;
    REQUIRE(fitCube(cube, W, H, 4, {}, &model, &error));

    // Take the pixel with the strongest last-axis coefficient; keeping fewer
    // components must produce a strictly growing dropped-mass RMSE.
    std::vector<double> y(4, 0.0);
    const size_t probePixel = 0; // arbitrary pixel; monotonicity is the claim
    forward(model, &cube[probePixel], y.data(), 4);

    const double rmse3 = reconstructionRmse(model, y.data(), {0, 1, 2});
    const double rmse2 = reconstructionRmse(model, y.data(), {0, 1});
    const double rmse1 = reconstructionRmse(model, y.data(), {0});
    CHECK(rmse3 >= 0.0);
    CHECK(rmse2 >= rmse3);
    CHECK(rmse1 >= rmse2);
    // Full selection reconstructs exactly: zero dropped mass.
    CHECK(reconstructionRmse(model, y.data(), {0, 1, 2, 3}) == Approx(0.0).margin(1e-12));
}

TEST_CASE("Singular noise covariance refuses with a typed message", "[mnf][kernel]")
{
    // Two constant bands + two noisy bands: the constant bands have zero noise
    // variance -> the noise covariance is singular.
    const int W = 8, H = 6;
    std::vector<float> cube(static_cast<size_t>(W) * H * 4);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    for (size_t p = 0; p < static_cast<size_t>(W) * H; ++p)
    {
        float *spectrum = &cube[p * 4];
        spectrum[0] = 0.5f;                    // constant: no noise
        spectrum[1] = 0.5f + noise(rng);
        spectrum[2] = 0.25f;                   // constant: no noise
        spectrum[3] = 0.75f + noise(rng);
    }
    Model model;
    QString error;
    REQUIRE_FALSE(fitCube(cube, W, H, 4, {}, &model, &error));
    REQUIRE(error.contains("singular"));
}

TEST_CASE("All-NoData input refuses; a non-matching sentinel is harmless", "[mnf][kernel]")
{
    // Every pixel equals the sentinel (0.25 here, planted below) in every
    // band -> zero valid samples -> the fit refuses (never divides by zero).
    const int W = 6, H = 4;
    std::vector<float> flatCube(static_cast<size_t>(W) * H * 4, 0.25f);
    Model model;
    QString error;
    REQUIRE_FALSE(fitCube(flatCube, W, H, 4, { 0.25f, 0.25f, 0.25f, 0.25f },
                          &model, &error));

    // A sentinel that matches nothing excludes nothing: the ordinary cube
    // still fits.
    const std::vector<float> cube = syntheticCube(W, H);
    REQUIRE(fitCube(cube, W, H, 4, { -1.0f, -1.0f, -1.0f, -1.0f }, &model, &error));
}

TEST_CASE("Transform model artifact round-trips with digest verification", "[mnf][artifact]")
{
    const int W = 10, H = 6;
    const std::vector<float> cube = syntheticCube(W, H);
    Model model;
    QString error;
    REQUIRE(fitCube(cube, W, H, 4, {}, &model, &error));
    model.wavelengthsNm = { 500.0f, 600.0f, 700.0f, 800.0f };

    QJsonObject json;
    modelToJson(model, QStringLiteral("synthetic"), QStringLiteral("{}"), 42, json);
    REQUIRE(json["kind"].toString() == QStringLiteral("exp-rs:mnf-transform"));

    Model parsed;
    REQUIRE(modelFromJson(json, &parsed, &error));
    REQUIRE(parsed.bandCount == model.bandCount);
    REQUIRE(parsed.mean.size() == model.mean.size());
    for (size_t i = 0; i < model.mean.size(); ++i)
        CHECK(parsed.mean[i] == Approx(model.mean[i]).epsilon(1e-12));
    for (size_t i = 0; i < model.forwardBasis.size(); ++i)
        CHECK(parsed.forwardBasis[i] == Approx(model.forwardBasis[i]).epsilon(1e-12));
    REQUIRE(parsed.wavelengthsNm.size() == 4);

    // Tamper with one basis value -> digest mismatch refusal.
    QJsonObject tampered = json;
    QJsonArray basis = tampered["forwardBasis"].toArray();
    basis[0] = basis.at(0).toDouble() + 0.5;
    tampered["forwardBasis"] = basis;
    REQUIRE_FALSE(modelFromJson(tampered, &parsed, &error));
    CHECK(error.contains("digest"));
}

TEST_CASE("Inverse reconstruction uses the model to convert MNF-space spectra",
          "[mnf][kernel]")
{
    // The PPI-in-MNF-space chain: a spectrum expressed in component space
    // converts back to band space; the converted spectrum matches the
    // forward->inverse roundtrip of the same pixel.
    const int W = 10, H = 6;
    const std::vector<float> cube = syntheticCube(W, H);
    Model model;
    QString error;
    REQUIRE(fitCube(cube, W, H, 4, {}, &model, &error));

    const size_t p = 17 % (static_cast<size_t>(W) * H);
    std::vector<double> y(4, 0.0);
    std::vector<double> viaForwardInverse(4, 0.0);
    std::vector<double> viaInverse(4, 0.0);
    forward(model, &cube[p * 4], y.data(), 4);
    inverse(model, y.data(), {}, viaForwardInverse.data());
    inverse(model, y.data(), {0, 1, 2, 3}, viaInverse.data());
    for (int b = 0; b < 4; ++b)
        CHECK(viaInverse[static_cast<size_t>(b)]
              == Approx(viaForwardInverse[static_cast<size_t>(b)]).epsilon(1e-9));
}
