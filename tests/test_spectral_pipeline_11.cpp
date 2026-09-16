// test_spectral_pipeline_11.cpp — Spectral Intelligence 11.0 end-to-end
// artifact chain over the real operator registry:
//
//   synthetic cube -> rs:endmember_extraction (PPI, endmembersOut table)
//     -> rs:endmember_analysis (reduce + SAM matrix -> derived table with
//        inherited provenance/license + digests)
//     -> rs:sparse_unmixing (endmembersRef chain consumer)
//   plus rs:local_rx_anomaly (score + quality rasters) and
//   rs:spectral_similarity (hybrid classification) on the same cube.
//
// The chain exercises the placeholder-path contract (artifacts travel as
// file paths between steps) and asserts provenance custody at every hop.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/spectral_table.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_pipeline_11";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

constexpr int kWidth = 40;
constexpr int kHeight = 40;
constexpr int kBands = 6;

/// Three linearly independent material spectra over 6 bands; pixels are
/// deterministic mixtures. An extreme outlier goes to (31, 5).
std::vector<std::vector<float>> buildCube()
{
    const std::vector<float> veg = {0.05f, 0.08f, 0.20f, 0.45f, 0.35f, 0.15f};
    const std::vector<float> soil = {0.40f, 0.42f, 0.40f, 0.35f, 0.30f, 0.25f};
    const std::vector<float> water = {0.02f, 0.03f, 0.05f, 0.04f, 0.03f, 0.02f};

    std::vector<std::vector<float>> bands(kBands, std::vector<float>(kWidth * kHeight));
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            // Three deterministic regions: left / middle / right thirds.
            const double w1 = x < kWidth / 3 ? 0.8 : 0.2;
            const double w2 = x < kWidth / 3 ? 0.1 : (x < 2 * kWidth / 3 ? 0.7 : 0.1);
            const double w3 = 1.0 - w1 - w2;
            // Deterministic checkerboard perturbation: real scenes always
            // carry background variance — a piecewise-constant image would
            // make every window covariance exactly zero (all its pixels
            // identical) and the kernel would honestly refuse to score
            // anything (singular background, no information).
            const double sign = ((x + y) % 2 == 0) ? 1.0 : -1.0;
            for (int b = 0; b < kBands; ++b)
            {
                const double v = w1 * veg[b] + w2 * soil[b] + w3 * water[b]
                                 + sign * 0.01 * (1.0 + 0.1 * b);
                bands[b][y * kWidth + x] = static_cast<float>(v);
            }
        }
    }
    // A strong anomaly that local RX must flag.
    for (int b = 0; b < kBands; ++b)
        bands[b][5 * kWidth + 31] = 2.5f + 0.1f * b;
    return bands;
}

Json::Value runOperator(const std::string &id, const Json::Value &params)
{
    auto op = RSOperatorRegistry::instance().create(id);
    REQUIRE(op != nullptr);
    RSOperatorContext ctx;
    return op->run(params, ctx);
}

} // namespace

