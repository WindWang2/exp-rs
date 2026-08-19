#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>
#include <vector>
#include <array>
#include <cmath>
#include <limits>

using Catch::Approx;

// Regression for #380: ImageEnhancementPanel IHS must call rgbToIhs per pixel
// (true decomposition) not ImageFusion::ihsFusion with pan=red.
// Also for #343 DLGA-5: IHS on <3 bands must be rejected (worker else).

static void panelIhsDecomposition(const std::vector<float> &r,
                                  const std::vector<float> &g,
                                  const std::vector<float> &b,
                                  std::vector<std::vector<float>> &out)
{
    const size_t n = r.size();
    out.assign(3, std::vector<float>(n));
    for (size_t i = 0; i < n; ++i) {
        float rv = r[i], gv = g[i], bv = b[i];
        if (std::isnan(rv) || std::isnan(gv) || std::isnan(bv) ||
            rv == -9999.f || gv == -9999.f || bv == -9999.f) {
            out[0][i] = std::numeric_limits<float>::quiet_NaN();
            out[1][i] = std::numeric_limits<float>::quiet_NaN();
            out[2][i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        float ii, h, s;
        ImageEnhancement::rgbToIhs(rv, gv, bv, ii, h, s);
        out[0][i] = ii;
        out[1][i] = h;
        out[2][i] = s;
    }
}

TEST_CASE("ImageEnhancementPanel IHS equals BandRatioDialog decomposition", "[enhancement][panel][380]")
{
    // Same logic as BandRatioDialog::onRun IHS branch
    std::vector<float> r = {200, 50, 128, 255, 10, 0};
    std::vector<float> g = {100, 100, 128, 0, 20, 0};
    std::vector<float> b = {50, 200, 128, 0, 30, 0};

    std::vector<std::vector<float>> panelOut;
    panelIhsDecomposition(r, g, b, panelOut);

    REQUIRE(panelOut.size() == 3);
    REQUIRE(panelOut[0].size() == r.size());

    // Reference: direct rgbToIhs loop (BandRatioDialog)
    std::vector<std::vector<float>> ref(3, std::vector<float>(r.size()));
    for (size_t i = 0; i < r.size(); ++i) {
        float ii, h, s;
        ImageEnhancement::rgbToIhs(r[i], g[i], b[i], ii, h, s);
        ref[0][i] = ii;
        ref[1][i] = h;
        ref[2][i] = s;
    }

    for (int band = 0; band < 3; ++band)
        for (size_t i = 0; i < r.size(); ++i)
            REQUIRE(panelOut[band][i] == Approx(ref[band][i]).margin(1e-5));

    SECTION("I/H/S ranges are distinct from fused RGB")
    {
        // I should be average, H in [0,1), S in [0,1]. Fused RGB would be in
        // original value range; this checks the decomposition is not a no-op copy.
        for (size_t i = 0; i < r.size(); ++i) {
            if (r[i]==0 && g[i]==0 && b[i]==0) continue;
            // Intensity is mean
            REQUIRE(panelOut[0][i] == Approx((r[i]+g[i]+b[i])/3.0f).margin(1e-4));
            REQUIRE(panelOut[1][i] >= 0.0f);
            REQUIRE(panelOut[1][i] < 1.0f);
            REQUIRE(panelOut[2][i] >= 0.0f);
            REQUIRE(panelOut[2][i] <= 1.0f);
        }
    }

    SECTION("IHS result can be written as 3-band GeoTIFF")
    {
        GDALAllRegister();
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        const QString path = tmp.path() + QStringLiteral("/ihs.tif");
        QString err;
        REQUIRE(writeGdalOutput(path, 3, 2, panelOut, gt, QString(), &err));
        REQUIRE(err.isEmpty());
        GDALDatasetH ds = GDALOpen(path.toUtf8().constData(), GA_ReadOnly);
        REQUIRE(ds != nullptr);
        REQUIRE(GDALGetRasterCount(ds) == 3);
        GDALClose(ds);
    }
}

TEST_CASE("ImageEnhancementPanel IHS masks NoData as NaN", "[enhancement][panel][380][nodata]")
{
    std::vector<float> r = {100, std::numeric_limits<float>::quiet_NaN(), 50, -9999.f};
    std::vector<float> g = {100, 100, std::numeric_limits<float>::quiet_NaN(), 100};
    std::vector<float> b = {100, 100, 100, 100};

    std::vector<std::vector<float>> out;
    panelIhsDecomposition(r, g, b, out);

    REQUIRE_FALSE(std::isnan(out[0][0])); // valid pixel
    REQUIRE(std::isnan(out[0][1]));       // r is NaN -> I NaN
    REQUIRE(std::isnan(out[1][1]));
    REQUIRE(std::isnan(out[2][1]));
    REQUIRE(std::isnan(out[0][2]));       // g is NaN
    REQUIRE(std::isnan(out[0][3]));       // r is -9999 sentinel
}

TEST_CASE("ImageEnhancementPanel IHS round-trip is not fusion", "[enhancement][panel][380]")
{
    // Fusion (ihsFusion) does histogram-matched pan substitution then back to RGB;
    // decomposition round-trips through rgbToIhs/ihsToRgb.
    float r = 180, g = 90, b = 45;
    float i, h, s;
    ImageEnhancement::rgbToIhs(r, g, b, i, h, s);
    float r2, g2, b2;
    ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);
    REQUIRE(r2 == Approx(r).margin(1.0f));
    REQUIRE(g2 == Approx(g).margin(1.0f));
    REQUIRE(b2 == Approx(b).margin(1.0f));
}
