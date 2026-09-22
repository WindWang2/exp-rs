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
#include <limits>
#include <string>
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

TEST_CASE("SpectralResampling resampleSpectrumGaussian rejects non-monotonic source grids", "[resample][gaussian]")
{
    const float src[] = {0.1f, 0.3f};
    const float badWl[] = {500.0f, 400.0f};
    const float dstWl[] = {450.0f};
    const float dstFwhm[] = {50.0f};
    float out[1] = {0.0f};
    CHECK_FALSE(SpectralResampling::resampleSpectrumGaussian(src, badWl, 2, dstWl, dstFwhm, 1, out));
}

TEST_CASE("SpectralResampling Gaussian SRF propagates source holes like the linear kernel (#1186 item 28)", "[resample][gaussian][nodata]")
{
    // Regression oracle for the #1186 item 28 follow-up: a non-finite source
    // band inside a target band's SRF support (|diff| <= 3.5*FWHM, the same
    // set of bands that contribute weight) must propagate as NaN — never as a
    // confident interpolated value — while a hole OUTSIDE the support must
    // not change the Gaussian aggregate at all (the linear kernel ignores
    // holes outside the bracket the same way).

    SECTION("hole inside the SRF support yields NaN, not the bracket-linear value")
    {
        // Target 450 (FWHM=100): support 450±350 covers 400/500/600, so the
        // NaN at 600 poisons the integration. The linear bracket of 450 is
        // [400,500] — both finite — so the pre-fix fallback silently emitted
        // the bracket value 0.2 instead of NaN.
        const float src[] = {0.1f, 0.3f, std::numeric_limits<float>::quiet_NaN()};
        const float srcWl[] = {400.0f, 500.0f, 600.0f};
        const float dstWl[] = {450.0f};
        const float dstFwhm[] = {100.0f};
        float out[1] = {0.0f};
        REQUIRE(SpectralResampling::resampleSpectrumGaussian(src, srcWl, 3, dstWl, dstFwhm, 1, out));
        CHECK(std::isnan(out[0]));
    }

    SECTION("hole outside the SRF support does not degrade the Gaussian value")
    {
        // Target 520 (FWHM=20): support 520±70 covers only the 500 band, so
        // the Gaussian value is exactly the 500 value (0.9). The NaN at 400
        // sits outside the support; the pre-fix code broke on it, discarded
        // the aggregate and fell back to the linear bracket value
        // 0.9 + 0.2*(0.3-0.9) = 0.78.
        const float srcWl[] = {400.0f, 500.0f, 600.0f, 700.0f};
        const float dstWl[] = {520.0f};
        const float dstFwhm[] = {20.0f};
        float out[1] = {0.0f};

        const float holed[] = {std::numeric_limits<float>::quiet_NaN(), 0.9f, 0.3f, 0.7f};
        REQUIRE(SpectralResampling::resampleSpectrumGaussian(holed, srcWl, 4, dstWl, dstFwhm, 1, out));
        CHECK(out[0] == Approx(0.9f).margin(1e-6f));

        // The hole-free spectrum must give the identical value.
        const float clean[] = {0.1f, 0.9f, 0.3f, 0.7f};
        float cleanOut[1] = {0.0f};
        REQUIRE(SpectralResampling::resampleSpectrumGaussian(clean, srcWl, 4, dstWl, dstFwhm, 1, cleanOut));
        CHECK(cleanOut[0] == Approx(out[0]).margin(1e-6f));
    }
}




TEST_CASE("rs:spectral_resample propagates NoData sentinel instead of interpolating it (#445)", "[operators][rs][resample][nodata]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input_nd.tif";
    const QString outputPath = tmp.path() + "/resampled_nd.tif";

    // 3 source bands; band at 500nm carries the NoData sentinel. Target 450nm
    // interpolates between the 400 and 500 bands -> must be NaN, not (-9999+0.1)/2.
    std::vector<std::vector<float>> bands = {
        {0.1f}, {-9999.0f}, {0.5f},
    };
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 1, 1, bands, gt, "EPSG:32648", &err));
    {
        GDALDatasetH ds = GDALOpen(inputPath.toUtf8().constData(), GA_Update);
        REQUIRE(ds != nullptr);
        GDALSetRasterNoDataValue(GDALGetRasterBand(ds, 2), -9999.0);
        GDALClose(ds);
    }

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
    op->run(params, ctx);

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(outputPath));
    std::vector<float> out(1);
    REQUIRE(ds.readBandData(1, out.data(), 1, 1));
    CHECK(std::isnan(out[0]));
}

