// test_endmember_extraction.cpp — PPI endmember extraction kernel + operator
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <array>
#include <set>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/endmember_extraction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_endmember_extraction";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

/// Simplex data: three pure vertices plus interior mixtures.
std::vector<float> simplexData()
{
    // 8 pixels x 3 bands: pixels 0,1,2 are the pure endmembers; the rest mix them.
    const float e1[] = {1.0f, 0.0f, 0.0f};
    const float e2[] = {0.0f, 1.0f, 0.0f};
    const float e3[] = {0.0f, 0.0f, 1.0f};
    std::vector<float> pixels;
    for (int b = 0; b < 3; ++b)
        pixels.push_back(e1[b]);
    for (int b = 0; b < 3; ++b)
        pixels.push_back(e2[b]);
    for (int b = 0; b < 3; ++b)
        pixels.push_back(e3[b]);
    const float mixtures[5][3] = {
        {0.5f, 0.5f, 0.0f}, {0.5f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.5f},
        {0.33f, 0.33f, 0.34f}, {0.2f, 0.3f, 0.5f},
    };
    for (const auto &m : mixtures)
        for (int b = 0; b < 3; ++b)
            pixels.push_back(m[b]);
    return pixels;
}

} // namespace

TEST_CASE("PPI extracts the simplex vertices as endmembers", "[endmember][kernel]")
{
    const std::vector<float> pixels = simplexData();
    const size_t count = 8;

    EndmemberExtraction::EndmemberResult result;
    QString err;
    REQUIRE(EndmemberExtraction::pixelPurityIndex(pixels.data(), count, 3, 3, 500,
                                                  &result, &err));
    REQUIRE(result.endmembers.size() == 9);
    REQUIRE(result.endmemberIndices.size() == 3);
    REQUIRE(result.ppiCounts.size() == count);

    // The three pure vertices (pixels 0, 1, 2) are selected.
    const std::set<int> indices(result.endmemberIndices.begin(), result.endmemberIndices.end());
    CHECK(indices == std::set<int>({0, 1, 2}));

    // The extracted spectra match the vertices (each band has exactly one 1.0).
    for (int e = 0; e < 3; ++e)
    {
        float sum = 0.0f;
        for (int b = 0; b < 3; ++b)
            sum += result.endmembers[static_cast<size_t>(e) * 3 + b];
        CHECK(sum == Approx(1.0f).margin(1e-5f));
    }
}

TEST_CASE("PPI guards invalid arguments", "[endmember][kernel]")
{
    const std::vector<float> pixels = simplexData();
    EndmemberExtraction::EndmemberResult result;
    QString err;

    CHECK_FALSE(EndmemberExtraction::pixelPurityIndex(pixels.data(), 0, 3, 3, 500,
                                                      &result, &err));
    CHECK_FALSE(EndmemberExtraction::pixelPurityIndex(pixels.data(), 8, 3, 0, 500,
                                                      &result, &err));
    CHECK_FALSE(EndmemberExtraction::pixelPurityIndex(pixels.data(), 8, 3, 9, 500,
                                                      &result, &err));
    CHECK_FALSE(EndmemberExtraction::pixelPurityIndex(pixels.data(), 8, 3, 3, 8,
                                                      &result, &err));
}

TEST_CASE("rs:endmember_extraction returns endmember spectra in JSON", "[operators][rs][endmember]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/input.tif";

    const std::vector<float> pixels = simplexData();
    std::vector<std::vector<float>> bands(3, std::vector<float>(8, 0.0f));
    for (size_t p = 0; p < 8; ++p)
        for (int b = 0; b < 3; ++b)
            bands[b][p] = pixels[p * 3 + b];
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, 8, 1, bands, gt, "EPSG:32648", &err));

    auto op = RSOperatorRegistry::instance().create("rs:endmember_extraction");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["nEndmembers"] = 3;

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    REQUIRE(result["endmembers"].isArray());
    REQUIRE(result["endmembers"].size() == 3);
    for (Json::ArrayIndex e = 0; e < 3; ++e)
    {
        REQUIRE(result["endmembers"][e].isArray());
        REQUIRE(result["endmembers"][e].size() == 3);
    }
    REQUIRE(result["indices"].isArray());
    REQUIRE(result["indices"].size() == 3);
    REQUIRE(result["ppiCounts"].isArray());
    REQUIRE(result["ppiCounts"].size() == 8);
}
