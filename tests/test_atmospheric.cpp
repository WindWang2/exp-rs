// tests/test_atmospheric.cpp — TDD Red phase for DOS atmospheric correction
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
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

TEST_CASE("DOS1 processFile preserves NoData and excludes NoData from dark object stats", "[atm][dos1][nodata]")
{
    ensureGdalInit();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("src_nodata.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("out_nodata.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    const int W = 4, H = 4;
    GDALDatasetH srcDs = createOutputTiff(sourcePath, W, H, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);
    GDALRasterBandH rb = GDALGetRasterBand(srcDs, 1);
    GDALSetRasterNoDataValue(rb, -9999.0);

    // Valid pixels: 100..115; NoData pixels at corners: -9999.0
    std::vector<float> data(W * H);
    for (int i = 0; i < W * H; ++i)
        data[i] = 100.0f + static_cast<float>(i);
    data[0] = -9999.0f;
    data[15] = -9999.0f;

    REQUIRE(GDALRasterIO(rb, GF_Write, 0, 0, W, H, data.data(), W, H, GDT_Float32, 0, 0) == CE_None);
    GDALClose(srcDs);

    QString error;
    const bool ok = AtmosphericCorrection::processFile(sourcePath, outputPath, 1,
                                                      AtmosphericCorrection::Method::Dos1,
                                                      0.01f, 0.0f, 1.0f, &error);
    REQUIRE(ok);
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper out;
    REQUIRE(out.open(outputPath));
    bool hasNoData = false;
    CHECK(out.bandNoDataValue(1, &hasNoData) == Catch::Approx(-9999.0));
    CHECK(hasNoData);

    std::vector<float> res(W * H);
    REQUIRE(out.readBandData(1, res.data(), W, H));
    // NoData pixels must stay -9999
    CHECK(res[0] == Catch::Approx(-9999.0f));
    CHECK(res[15] == Catch::Approx(-9999.0f));
    // Dark object should be lowest valid pixel (101.0 -> radiance 1.01), so valid pixels are >= 0
    for (int i = 1; i < 15; ++i) {
        CHECK(res[i] >= 0.0f);
    }
}
