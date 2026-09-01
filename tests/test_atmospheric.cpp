// tests/test_atmospheric.cpp — TDD Red phase for DOS atmospheric correction
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/atmospheric_correction.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

// --- DN to Radiance ---

TEST_CASE("dnToRadiance converts DN to radiance", "[atm][dos]")
{
    // L = gain * DN + bias
    std::vector<float> dn = {100.0f, 200.0f, 50.0f};
    std::vector<float> out(3);

    bool ok = AtmosphericCorrection::dnToRadiance(dn.data(), out.data(), 3, 0.01f, -0.1f);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.01f * 100.0f - 0.1f, 0.001f)); // 0.9
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.01f * 200.0f - 0.1f, 0.001f)); // 1.9
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.01f * 50.0f - 0.1f, 0.001f));  // 0.4
}

TEST_CASE("dnToRadiance rejects null pointers", "[atm][dos]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dnToRadiance(nullptr, buf.data(), 3, 1.0f, 0.0f));
    REQUIRE_FALSE(AtmosphericCorrection::dnToRadiance(buf.data(), nullptr, 3, 1.0f, 0.0f));
}

TEST_CASE("dnToRadiance rejects zero count", "[atm][dos]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dnToRadiance(buf.data(), buf.data(), 0, 1.0f, 0.0f));
}

// --- DOS1 ---

TEST_CASE("DOS1 subtracts minimum radiance", "[atm][dos1]")
{
    // DN values: 50, 100, 200, 80
    // gain=0.01, bias=0 → radiance: 0.5, 1.0, 2.0, 0.8
    // min radiance = 0.5
    // surface = radiance - 0.5 → 0.0, 0.5, 1.5, 0.3
    std::vector<float> dn = {50.0f, 100.0f, 200.0f, 80.0f};
    std::vector<float> out(4);

    bool ok = AtmosphericCorrection::dos1(dn.data(), out.data(), 4, 0.01f, 0.0f);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(1.5f, 0.001f));
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(0.3f, 0.001f));
}

