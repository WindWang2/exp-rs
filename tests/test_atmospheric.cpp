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
}
