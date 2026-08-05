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

// --- Functions ---

TEST_CASE("BandMath evaluates sqrt function", "[bandmath][func]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    BandMath::evaluate("sqrt(b1)", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(std::sqrt(0.5f), 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(std::sqrt(0.8f), 0.001f));
}

TEST_CASE("BandMath evaluates exp and ln functions", "[bandmath][func]")
{
    BandMath::BandData bands;
    bands[1] = {1.0f};
    std::vector<float> out(1);
    BandMath::evaluate("exp(b1)", bands, out.data(), 1);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(std::exp(1.0f), 0.001f));
    BandMath::evaluate("ln(b1)", bands, out.data(), 1);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
}

TEST_CASE("BandMath evaluates two-arg functions pow/min/max", "[bandmath][func]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    BandMath::evaluate("pow(b1, 2)", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.25f, 0.001f));
    BandMath::evaluate("min(b1, b2)", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.1f, 0.001f)); // min(0.5, 0.1)
    BandMath::evaluate("max(b1, b2)", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 0.001f)); // max(0.5, 0.1)
}

TEST_CASE("BandMath evaluates pi() constant", "[bandmath][func]")
{
    BandMath::BandData bands;
    std::vector<float> out(2);
    BandMath::evaluate("pi()", bands, out.data(), 2);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(static_cast<float>(M_PI), 0.001f));
}

TEST_CASE("BandMath evaluates nested function sqrt(b1*b1+b2*b2)", "[bandmath][func]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    BandMath::evaluate("sqrt(b1*b1 + b2*b2)", bands, out.data(), 4);
    float expected = std::sqrt(0.5f * 0.5f + 0.1f * 0.1f);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(expected, 0.001f));
}

TEST_CASE("BandMath rejects unknown function", "[bandmath][func]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    bool ok = BandMath::evaluate("foobar(b1)", bands, out.data(), 4);
    REQUIRE_FALSE(ok);
}

TEST_CASE("BandMath rejects wrong argument count", "[bandmath][func]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    // sqrt is single-arg; two args should fail.
    REQUIRE_FALSE(BandMath::evaluate("sqrt(b1, b2)", bands, out.data(), 4));
}

// --- Comparison operators ---

TEST_CASE("BandMath evaluates comparison operators", "[bandmath][cmp]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    // b1 = [0.5, 0.8, 0.0, 0.3], b2 = [0.1, 0.2, 0.0, 0.3]
    BandMath::evaluate("b1 > b2", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.5 > 0.1 → true
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.0 > 0.0 → false

    BandMath::evaluate("b1 >= 0.5", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.5 >= 0.5
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.0 >= 0.5

    BandMath::evaluate("b1 == b2", bands, out.data(), 4);
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.0 == 0.0
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.3 == 0.3

    BandMath::evaluate("b1 != b2", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.5 != 0.1
}

// --- Logical operators ---

TEST_CASE("BandMath evaluates logical AND/OR", "[bandmath][logic]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    // b1 = [0.5, 0.8, 0.0, 0.3], b2 = [0.1, 0.2, 0.0, 0.3]
    BandMath::evaluate("b1 > 0.3 && b2 < 0.25", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.5>0.3=T, 0.1<0.25=T → T
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.8>0.3=T, 0.2<0.25=T → T
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.0>0.3=F → F
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.3>0.3=F → F
}

TEST_CASE("BandMath evaluates logical OR", "[bandmath][logic]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    BandMath::evaluate("b1 > 0.6 || b2 > 0.15", bands, out.data(), 4);
    // b1=[0.5,0.8,0.0,0.3] b2=[0.1,0.2,0.0,0.3]
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.5>0.6=F, 0.1>0.15=F → F
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f)); // 0.8>0.6=T → T
}

// --- Conditional (ternary) ---

TEST_CASE("BandMath evaluates ternary conditional", "[bandmath][cond]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    // b1 = [0.5, 0.8, 0.0, 0.3]
    BandMath::evaluate("b1 > 0.4 ? b2 : 0", bands, out.data(), 4);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.1f, 0.001f)); // 0.5>0.4 → b2=0.1
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.2f, 0.001f)); // 0.8>0.4 → b2=0.2
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f)); // 0.0>0.4 → 0
}

TEST_CASE("BandMath evaluates nested conditional with function", "[bandmath][cond]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    // b1=[0.5,0.8,0.0,0.3] b2=[0.1,0.2,0.0,0.3]
    BandMath::evaluate("b1 > 0.4 ? sqrt(b2) : b1 * 2", bands, out.data(), 4);
    float expectedTrue = std::sqrt(0.1f);
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(expectedTrue, 0.001f)); // 0.5>0.4 → sqrt(0.1)
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f));         // 0.0≤0.4 → 0*2
}

TEST_CASE("BandMath evaluates complex threshold mask", "[bandmath][cond]")
{
    auto bands = makeTwoBandData();
    std::vector<float> out(4);
    BandMath::evaluate("(b1 > 0.4 && b2 < 0.25) ? 1 : 0", bands, out.data(), 4);
    // pixel 0: 0.5>0.4=T, 0.1<0.25=T → 1
    // pixel 1: 0.8>0.4=T, 0.2<0.25=T → 1
    // pixel 2: 0.0>0.4=F → 0
    // pixel 3: 0.3>0.4=F → 0
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(0.0f, 0.001f));
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