TEST_CASE("DOS1 handles all-same values (min=max)", "[atm][dos1]")
{
    // All same DN → all same radiance → subtract min → all zeros
    std::vector<float> dn = {100.0f, 100.0f, 100.0f};
    std::vector<float> out(3);

    AtmosphericCorrection::dos1(dn.data(), out.data(), 3, 0.01f, 0.0f);
    for (int i = 0; i < 3; i++)
        REQUIRE_THAT(out[i], Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

TEST_CASE("DOS1 minimum pixel becomes zero", "[atm][dos1]")
{
    // The darkest pixel should become exactly 0 after DOS1
    std::vector<float> dn = {30.0f, 150.0f, 80.0f, 200.0f, 45.0f};
    std::vector<float> out(5);

    AtmosphericCorrection::dos1(dn.data(), out.data(), 5, 0.005f, 0.02f);

    // Find min output — should be 0 (the pixel with DN=30)
    float minVal = *std::min_element(out.begin(), out.end());
    REQUIRE_THAT(minVal, Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

TEST_CASE("DOS1 with bias offset", "[atm][dos1]")
{
    // gain=0.01, bias=-0.5
    // DN: 100, 200 → radiance: 0.5, 1.5
    // min=0.5 → surface: 0.0, 1.0
    std::vector<float> dn = {100.0f, 200.0f};
    std::vector<float> out(2);

    AtmosphericCorrection::dos1(dn.data(), out.data(), 2, 0.01f, -0.5f);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("DOS1 rejects null pointers", "[atm][dos1]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dos1(nullptr, buf.data(), 3, 1.0f, 0.0f));
    REQUIRE_FALSE(AtmosphericCorrection::dos1(buf.data(), nullptr, 3, 1.0f, 0.0f));
}

TEST_CASE("DOS1 rejects zero count", "[atm][dos1]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dos1(buf.data(), buf.data(), 0, 1.0f, 0.0f));
}

// --- Histogram-based dark object extraction (DOS1, Chavez 1996) ---

TEST_CASE("findDarkObjectByHistogram ignores isolated noisy dark pixel", "[atm][dos1][histogram]")
{
    // Scene floor at 8.0 radiance; one single-pixel sensor-noise spike at 2.0.
    std::vector<float> radiance(10000, 8.0f);
    radiance[0] = 2.0f;
    const float dark = AtmosphericCorrection::findDarkObjectByHistogram(radiance.data(), radiance.size());
    // The 0.01% frequency floor must reject the spike and pick the scene floor.
    REQUIRE_THAT(dark, Catch::Matchers::WithinAbs(8.0f, 0.1f));
}

TEST_CASE("findDarkObjectByHistogram picks lowest dense dark cluster", "[atm][dos1][histogram]")
{
    std::vector<float> radiance(10000, 12.0f);
    radiance.insert(radiance.begin(), 500, 4.0f); // dark cluster at 4.0
    const float dark = AtmosphericCorrection::findDarkObjectByHistogram(radiance.data(), radiance.size());
    REQUIRE_THAT(dark, Catch::Matchers::WithinAbs(4.0f, 0.1f));
}

TEST_CASE("findDarkObjectByHistogram single-level scene returns that level", "[atm][dos1][histogram]")
{
    std::vector<float> radiance(100, 7.25f);
    REQUIRE_THAT(AtmosphericCorrection::findDarkObjectByHistogram(radiance.data(), radiance.size()),
                 Catch::Matchers::WithinAbs(7.25f, 1e-4f));
}

TEST_CASE("findDarkObjectByHistogram tiny scene falls back to global min", "[atm][dos1][histogram]")
{
    // Fewer pixels than the frequency floor: fall back to the scene minimum.
    std::vector<float> radiance = {3.0f, 5.0f, 9.0f};
    REQUIRE_THAT(AtmosphericCorrection::findDarkObjectByHistogram(radiance.data(), radiance.size()),
                 Catch::Matchers::WithinAbs(3.0f, 1e-4f));
}

TEST_CASE("findDarkObjectByHistogram skips NaN and handles all-NaN", "[atm][dos1][histogram]")
{
    std::vector<float> radiance = {std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::quiet_NaN()};
    REQUIRE(AtmosphericCorrection::findDarkObjectByHistogram(radiance.data(), radiance.size()) == 0.0f);

    std::vector<float> mixed = {std::numeric_limits<float>::quiet_NaN(), 2.0f, 2.0f, 2.0f, 2.0f};
    REQUIRE_THAT(AtmosphericCorrection::findDarkObjectByHistogram(mixed.data(), mixed.size()),
                 Catch::Matchers::WithinAbs(2.0f, 0.1f));
}

TEST_CASE("findDarkObjectByHistogram null or empty input returns zero", "[atm][dos1][histogram]")
{
    REQUIRE(AtmosphericCorrection::findDarkObjectByHistogram(nullptr, 10) == 0.0f);
    std::vector<float> empty;
    REQUIRE(AtmosphericCorrection::findDarkObjectByHistogram(empty.data(), 0) == 0.0f);
}

TEST_CASE("dos1Histogram equals dos1 on a clean scene", "[atm][dos1][histogram]")
{
    // Without outliers the histogram floor coincides with the global minimum.
    std::vector<float> dn = {100.0f, 200.0f, 150.0f, 120.0f, 130.0f, 110.0f, 140.0f, 105.0f};
    std::vector<float> plain(8), hist(8);
    REQUIRE(AtmosphericCorrection::dos1(dn.data(), plain.data(), 8, 0.01f, 0.0f));
    REQUIRE(AtmosphericCorrection::dos1Histogram(dn.data(), hist.data(), 8, 0.01f, 0.0f));
    for (size_t i = 0; i < 8; ++i)
        REQUIRE_THAT(hist[i], Catch::Matchers::WithinAbs(plain[i], 1e-4f));
}

TEST_CASE("dos1Histogram is robust against single noisy dark pixel", "[atm][dos1][histogram]")
{
    // 10000 pixels at DN 100 (radiance 1.0) plus one hot noise pixel at DN 20.
    std::vector<float> dn(10000, 100.0f);
    dn[0] = 20.0f;
    std::vector<float> out(dn.size());
    REQUIRE(AtmosphericCorrection::dos1Histogram(dn.data(), out.data(), dn.size(), 0.01f, 0.0f));
    // Scene floor maps to ~0 instead of being dragged down by the noise pixel.
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.0f, 0.1f));
    // The noise pixel itself drops below zero (below the dark object level).
    REQUIRE(out[0] < -0.5f);
}

TEST_CASE("dos1Histogram rejects null pointers and zero count", "[atm][dos1][histogram]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dos1Histogram(nullptr, buf.data(), 3, 1.0f, 0.0f));
    REQUIRE_FALSE(AtmosphericCorrection::dos1Histogram(buf.data(), nullptr, 3, 1.0f, 0.0f));
    REQUIRE_FALSE(AtmosphericCorrection::dos1Histogram(buf.data(), buf.data(), 0, 1.0f, 0.0f));
}

// --- DOS2 ---

TEST_CASE("DOS2 applies transmittance correction", "[atm][dos2]")
{
    // DOS2: surface = (radiance - path_radiance) / transmittance
    // path_radiance = min_radiance (same as DOS1)
    // With transmittance=0.8:
    // DN: 50, 100, 200 → radiance: 0.5, 1.0, 2.0 (gain=0.01, bias=0)
    // min=0.5, path=0.5
    // surface: (0.5-0.5)/0.8=0, (1.0-0.5)/0.8=0.625, (2.0-0.5)/0.8=1.875
    std::vector<float> dn = {50.0f, 100.0f, 200.0f};
    std::vector<float> out(3);

    bool ok = AtmosphericCorrection::dos2(dn.data(), out.data(), 3, 0.01f, 0.0f, 0.8f);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.625f, 0.001f));
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(1.875f, 0.001f));
}

