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

TEST_CASE("BandMath evaluates std::min and std::max with mixed integer and float operands (#434)", "[bandmath][func][434]")
{
    auto bands = makeTwoBandData(); // b1 = [0.5, 0.8, 0.0, 0.3], b2 = [0.1, 0.2, 0.0, 0.3]
    std::vector<float> out(4);

    // std::min and std::max between bands
    REQUIRE(BandMath::evaluate("std::min(b1, b2)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.1f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.2f, 0.001f));

    REQUIRE(BandMath::evaluate("std::max(b1, b2)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.8f, 0.001f));

    // Mixed integer literal operand with float band
    REQUIRE(BandMath::evaluate("std::min(b1, 0)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.0f, 0.001f));

    REQUIRE(BandMath::evaluate("std::max(b1, 1)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(1.0f, 0.001f));

    REQUIRE(BandMath::evaluate("min(b1, 10)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 0.001f));

    REQUIRE(BandMath::evaluate("max(100, b1)", bands, out.data(), 4));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(100.0f, 0.001f));

    // Nested clamping expression
    REQUIRE(BandMath::evaluate("std::min(std::max(b1, 0.2), 0.6)", bands, out.data(), 4));
    // b1 = [0.5, 0.8, 0.0, 0.3] -> clamp [0.2, 0.6] -> [0.5, 0.6, 0.2, 0.3]
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 0.001f));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(0.6f, 0.001f));
    REQUIRE_THAT(out[2], Catch::Matchers::WithinAbs(0.2f, 0.001f));
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(0.3f, 0.001f));

    // referencedBands extracts bands correctly from std::min / std::max expressions
    auto refs = BandMath::referencedBands("std::min(b1, 10) + std::max(b2, 0)");
    REQUIRE(refs.size() == 2);
    CHECK(refs[0] == 1);
    CHECK(refs[1] == 2);
}

TEST_CASE("BandMath min/max/pow/atan2 propagate NaN symmetrically regardless of argument order (#434)", "[bandmath][func][434]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    BandMath::BandData bands;
    bands[1] = {0.5f, nan, nan}; // b1 NaN at pixels 1, 2
    bands[2] = {nan, 0.2f, nan}; // b2 NaN at pixels 0, 2

    std::vector<float> out(3);
    REQUIRE(BandMath::evaluate("min(b1, b2)", bands, out.data(), 3));
    for (float v : out)
        CHECK(std::isnan(v));

    // Argument order must not change the result (NaN commutativity)
    std::vector<float> outSwapped(3);
    REQUIRE(BandMath::evaluate("min(b2, b1)", bands, outSwapped.data(), 3));
    for (float v : outSwapped)
        CHECK(std::isnan(v));

    std::vector<float> outMax(3);
    REQUIRE(BandMath::evaluate("max(b1, b2)", bands, outMax.data(), 3));
    for (float v : outMax)
        CHECK(std::isnan(v));

    // Clamping a NoData pixel against a literal must not leak a valid-looking
    // value: min(NaN, 10) is NaN, not 10.
    std::vector<float> outClamp(3);
    REQUIRE(BandMath::evaluate("min(b1, 10)", bands, outClamp.data(), 3));
    REQUIRE_THAT(outClamp[0], Catch::Matchers::WithinAbs(0.5f, 0.001f));
    CHECK(std::isnan(outClamp[1]));
    CHECK(std::isnan(outClamp[2]));

    // pow/atan2 with a NaN operand propagate NaN
    std::vector<float> outPow(3);
    REQUIRE(BandMath::evaluate("pow(b1, 2)", bands, outPow.data(), 3));
    CHECK(std::isnan(outPow[1]));
    std::vector<float> outAtan(3);
    REQUIRE(BandMath::evaluate("atan2(b1, b2)", bands, outAtan.data(), 3));
    for (float v : outAtan)
        CHECK(std::isnan(v));
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

TEST_CASE("BandMath processFile masks large-magnitude float NoData sentinel (#434)", "[bandmath][gdal][434]")
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("source_bignd.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("output_bignd.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 2, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);

    // GDAL's default float NoData sentinel (~ -FLT_MAX): a fixed 1e-6 absolute
    // tolerance is far below one ULP at this magnitude and would never match.
    const float sentinel = -3.4028235e+38f;
    const std::vector<float> band1 = {0.5f, sentinel, sentinel, 0.8f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 2, const_cast<float *>(band1.data()),
                          2, 2, GDT_Float32, 0, 0) == CE_None);
    GDALSetRasterNoDataValue(b1, static_cast<double>(sentinel));
    GDALClose(srcDs);

    QString error;
    REQUIRE(BandMath::processFile(sourcePath, outputPath, QStringLiteral("b1 * 2 + 1"), &error));

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    std::vector<float> out(4);
    REQUIRE(outDs.readBandData(1, out.data(), 2, 2));
    // Sentinel pixels must be masked to NaN before evaluation (not 2*-3.4e38+1)
    CHECK(std::isnan(out[1]));
    CHECK(std::isnan(out[2]));
    REQUIRE_THAT(out[0], Catch::Matchers::WithinAbs(2.0f, 0.001f));
    REQUIRE_THAT(out[3], Catch::Matchers::WithinAbs(2.6f, 0.001f));
}

TEST_CASE("BandMath processFile masks Inf pixels to NaN before evaluation (#449)", "[bandmath][gdal][449]")
{
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString sourcePath = dir.filePath(QStringLiteral("inf.tif"));
    const QString outputPath = dir.filePath(QStringLiteral("inf_out.tif"));
    std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};

    GDALDatasetH srcDs = createOutputTiff(sourcePath, 2, 1, 1, GDT_Float32, gt, QString());
    REQUIRE(srcDs != nullptr);

    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<float> band1 = {inf, 1.0f};
    GDALRasterBandH b1 = GDALGetRasterBand(srcDs, 1);
    REQUIRE(GDALRasterIO(b1, GF_Write, 0, 0, 2, 1, const_cast<float *>(band1.data()),
                          2, 1, GDT_Float32, 0, 0) == CE_None);
    GDALClose(srcDs);

    QString error;
    REQUIRE(BandMath::processFile(sourcePath, outputPath, QStringLiteral("b1 * 2"), &error));

    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(outputPath));
    std::vector<float> out(2);
    REQUIRE(outDs.readBandData(1, out.data(), 2, 1));
    // Inf operand must be masked to NaN so the expression never sees it
    CHECK(std::isnan(out[0]));
    REQUIRE_THAT(out[1], Catch::Matchers::WithinAbs(2.0f, 0.001f));
}