TEST_CASE("analyzeResamplingCoverage: closed-form coverage flags", "[resample][coverage]")
{
    using namespace SpectralResampling;
    // Source grid 400/500/600 nm.
    const float srcWl[3] = {400.0f, 500.0f, 600.0f};

    SECTION("linear path (no FWHM) is binary full/none")
    {
        const float dstWl[4] = {450.0f, 550.0f, 650.0f, 300.0f};
        CoverageReport report;
        REQUIRE(analyzeResamplingCoverage(srcWl, 3, dstWl, nullptr, 4, &report));
        REQUIRE(report.bands.size() == 4);
        CHECK(report.bands[0] == BandCoverage::Full);
        CHECK(report.bands[1] == BandCoverage::Full);
        CHECK(report.bands[2] == BandCoverage::None); // 650 > 600: NaN territory
        CHECK(report.bands[3] == BandCoverage::None); // 300 < 400
        CHECK(report.full == 2);
        CHECK(report.partial == 0);
        CHECK(report.none == 2);
    }

    SECTION("gaussian path flags edge-truncated SRFs as partial")
    {
        // 450 nm with FWHM=10: the whole response (±35 nm at the 3.5-FWHM
        // cutoff) sits inside [400,600] → Full.
        // 450 nm with FWHM=100: σ√2 ≈ 60.07, captured mass on [400,600] is
        // 0.5·(erf(2.497) − erf(−0.832)) ≈ 0.880 < 0.99 → Partial.
        // 600 nm (edge center) with FWHM=100: half the response is outside →
        // captured = 0.5 → Partial.
        // 700 nm is outside the range → None regardless of FWHM.
        const float dstWl[4] = {450.0f, 450.0f, 600.0f, 700.0f};
        const float dstFwhm[4] = {10.0f, 100.0f, 100.0f, 100.0f};
        CoverageReport report;
        REQUIRE(analyzeResamplingCoverage(srcWl, 3, dstWl, dstFwhm, 4, &report));
        CHECK(report.bands[0] == BandCoverage::Full);
        CHECK(report.bands[1] == BandCoverage::Partial);
        CHECK(report.bands[2] == BandCoverage::Partial);
        CHECK(report.bands[3] == BandCoverage::None);
        CHECK(report.full == 1);
        CHECK(report.partial == 2);
        CHECK(report.none == 1);
    }

    SECTION("invalid arguments are refused")
    {
        const float dstWl[1] = {450.0f};
        const float badSrc[3] = {500.0f, 400.0f, 600.0f};
        CoverageReport report;
        REQUIRE_FALSE(analyzeResamplingCoverage(nullptr, 3, dstWl, nullptr, 1, &report));
        REQUIRE_FALSE(analyzeResamplingCoverage(srcWl, 1, dstWl, nullptr, 1, &report));
        REQUIRE_FALSE(analyzeResamplingCoverage(srcWl, 3, dstWl, nullptr, 0, &report));
        REQUIRE_FALSE(analyzeResamplingCoverage(badSrc, 3, dstWl, nullptr, 1, &report));
        REQUIRE_FALSE(analyzeResamplingCoverage(srcWl, 3, nullptr, nullptr, 1, &report));
        REQUIRE_FALSE(analyzeResamplingCoverage(srcWl, 3, dstWl, nullptr, 1, nullptr));
    }

    SECTION("non-finite and boundary targets")
    {
        const float nanWl = std::numeric_limits<float>::quiet_NaN();
        const float dstWl[3] = {400.0f, nanWl, 600.0f}; // boundary centers are in-range
        CoverageReport report;
        REQUIRE(analyzeResamplingCoverage(srcWl, 3, dstWl, nullptr, 3, &report));
        CHECK(report.bands[0] == BandCoverage::Full);
        CHECK(report.bands[1] == BandCoverage::None);
        CHECK(report.bands[2] == BandCoverage::Full);
    }

    SECTION("coverage text is stable")
    {
        CHECK(std::string(bandCoverageText(BandCoverage::Full)) == "full");
        CHECK(std::string(bandCoverageText(BandCoverage::Partial)) == "partial");
        CHECK(std::string(bandCoverageText(BandCoverage::None)) == "none");
    }
}

TEST_CASE("rs:spectral_resample reports coverage in the result", "[operators][rs][resample][coverage]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";
    const QString outputPath = tmp.path() + "/resampled.tif";

    constexpr int W = 1;
    constexpr int H = 1;
    std::vector<std::vector<float>> bands = {
        {0.1f}, {0.3f}, {0.5f},
    };
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, W, H, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:spectral_resample");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    Json::Value sources(Json::arrayValue);
    sources.append(400.0);
    sources.append(500.0);
    sources.append(600.0);
    params["sourceWavelengths"] = sources;
    Json::Value targets(Json::arrayValue);
    targets.append(450.0);  // inside
    targets.append(650.0);  // outside → NaN output
    targets.append(550.0);  // inside
    params["wavelengths"] = targets;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    REQUIRE(result["coverageFull"].asInt() == 2);
    REQUIRE(result["coverageNone"].asInt() == 1);
    REQUIRE(result["coveragePartial"].asInt() == 0);
    REQUIRE(result["coverage"].size() == 3);
    CHECK(std::string(result["coverage"][0].asCString()) == "full");
    CHECK(std::string(result["coverage"][1].asCString()) == "none");
    CHECK(std::string(result["coverage"][2].asCString()) == "full");
}