TEST_CASE("DOS2 with transmittance=1.0 equals DOS1", "[atm][dos2]")
{
    // When transmittance=1.0, DOS2 should produce same result as DOS1
    std::vector<float> dn = {50.0f, 100.0f, 200.0f, 80.0f};
    std::vector<float> dos1_out(4), dos2_out(4);

    AtmosphericCorrection::dos1(dn.data(), dos1_out.data(), 4, 0.01f, 0.0f);
    AtmosphericCorrection::dos2(dn.data(), dos2_out.data(), 4, 0.01f, 0.0f, 1.0f);

    for (int i = 0; i < 4; i++)
        REQUIRE_THAT(dos1_out[i], Catch::Matchers::WithinAbs(dos2_out[i], 0.001f));
}

TEST_CASE("DOS2 rejects invalid transmittance", "[atm][dos2]")
{
    std::vector<float> dn = {50.0f, 100.0f};
    std::vector<float> out(2);

    REQUIRE_FALSE(AtmosphericCorrection::dos2(dn.data(), out.data(), 2, 0.01f, 0.0f, 0.0f));
    REQUIRE_FALSE(AtmosphericCorrection::dos2(dn.data(), out.data(), 2, 0.01f, 0.0f, -0.5f));
}

TEST_CASE("DOS2 rejects null pointers", "[atm][dos2]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dos2(nullptr, buf.data(), 3, 1.0f, 0.0f, 0.8f));
    REQUIRE_FALSE(AtmosphericCorrection::dos2(buf.data(), nullptr, 3, 1.0f, 0.0f, 0.8f));
}

TEST_CASE("DOS2 rejects zero count", "[atm][dos2]")
{
    std::vector<float> buf(3);
    REQUIRE_FALSE(AtmosphericCorrection::dos2(buf.data(), buf.data(), 0, 1.0f, 0.0f, 0.8f));
}