TEST_CASE("Spectral 11 chain: PPI table -> analysis -> sparse unmixing keeps provenance",
          "[spectral11][operators][artifact]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/cube.tif";

    auto bands = buildCube();
    const std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, kWidth, kHeight, bands, gt, "EPSG:32648", &err));

    // Step 1: PPI extraction -> endmembersOut table artifact.
    const QString ppiTable = tmp.path() + "/ppi_endmembers.json";
    Json::Value ppiParams(Json::objectValue);
    ppiParams["input"] = inputPath.toStdString();
    ppiParams["nEndmembers"] = 4;
    ppiParams["projections"] = 64;
    ppiParams["endmembersOut"] = ppiTable.toStdString();
    Json::Value ppiResult = runOperator("rs:endmember_extraction", ppiParams);
    REQUIRE(QFile::exists(ppiTable));
    REQUIRE(ppiResult.isMember("endmembersArtifact"));
    CHECK(ppiResult["endmembersArtifact"].asString() == ppiTable.toStdString());

    SpectralTable::Table ppiLoaded;
    REQUIRE(SpectralTable::loadValidated(ppiTable, &ppiLoaded, &err));
    REQUIRE(ppiLoaded.count() == 4);
    CHECK(ppiLoaded.provenance.sourceOperator == "rs:endmember_extraction");
    CHECK_FALSE(ppiLoaded.digestHex.isEmpty());

    // Step 2: analysis -> reduced derived table with inherited provenance.
    const QString reducedTable = tmp.path() + "/reduced_endmembers.json";
    Json::Value analysisParams(Json::objectValue);
    analysisParams["endmembersRef"] = ppiTable.toStdString();
    analysisParams["output"] = reducedTable.toStdString();
    analysisParams["mergeAngleDegrees"] = 2.0;
    analysisParams["angleMatrix"] = true;
    Json::Value analysisResult = runOperator("rs:endmember_analysis", analysisParams);
    REQUIRE(QFile::exists(reducedTable));
    CHECK(analysisResult["inputRows"].asInt() == 4);
    REQUIRE(analysisResult["outputRows"].asInt() >= 1);
    CHECK(analysisResult.isMember("angleMatrix"));
    CHECK(analysisResult["inputDigest"].asString() == ppiLoaded.digestHex.toStdString());

    SpectralTable::Table reducedLoaded;
    REQUIRE(SpectralTable::loadValidated(reducedTable, &reducedLoaded, &err));
    CHECK(reducedLoaded.provenance.sourceOperator == "rs:endmember_analysis");
    CHECK(reducedLoaded.provenance.derived);
    CHECK(reducedLoaded.provenance.sourceInput == ppiTable);
    // Digest custody: the analysis result echoes the input digest and the
    // derived table carries a digest consistent with its own file. (The
    // digest MAY equal the input's when reduction passes rows through
    // unchanged — content identity, not a defect.)
    CHECK_FALSE(reducedLoaded.digestHex.isEmpty());
    CHECK(reducedLoaded.digestHex == analysisResult["outputDigest"].asString());

    // Step 3: sparse unmixing consumes the reduced table through the chain.
    const QString abundancePath = tmp.path() + "/abundance.tif";
    Json::Value sparseParams(Json::objectValue);
    sparseParams["input"] = inputPath.toStdString();
    sparseParams["output"] = abundancePath.toStdString();
    sparseParams["endmembersRef"] = reducedTable.toStdString();
    sparseParams["lambda"] = 0.01;
    sparseParams["sumToOnePenalty"] = 100.0;
    Json::Value sparseResult = runOperator("rs:sparse_unmixing", sparseParams);
    REQUIRE(QFile::exists(abundancePath));
    const int atoms = sparseResult["atoms"].asInt();
    REQUIRE(atoms == reducedLoaded.count());

    GdalDatasetWrapper abundanceDs;
    REQUIRE(abundanceDs.open(abundancePath));
    CHECK(abundanceDs.bandCount() == atoms);
    std::vector<float> band0(kWidth * kHeight);
    REQUIRE(abundanceDs.readBandData(1, band0.data(), kWidth, kHeight));
    for (size_t p = 0; p < band0.size(); ++p)
        CHECK(band0[p] >= -1e-4f); // non-negativity on a real raster
    abundanceDs.close();

    // Provenance echo: the sparse result identifies the consumed table.
    CHECK(sparseResult["endmemberSource"].asString().find("spectral table")
          != std::string::npos);
    CHECK(sparseResult["convergedFraction"].asDouble() >= 0.0);
}

