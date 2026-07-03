// tests/test_band_math.cpp — TDD Red phase for band math engine
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/band_math.h"

#include <vector>
#include <cmath>
#include <map>

// Helper: simple 4-pixel, 2-band dataset
static BandMath::BandData makeTwoBandData()
{
    BandMath::BandData bands;
    bands[1] = {0.5f, 0.8f, 0.0f, 0.3f}; // band 1
    bands[2] = {0.1f, 0.2f, 0.0f, 0.3f}; // band 2
    return bands;
}

// --- Expression parsing & evaluation ---

TEST_CASE("BandMath evaluates constant expression", "[bandmath]")
{
    BandMath::BandData bands;
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("42", bands, out.data(), 4);
    REQUIRE(ok);
    for (int i = 0; i < 4; i++)
        REQUIRE_THAT(out[i], Catch::Matchers::WithinAbs(42.0f, 0.001f));
}

TEST_CASE("BandMath evaluates band reference", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("b1", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.8f, 0.001f));
}

TEST_CASE("BandMath evaluates addition", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("b1 + b2", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.6f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("BandMath evaluates subtraction", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("b1 - b2", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.4f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.6f, 0.001f));
}

TEST_CASE("BandMath evaluates multiplication", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("b1 * b2", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.05f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.16f, 0.001f));
}

TEST_CASE("BandMath evaluates division", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    bool ok = BandMath::evaluate("b1 / b2", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(5.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(4.0f, 0.001f));
}

TEST_CASE("BandMath handles division by zero", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    BandMath::evaluate("b1 / b2", bands, out.data(), 4);
    // pixel 2: b1=0.0, b2=0.0 → 0/0 = NaN
    REQUIRE(std::isnan(out[2]));
}

TEST_CASE("BandMath evaluates parenthesized expression", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    // NDVI-like: (b1 - b2) / (b1 + b2)
    bool ok = BandMath::evaluate("(b1 - b2) / (b1 + b2)", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.4f / 0.6f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.6f / 1.0f, 0.001f));
}

TEST_CASE("BandMath evaluates expression with constant factor", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    // 2.5 * b1
    bool ok = BandMath::evaluate("2.5 * b1", bands, out.data(), 4);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.25f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(2.0f, 0.001f));
}

TEST_CASE("BandMath evaluates complex expression", "[bandmath]")
{
    // EVI-like: 2.5 * (b1 - b2) / (b1 + 6 * b2 - 7.5 * b3 + 1)
    BandMath::BandData bands;
    bands[1] = {0.5f}; // NIR
    bands[2] = {0.1f}; // Red
    bands[3] = {0.05f}; // Blue
    std::vector<float> out(1);

    bool ok = BandMath::evaluate("2.5 * (b1 - b2) / (b1 + 6 * b2 - 7.5 * b3 + 1)", bands, out.data(), 1);
    REQUIRE(ok);
    float expected = 2.5f * (0.5f - 0.1f) / (0.5f + 6.0f * 0.1f - 7.5f * 0.05f + 1.0f);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(expected, 0.001f));
}

TEST_CASE("BandMath evaluates expression with 3+ bands", "[bandmath]")
{
    BandMath::BandData bands;
    bands[1] = {1.0f};
    bands[2] = {2.0f};
    bands[3] = {3.0f};
    std::vector<float> out(1);

    bool ok = BandMath::evaluate("b1 + b2 + b3", bands, out.data(), 1);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(6.0f, 0.001f));
}

TEST_CASE("BandMath evaluates negative constant", "[bandmath]")
{
    BandMath::BandData bands;
    bands[1] = {0.5f};
    std::vector<float> out(1);

    bool ok = BandMath::evaluate("-b1", bands, out.data(), 1);
    REQUIRE(ok);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(-0.5f, 0.001f));
}

// --- Error handling ---

TEST_CASE("BandMath rejects empty expression", "[bandmath]")
{
    BandMath::BandData bands;
    std::vector<float> out(4);
    REQUIRE_FALSE(BandMath::evaluate("", bands, out.data(), 4));
}

TEST_CASE("BandMath rejects null output buffer", "[bandmath]")
{
    auto bands = makeTwoBandData();
    REQUIRE_FALSE(BandMath::evaluate("b1", bands, nullptr, 4));
}

TEST_CASE("BandMath rejects missing band in expression", "[bandmath]")
{
    auto bands = makeTwoBandData(); // has b1, b2
    std::vector<float> out(4);
    REQUIRE_FALSE(BandMath::evaluate("b3", bands, out.data(), 4)); // b3 not provided
}

TEST_CASE("BandMath rejects zero count", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    REQUIRE_FALSE(BandMath::evaluate("b1", bands, out.data(), 0));
}

TEST_CASE("BandMath rejects malformed expression", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    REQUIRE_FALSE(BandMath::evaluate("b1 +", bands, out.data(), 4));
    REQUIRE_FALSE(BandMath::evaluate("(b1 + b2", bands, out.data(), 4)); // unmatched paren
    REQUIRE_FALSE(BandMath::evaluate("+ b1", bands, out.data(), 4));
}

TEST_CASE("BandMath handles very large numbers gracefully", "[bandmath]")
{
    BandMath::BandData bands;
    std::vector<float> out(4);

    // Very large number that could cause std::stof to throw std::out_of_range
    // This should not crash, even if it returns false or produces inf
    REQUIRE_NOTHROW(BandMath::evaluate("999999999999999999999999999999999999999999999999", bands, out.data(), 4));
}

TEST_CASE("BandMath handles very small numbers gracefully", "[bandmath]")
{
    BandMath::BandData bands;
    std::vector<float> out(4);

    // Very small number with many decimal places
    REQUIRE_NOTHROW(BandMath::evaluate("0.000000000000000000000000000000000000000000000001", bands, out.data(), 4));
}

// --- Operator precedence ---

TEST_CASE("BandMath respects operator precedence (* over +)", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    // b1 + b2 * 2 should be b1 + (b2 * 2), not (b1 + b2) * 2
    BandMath::evaluate("b1 + b2 * 2", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f + 0.1f * 2.0f, 0.001f)); // 0.7
}

TEST_CASE("BandMath respects operator precedence (* over -)", "[bandmath]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);

    // b1 * 2 - b2 should be (b1 * 2) - b2
    BandMath::evaluate("b1 * 2 - b2", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f * 2.0f - 0.1f, 0.001f)); // 0.9
}

// --- File-level processing (dialog async pipeline) ---

#include "processing/gdal/gdal_dataset_wrapper.h"
#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>

TEST_CASE("BandMath processFile writes evaluated raster", "[bandmath][gdal]")
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 2, 2, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);

    std::vector<float> band1 = {0.5f, 0.8f, 0.0f, 0.3f};
    std::vector<float> band2 = {0.1f, 0.2f, 0.0f, 0.3f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    GDALRasterBandH b2 = GDALGetRasterBand(srcDs, 2);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 2, band1.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    REQUIRE(GDALRasterIO(b2, GF_Write, 0, 0, 2, 2, band2.data(), 2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALClose(srcDs);

    QString error;
    const bool ok = BandMath::processFile(sourcePath, outputPath, QStringLiteral("(b1 - b2) / (b1 + b2)"), &error);
    REQUIRE(ok);
    REQUIRE(QFile::exists(outputPath));
    REQUIRE(error.isEmpty());

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    REQUIRE(outDs.bandCount() == 1);

    std::vector<float> out(4);
    REQUIRE(outDs.readBandData(1, out.data(), 2, 2));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.666666f, 0.01f));
}
