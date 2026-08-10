// test_spectral_resampling.cpp — spectral resampling kernel + operator
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include <gdal.h>

#include <array>
#include <cmath>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/spectral_resampling.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_resampling";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

} // namespace

TEST_CASE("Spectral resampling interpolates linearly between band centers", "[resample][kernel]")
{
    const float src[] = {0.1f, 0.3f, 0.5f};
    const float srcWl[] = {400.0f, 500.0f, 600.0f};

    SECTION("Midpoint interpolation") {
        const float dstWl[] = {450.0f};
        float out[1] = {0.0f};
        REQUIRE(SpectralResampling::resampleSpectrum(src, srcWl, 3, dstWl, 1, out));
        CHECK(out[0] == Approx(0.2f).margin(1e-6f));
    }
    SECTION("Interior fraction") {
        const float dstWl[] = {525.0f};
        float out[1] = {0.0f};
        REQUIRE(SpectralResampling::resampleSpectrum(src, srcWl, 3, dstWl, 1, out));
        CHECK(out[0] == Approx(0.35f).margin(1e-6f));
    }
    SECTION("Exact band center") {
        const float dstWl[] = {400.0f, 600.0f};
        float out[2] = {0.0f, 0.0f};
        REQUIRE(SpectralResampling::resampleSpectrum(src, srcWl, 3, dstWl, 2, out));
        CHECK(out[0] == Approx(0.1f).margin(1e-6f));
        CHECK(out[1] == Approx(0.5f).margin(1e-6f));
    }
    SECTION("Out-of-range targets are NaN") {
        const float dstWl[] = {350.0f, 650.0f};
        float out[2] = {0.0f, 0.0f};
        REQUIRE(SpectralResampling::resampleSpectrum(src, srcWl, 3, dstWl, 2, out));
        CHECK(std::isnan(out[0]));
        CHECK(std::isnan(out[1]));
    }
}

TEST_CASE("Spectral resampling guards invalid arguments", "[resample][kernel]")
{
    const float src[] = {0.1f, 0.3f};
    const float srcWl[] = {400.0f, 500.0f};
    const float dstWl[] = {450.0f};
    float out[1] = {0.0f};

    CHECK_FALSE(SpectralResampling::resampleSpectrum(nullptr, srcWl, 2, dstWl, 1, out));
    CHECK_FALSE(SpectralResampling::resampleSpectrum(src, srcWl, 1, dstWl, 1, out));
    CHECK_FALSE(SpectralResampling::resampleSpectrum(src, srcWl, 2, dstWl, 0, out));
    // Non-increasing source wavelengths are rejected.
    const float badWl[] = {500.0f, 400.0f};
    CHECK_FALSE(SpectralResampling::resampleSpectrum(src, badWl, 2, dstWl, 1, out));
}

TEST_CASE("rs:spectral_resample resamples using band WAVELENGTH metadata", "[operators][rs][resample]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/resampled.tif";

    constexpr int W = 2;
    constexpr int H = 1;
    // 3 bands at 400/500/600 nm with constant per-band values.
    std::vector<std::vector<float>> bands = {
        {0.1f, 0.1f}, {0.3f, 0.3f}, {0.5f, 0.5f},
    };
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));

    // Stamp WAVELENGTH metadata like a product-stacked raster (ADR 0065).
    {
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        const char* wl[] = {"400", "500", "600"};
        for (int b = 0; b < 3; ++b)
            GDALSetMetadataItem(GDALGetRasterBand(ds, b + 1), "WAVELENGTH", wl[b], nullptr);
        GDALClose(ds);
    }

    auto op = RSOperatorRegistry::instance().create("rs:spectral_resample");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    Json::Value targets(Json::arrayValue);
    targets.append(450.0);
    targets.append(550.0);
    params["wavelengths"] = targets;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    CHECK(result["output"].asString() == outputPath.toStdString());
    CHECK(result["bands"].asInt() == 2);
    REQUIRE(result["sourceWavelengths"].isArray());
    CHECK(result["sourceWavelengths"].size() == 3);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    CHECK(ds.bandCount() == 2);
    std::vector<float> b1(W * H), b2(W * H);
    REQUIRE(ds.readBandData(1, b1.data(), W, H));
    REQUIRE(ds.readBandData(2, b2.data(), W, H));
    CHECK(b1[0] == Approx(0.2f).margin(1e-3f)); // 450 nm midpoint
    CHECK(b2[0] == Approx(0.4f).margin(1e-3f)); // 550 nm midpoint
}

TEST_CASE("rs:spectral_resample accepts explicit source wavelengths", "[operators][rs][resample]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/resampled.tif";

    std::vector<std::vector<float>> bands = {
        {0.1f}, {0.3f}, {0.5f},
    };
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 1, 1, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:spectral_resample");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    Json::Value targets(Json::arrayValue);
    targets.append(450.0);
    params["wavelengths"] = targets;
    Json::Value sources(Json::arrayValue);
    sources.append(400.0);
    sources.append(500.0);
    sources.append(600.0);
    params["sourceWavelengths"] = sources;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);
    CHECK(QFile::exists(outputPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> b1(1);
    REQUIRE(ds.readBandData(1, b1.data(), 1, 1));
    CHECK(b1[0] == Approx(0.2f).margin(1e-3f));
}

TEST_CASE("SpectralResampling resampleSpectrumGaussian uses FWHM Gaussian weighting", "[resample][gaussian]")
{
    const float src[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const float srcWl[] = {400.0f, 450.0f, 500.0f, 550.0f, 600.0f};

    const float dstWl[] = {500.0f, 525.0f};
    const float dstFwhm[] = {50.0f, 50.0f};
    float out[2] = {0.0f, 0.0f};

    REQUIRE(SpectralResampling::resampleSpectrumGaussian(src, srcWl, 5, dstWl, dstFwhm, 2, out));

    // Peak at 500nm should have highest weight
    CHECK(out[0] > out[1]);
    CHECK(out[0] > 0.5f);
}