TEST_CASE("Local RX operator: anomaly flagged, quality plane honest, provenance stamped",
          "[spectral11][operators][localrx]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/cube.tif";
    const QString scorePath = tmp.path() + "/localrx.tif";
    const QString qualityPath = tmp.path() + "/localrx_quality.tif";

    auto bands = buildCube();
    const std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, kWidth, kHeight, bands, gt, "EPSG:32648", &err));

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = scorePath.toStdString();
    params["qualityOut"] = qualityPath.toStdString();
    Json::Value result = runOperator("rs:local_rx_anomaly", params);

    REQUIRE(QFile::exists(scorePath));
    REQUIRE(QFile::exists(qualityPath));
    CHECK(result["scoredPixels"].asUInt64() + result["unscoredPixels"].asUInt64()
          == static_cast<Json::UInt64>(kWidth) * kHeight);
    CHECK(result["covariance"].asString() == "full");

    GdalDatasetWrapper scoreDs;
    REQUIRE(scoreDs.open(scorePath));
    CHECK(scoreDs.bandCount() == 1);
    std::vector<float> scores(kWidth * kHeight);
    REQUIRE(scoreDs.readBandData(1, scores.data(), kWidth, kHeight));
    const int anomalyCell = 5 * kWidth + 31;
    float best = -1.0f;
    int bestCell = -1;
    for (size_t p = 0; p < scores.size(); ++p)
        if (std::isfinite(scores[p]) && scores[p] > best)
        {
            best = scores[p];
            bestCell = static_cast<int>(p);
        }
    CHECK(bestCell == anomalyCell);
    CHECK(best > 10.0f);

    GdalDatasetWrapper qualityDs;
    REQUIRE(qualityDs.open(qualityPath));
    std::vector<float> quality(kWidth * kHeight);
    REQUIRE(qualityDs.readBandData(1, quality.data(), kWidth, kHeight));
    // Interior pixel: 5x5 window minus 3x3 guard = 16 background samples.
    CHECK(quality[12 * kWidth + 20] == Approx(16.0f).margin(1e-3));
}

TEST_CASE("Hybrid similarity operator labels regions correctly", "[spectral11][operators][hybrid]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/cube.tif";
    const QString labelPath = tmp.path() + "/labels.tif";

    auto bands = buildCube();
    const std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    REQUIRE(writeGdalOutput(inputPath, kWidth, kHeight, bands, gt, "EPSG:32648", &err));

    // Inline references: vegetation ramp, soil flat, water dark (class order).
    Json::Value refs(Json::arrayValue);
    const auto pushRef = [&refs](const std::vector<float> &spectrum)
    {
        Json::Value s(Json::arrayValue);
        for (float v : spectrum)
            s.append(v);
        refs.append(s);
    };
    pushRef({0.05f, 0.08f, 0.20f, 0.45f, 0.35f, 0.15f});
    pushRef({0.40f, 0.42f, 0.40f, 0.35f, 0.30f, 0.25f});
    pushRef({0.02f, 0.03f, 0.05f, 0.04f, 0.03f, 0.02f});

    Json::Value params(Json::objectValue);
    params["input"] = inputPath.toStdString();
    params["output"] = labelPath.toStdString();
    params["refs"] = refs;
    Json::Value result = runOperator("rs:spectral_similarity", params);

    REQUIRE(QFile::exists(labelPath));
    CHECK(result["refs"].asInt() == 3);
    CHECK(result["form"].asString() == "product_normalized");

    GdalDatasetWrapper labelDs;
    REQUIRE(labelDs.open(labelPath));
    std::vector<float> labels(kWidth * kHeight);
    REQUIRE(labelDs.readBandData(1, labels.data(), kWidth, kHeight));
    // Labels are 0-based class ids (master's sam_classify convention):
    // left third -> class 0 (vegetation), middle -> class 1 (soil).
    CHECK(labels[10 * kWidth + 5] == Approx(0.0f).margin(1e-6));
    CHECK(labels[10 * kWidth + 20] == Approx(1.0f).margin(1e-6));
    // The extreme outlier stays labelled but scores lowest; every valid
    // pixel must carry a class id in [0, refCount).
    for (size_t p = 0; p < labels.size(); ++p)
    {
        if (static_cast<int>(p) == 5 * kWidth + 31)
            continue;
        CHECK(labels[p] >= -0.5f);
        CHECK(labels[p] <= 2.5f);
    }
}