// --- Estimate transmittance ---

TEST_CASE("estimateTransmittance returns valid range", "[atm][dos2]")
{
    // Simple model: T = exp(-0.1 * airmass)
    // For airmass=1.0: T ≈ 0.905
    float T = AtmosphericCorrection::estimateTransmittance(1.0f);
    REQUIRE(T > 0.0f);
    REQUIRE(T <= 1.0f);
    REQUIRE_THAT(T, Catch::Matchers::WithinAbs(0.905f, 0.01f));
}

TEST_CASE("estimateTransmittance decreases with airmass", "[atm][dos2]")
{
    float T1 = AtmosphericCorrection::estimateTransmittance(1.0f);
    float T2 = AtmosphericCorrection::estimateTransmittance(2.0f);
    REQUIRE(T1 > T2);
}

// --- Integration: multi-band correction ---

TEST_CASE("DOS1 multi-band correction preserves band independence", "[atm][dos1][integration]")
{
    // Each band should be corrected independently
    std::vector<float> band1 = {50.0f, 100.0f, 200.0f};
    std::vector<float> band2 = {80.0f, 60.0f, 150.0f};
    std::vector<float> out1(3), out2(3);

    AtmosphericCorrection::dos1(band1.data(), out1.data(), 3, 0.01f, 0.0f);
    AtmosphericCorrection::dos1(band2.data(), out2.data(), 3, 0.01f, 0.0f);

    // Band 1 min DN=50 → radiance=0.5, subtracted
    REQUIRE_THAT(out1[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    // Band 2 min DN=60 → radiance=0.6, subtracted
    REQUIRE_THAT(out2[1], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    // Band 2 DN=80 → radiance=0.8, surface=0.8-0.6=0.2
    REQUIRE_THAT(out2[0], Catch::Matchers::WithinAbs(0.2f, 0.001f));
}

#include "processing/gdal/gdal_dataset_wrapper.h"
#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>

TEST_CASE("AtmosphericCorrection processFile applies DOS1", "[atm][gdal]")
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 2, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);

    std::vector<float> band = {50.0f, 100.0f, 200.0f, 80.0f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 2, band.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALClose(srcDs);

    QString error;
    const bool ok = AtmosphericCorrection::processFile(sourcePath, outputPath, 1, 1,
                                                       0.01f, 0.0f, 1.0f, &error);
    REQUIRE(ok);
    REQUIRE(QFile::exists(outputPath));
    REQUIRE(error.isEmpty());

    // Radiance = 0.01*DN = {0.5, 1.0, 2.0, 0.8}. The 4-pixel scene is below
    // the histogram frequency floor (0.01% of 4 → threshold 2), so the dark
    // object falls back to the global minimum 0.5 and the scene maps to
    // {0.0, 0.5, 1.5, 0.3}.
    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> result(4);
    REQUIRE(out.readBandData(1, result.data(), 2, 2));
    REQUIRE_THAT(result[0], Catch::Matchers::WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(result[1], Catch::Matchers::WithinAbs(0.5f, 1e-4f));
    REQUIRE_THAT(result[2], Catch::Matchers::WithinAbs(1.5f, 1e-4f));
    REQUIRE_THAT(result[3], Catch::Matchers::WithinAbs(0.3f, 1e-4f));
}

// --- QUAC (Quick Atmospheric Correction) ---

TEST_CASE("QUAC rejects fewer than 2 bands", "[atm][quac]")
{
    std::vector<float> dn = {100.0f, 200.0f, 50.0f};
    std::vector<float> out(3);
    float *dnPtr = dn.data();
    float *outPtr = out.data();
    QString err;
    REQUIRE_FALSE(AtmosphericCorrection::quac(&dnPtr, &outPtr, 1, 3, &err));
}

TEST_CASE("QUAC rejects null buffers", "[atm][quac]")
{
    std::vector<float> dn(3), out(3);
    float *dnPtr = dn.data();
    float *outPtr = out.data();
    QString err;
    REQUIRE_FALSE(AtmosphericCorrection::quac(nullptr, &outPtr, 2, 3, &err));
    REQUIRE_FALSE(AtmosphericCorrection::quac(&dnPtr, nullptr, 2, 3, &err));
}

TEST_CASE("QUAC outputs values in [0, 1]", "[atm][quac]")
{
    // 3 bands, 100 pixels each with ascending DN so percentiles are meaningful.
    const size_t n = 100;
    std::vector<std::vector<float>> dnBands(3, std::vector<float>(n));
    std::vector<std::vector<float>> outBands(3, std::vector<float>(n));
    for (size_t i = 0; i < n; ++i) {
        dnBands[0][i] = static_cast<float>(i);          // 0..99
        dnBands[1][i] = static_cast<float>(i * 2);      // 0..198
        dnBands[2][i] = static_cast<float>(i + 10);     // 10..109
    }
    std::vector<float *> dnPtrs = {dnBands[0].data(), dnBands[1].data(), dnBands[2].data()};
    std::vector<float *> outPtrs = {outBands[0].data(), outBands[1].data(), outBands[2].data()};

    QString err;
    REQUIRE(AtmosphericCorrection::quac(dnPtrs.data(), outPtrs.data(), 3, n, &err));

    // Every output pixel must be in [0, 1].
    for (int b = 0; b < 3; ++b) {
        for (size_t i = 0; i < n; ++i) {
            REQUIRE(outBands[b][i] >= 0.0f);
            REQUIRE(outBands[b][i] <= 1.0f);
        }
    }
}

TEST_CASE("QUAC dark pixel maps near zero", "[atm][quac]")
{
    // Minimum DN (1st percentile) should produce a near-zero reflectance.
    const size_t n = 100;
    std::vector<std::vector<float>> dnBands(2, std::vector<float>(n));
    std::vector<std::vector<float>> outBands(2, std::vector<float>(n));
    for (size_t i = 0; i < n; ++i) {
        dnBands[0][i] = static_cast<float>(10 + i);     // 10..109
        dnBands[1][i] = static_cast<float>(20 + i);     // 20..119
    }
    std::vector<float *> dnPtrs = {dnBands[0].data(), dnBands[1].data()};
    std::vector<float *> outPtrs = {outBands[0].data(), outBands[1].data()};

    QString err;
    REQUIRE(AtmosphericCorrection::quac(dnPtrs.data(), outPtrs.data(), 2, n, &err));

    // The smallest input DN (~10, the 1st percentile) should yield ~0 reflectance
    // after the offset subtraction.
    REQUIRE(outBands[0][0] >= 0.0f);
    REQUIRE(outBands[0][0] < 0.05f);
}

TEST_CASE("QUAC rejects all-dark scene (meanBright==0)", "[atm][quac]")
{
    // All pixels zero -> bright percentile is 0 -> meanBright == 0 -> must reject.
    const size_t n = 50;
    std::vector<std::vector<float>> dnBands(2, std::vector<float>(n, 0.0f));
    std::vector<std::vector<float>> outBands(2, std::vector<float>(n));
    std::vector<float *> dnPtrs = {dnBands[0].data(), dnBands[1].data()};
    std::vector<float *> outPtrs = {outBands[0].data(), outBands[1].data()};

    QString err;
    REQUIRE_FALSE(AtmosphericCorrection::quac(dnPtrs.data(), outPtrs.data(), 2, n, &err));
    REQUIRE_FALSE(err.isEmpty());
}

TEST_CASE("QUAC rejects zero dynamic range", "[atm][quac]")
{
    // All pixels the same non-zero value -> range == 0 -> must reject.
    const size_t n = 50;
    std::vector<std::vector<float>> dnBands(2, std::vector<float>(n, 42.0f));
    std::vector<std::vector<float>> outBands(2, std::vector<float>(n));
    std::vector<float *> dnPtrs = {dnBands[0].data(), dnBands[1].data()};
    std::vector<float *> outPtrs = {outBands[0].data(), outBands[1].data()};

    QString err;
    REQUIRE_FALSE(AtmosphericCorrection::quac(dnPtrs.data(), outPtrs.data(), 2, n, &err));
    REQUIRE_FALSE(err.isEmpty());
}

TEST_CASE("processFileMultiBand applies QUAC", "[atm][quac][gdal]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    // 10x10, 3-band raster with a gradient so QUAC has dynamic range.
    const int W = 10, H = 10, B = 3;
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, B, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<float> band(W * H);
    for (int b = 0; b < B; ++b) {
        for (int i = 0; i < W * H; ++i)
            band[i] = static_cast<float>(i * (b + 1));
        GDALRasterBandH rb = GDALGetRasterBand(srcDs, b + 1);
        REQUIRE(GDALRasterIO(rb, GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(srcDs);

    QString error;
    const bool ok = AtmosphericCorrection::processFileMultiBand(sourcePath, outputPath, 3, &error);
    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(QFile::exists(outputPath));

    // Verify output is 3-band Float32 with values in [0, 1].
    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.bandCount() == B);
    std::vector<float> result(W * H);
    for (int b = 0; b < B; ++b) {
        REQUIRE(out.readBandData(b + 1, result.data(), W, H));
        for (int i = 0; i < W * H; ++i) {
            REQUIRE(result[i] >= 0.0f);
            REQUIRE(result[i] <= 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// #610: Chavez DOS in TOA-reflectance space
// ---------------------------------------------------------------------------

TEST_CASE("dosReflectance kernel: Chavez 1% assumption in reflectance space", "[atm][dos1]")
{
    // rho_surf = (rho_TOA - dark + 0.01) / T
    REQUIRE_THAT(AtmosphericCorrection::dosReflectance(0.30f, 0.10f, 1.0f),
                 Catch::Matchers::WithinAbs(0.21f, 1e-6f));
    // DOS2 divides by transmittance.
    REQUIRE_THAT(AtmosphericCorrection::dosReflectance(0.30f, 0.10f, 0.9f),
                 Catch::Matchers::WithinAbs(0.21f / 0.9f, 1e-6f));
    // NaN input propagates.
    REQUIRE(std::isnan(AtmosphericCorrection::dosReflectance(
        std::numeric_limits<float>::quiet_NaN(), 0.1f, 1.0f)));
    // Non-positive transmittance is invalid.
    REQUIRE(std::isnan(AtmosphericCorrection::dosReflectance(0.3f, 0.1f, 0.0f)));
}

TEST_CASE("processFileDos produces reflectance-scale surface output (#610)", "[atm][gdal]")
{
    // Landsat-like synthetic scene: reflMult=2e-5, reflAdd=0, sunEl=30deg.
    // rho_TOA = 2e-5 * DN / sin(30) = 4e-5 * DN.
    // DN 10000 -> rho 0.40; DN 3000 -> 0.12; DN 6000 -> 0.24.
    // Dark-object level (single tile, histogram falls back to min for tiny
    // scenes) = 0.12 -> rho_surf = rho_TOA - 0.12 + 0.01.
    // Expected: 0.40-0.11=0.29, 0.12-0.11=0.01, 0.24-0.11=0.13.
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("dos_src.tif"));
    const QString dst = dir.filePath(QStringLiteral("dos_out.tif"));

    GDALDriverH drv = GDALGetDriverByName("GTiff");
    REQUIRE(drv);
    GDALDatasetH ds = GDALCreate(drv, src.toUtf8().constData(), 3, 1, 1, GDT_Float32, nullptr);
    REQUIRE(ds);
    double gt[6] = {0, 1, 0, 0, 0, -1};
    GDALSetGeoTransform(ds, gt);
    GDALRasterBandH b = GDALGetRasterBand(ds, 1);
    std::vector<float> dn = {10000.0f, 3000.0f, 6000.0f};
    REQUIRE(GDALRasterIO(b, GF_Write, 0, 0, 3, 1, dn.data(), 3, 1, GDT_Float32, 0, 0) == CE_None);
    GDALClose(ds);

    RadiometricCalibration::BandCoefficients c;
    c.reflMult = 2e-5;
    c.reflAdd = 0.0;
    c.hasReflectance = true;
    QString err;
    REQUIRE(AtmosphericCorrection::processFileDos(
        src, dst, 1, AtmosphericCorrection::Dos1, c,
        RadiometricCalibration::SensorType::Landsat, 30.0, 1.0f, &err));

    GdalDatasetWrapper out;
    REQUIRE(out.open(dst));
    std::vector<float> result(3, -999.0f);
    REQUIRE(out.readBandData(1, result.data(), 3, 1));
    REQUIRE_THAT(result[0], Catch::Matchers::WithinAbs(0.29f, 0.005f));
    REQUIRE_THAT(result[1], Catch::Matchers::WithinAbs(0.01f, 0.005f));
    REQUIRE_THAT(result[2], Catch::Matchers::WithinAbs(0.13f, 0.005f));
}

TEST_CASE("processFileDos refuses Landsat without finite sun elevation (#610)", "[atm][gdal]")
{
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("dos_src2.tif"));
    const QString dst = dir.filePath(QStringLiteral("dos_out2.tif"));
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    REQUIRE(drv);
    GDALDatasetH ds = GDALCreate(drv, src.toUtf8().constData(), 1, 1, 1, GDT_Float32, nullptr);
    REQUIRE(ds);
    GDALClose(ds);
    RadiometricCalibration::BandCoefficients c;
    c.reflMult = 2e-5;
    QString err;
    REQUIRE_FALSE(AtmosphericCorrection::processFileDos(
        src, dst, 1, AtmosphericCorrection::Dos1, c,
        RadiometricCalibration::SensorType::Landsat,
        std::numeric_limits<double>::quiet_NaN(), 1.0f, &err));
}

TEST_CASE("processFileMultiBand QUAC streaming matches the in-memory kernel (#634)", "[atm][quac][gdal][stream]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("quac_stream_src.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("quac_stream_out.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    // 300x40 spans multiple 256-tiles in x, exercising the streaming path.
    constexpr int W = 300, H = 40, B = 3;
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, B, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    std::vector<std::vector<float>> bands(B, std::vector<float>(W * H));
    for (int b = 0; b < B; ++b) {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                bands[b][i] = static_cast<float>((x * 7 + y * 13) % 251) * (b + 1);
            }
        GDALRasterBandH rb = GDALGetRasterBand(srcDs, b + 1);
        REQUIRE(GDALRasterIO(rb, GF_Write, 0, 0, W, H, bands[b].data(), W, H, GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(srcDs);

    // Reference: the in-memory kernel on the same buffers.
    std::vector<std::vector<float>> ref(B, std::vector<float>(W * H));
    std::vector<const float *> inPtrs(B);
    std::vector<float *> outPtrs(B);
    for (int b = 0; b < B; ++b) {
        inPtrs[b] = bands[b].data();
        outPtrs[b] = ref[b].data();
    }
    QString kErr;
    REQUIRE(AtmosphericCorrection::quac(inPtrs.data(), outPtrs.data(), B,
                                        static_cast<size_t>(W) * H, &kErr));

    // Streaming file path.
    QString error;
    REQUIRE(AtmosphericCorrection::processFileMultiBand(sourcePath, outputPath,
                                                        AtmosphericCorrection::Quac, &error));

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    std::vector<float> result(W * H);
    for (int b = 0; b < B; ++b) {
        REQUIRE(out.readBandData(b + 1, result.data(), W, H));
        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
            // The streaming path derives percentiles from a 65536-bin
            // histogram (bin-centre rank) while the kernel uses exact
            // nth_element - allow a small absolute tolerance.
            REQUIRE_THAT(result[i], Catch::Matchers::WithinAbs(ref[b][i], 0.02f));
        }
    }
}

// ---------------------------------------------------------------------------
// #675: multi-band QUAC output declares NaN NoData per band (never the
// band-1 source sentinel, which the write pass converts to NaN anyway).
// ---------------------------------------------------------------------------

TEST_CASE("processFileMultiBand QUAC declares NaN NoData on every output band (#675)",
          "[atm][quac][gdal]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("quac_nd_src.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("quac_nd_out.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    // 2-band 8x8 raster. Band 1 declares a sentinel NoData (0.0) like a
    // source stack would; band 2 carries no declaration. One invalid pixel
    // per band (sentinel on band 1, raw NaN on band 2).
    constexpr int W = 8, H = 8, B = 2;
    const float nanF = std::numeric_limits<float>::quiet_NaN();
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, B, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    {
        GDALRasterBandH rb1 = GDALGetRasterBand(srcDs, 1);
        REQUIRE(rb1 != nullptr);
        GDALSetRasterNoDataValue(rb1, 0.0);
        std::vector<float> band1(static_cast<size_t>(W) * H);
        for (int i = 0; i < W * H; ++i)
            band1[i] = static_cast<float>(i) + 1.0f; // dynamic range, no zeros
        band1[0] = 0.0f;                             // declared sentinel
        REQUIRE(GDALRasterIO(rb1, GF_Write, 0, 0, W, H, band1.data(), W, H,
                             GDT_Float32, 0, 0) == CE_None);

        GDALRasterBandH rb2 = GDALGetRasterBand(srcDs, 2);
        REQUIRE(rb2 != nullptr);
        std::vector<float> band2(static_cast<size_t>(W) * H);
        for (int i = 0; i < W * H; ++i)
            band2[i] = static_cast<float>(i) + 1.0f;
        band2[1] = nanF; // undeclared NaN invalid pixel
        REQUIRE(GDALRasterIO(rb2, GF_Write, 0, 0, W, H, band2.data(), W, H,
                             GDT_Float32, 0, 0) == CE_None);
    }
    GDALClose(srcDs);

    QString error;
    REQUIRE(AtmosphericCorrection::processFileMultiBand(
        sourcePath, outputPath, AtmosphericCorrection::Quac, &error));
    REQUIRE(QFile::exists(outputPath));

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    REQUIRE(out.bandCount() == B);

    // Every band declares NaN as NoData (before the fix band 1 declared the
    // source sentinel 0.0 and bands >= 2 declared nothing).
    for (int b = 1; b <= B; ++b) {
        bool has = false;
        const double nd = out.bandNoDataValue(b, &has);
        REQUIRE(has);
        REQUIRE(std::isnan(nd));
    }

    // Each band's own invalid pixel comes back NaN; every valid pixel stays
    // finite. Band 1 pixel 0 was the declared sentinel, band 2 pixel 1 a raw
    // NaN — the old code declared the sentinel for band 1 only and left
    // band 2's NaN undeclared.
    std::vector<float> band1(static_cast<size_t>(W) * H);
    std::vector<float> band2(static_cast<size_t>(W) * H);
    REQUIRE(out.readBandData(1, band1.data(), W, H));
    REQUIRE(out.readBandData(2, band2.data(), W, H));
    REQUIRE(std::isnan(band1[0]));
    REQUIRE(std::isfinite(band1[1]));
    REQUIRE(std::isfinite(band2[0]));
    REQUIRE(std::isnan(band2[1]));
    for (int i = 2; i < W * H; ++i) {
        REQUIRE(std::isfinite(band1[i]));
        REQUIRE(std::isfinite(band2[i]));
    }
}
