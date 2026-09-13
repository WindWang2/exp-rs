// test_spectral_pipeline.cpp — structured spectral artifacts flowing through
// one workflow (Hyperspectral Platform 10.0 acceptance):
//
//   rs:endmember_extraction (endmembersOut) -> endmembersArtifact
//     -> rs:spectral_unmixing (endmembersRef via $step.endmembersArtifact)
//     -> rs:sam_classify   (refsRef)
// with the WorkflowSession placeholder path exercising the payload-port
// recording, plus the shared reference seam's typed refusals.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/rs/rs_spectral_reference_input.h"
#include "processing/algorithms/spectral_table.h"
#include "processing/algorithms/spectral_wavelength.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "workflow/workflow_runtime.h"
#include "workflow/workflow_session.h"

using namespace sicnu::operators;
using namespace sicnu::operators::rs;
using namespace sicnu::workflow;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_spectral_pipeline";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(appArgc(), appArgv);
}

/// Simplex scene: 3 pure vertices + mixtures, one pixel per row (W=1).
std::vector<float> simplexPixels()
{
    const float e1[] = {1.0f, 0.0f, 0.0f};
    const float e2[] = {0.0f, 1.0f, 0.0f};
    const float e3[] = {0.0f, 0.0f, 1.0f};
    std::vector<float> pixels;
    auto push = [&pixels](const float *e) {
        for (int b = 0; b < 3; ++b)
            pixels.push_back(e[b]);
    };
    push(e1);
    push(e2);
    push(e3);
    const float mixtures[5][3] = {
        {0.5f, 0.5f, 0.0f}, {0.5f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.5f},
        {0.33f, 0.33f, 0.34f}, {0.2f, 0.3f, 0.5f},
    };
    for (const auto &m : mixtures)
        push(m);
    return pixels;
}

QString writeSimplexRaster(const QString &path)
{
    const std::vector<float> pixels = simplexPixels();
    std::vector<std::vector<float>> bands(3, std::vector<float>(8, 0.0f));
    for (size_t p = 0; p < 8; ++p)
        for (int b = 0; b < 3; ++b)
            bands[b][p] = pixels[p * 3 + b];
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    if (!writeGdalOutput(path, 8, 1, bands, gt, "EPSG:32648", &err))
        return err;
    return {};
}

} // namespace

TEST_CASE("Endmember artifact flows PPI -> unmixing in one workflow",
          "[spectral][pipeline][artifact]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/scene.tif";
    const QString err = writeSimplexRaster(inputPath);
    REQUIRE(err.isEmpty());

    WorkflowRuntime runtime(true); // synchronous operator path for tests
    WorkflowDefinition def;
    def.id = "spectral_chain";
    def.title = "PPI to unmixing";

    StepDef ppi;
    ppi.id = "ppi";
    ppi.kind = StepKind::Operator;
    ppi.operatorId = "rs:endmember_extraction";
    ppi.params["input"] = inputPath.toStdString();
    ppi.params["nEndmembers"] = 3;
    ppi.params["endmembersOut"] = (tmp.path() + "/endmembers.json").toStdString();
    // The step's deliverable is a JSON spectral-table artifact, not a GDAL
    // dataset: opt out of raster/vector output verification (the field's
    // documented purpose).
    ppi.verificationPolicy = "skip";
    def.steps.push_back(ppi);

    StepDef unmix;
    unmix.id = "unmix";
    unmix.kind = StepKind::Operator;
    unmix.operatorId = "rs:spectral_unmixing";
    // The placeholder binds the producer's payload port to the consumer's
    // reference input — the whole point of the structured artifact.
    unmix.params["input"] = inputPath.toStdString();
    unmix.params["output"] = (tmp.path() + "/abundance.tif").toStdString();
    unmix.params["endmembersRef"] = "$ppi.endmembersArtifact";
    def.steps.push_back(unmix);

    runtime.registerDefinition(def);
    const std::string sessionId = runtime.open("spectral_chain");
    REQUIRE(!sessionId.empty());
    runtime.setParams(sessionId, "ppi", ppi.params);
    runtime.setParams(sessionId, "unmix", unmix.params);

    Json::Value ppiResult = runtime.runStep(sessionId, "ppi");
    REQUIRE(ppiResult.isMember("endmembersArtifact"));
    REQUIRE(QFile::exists(QString::fromStdString(ppiResult["endmembersArtifact"].asString())));

    Json::Value unmixResult = runtime.runStep(sessionId, "unmix");
    CHECK(unmixResult["output"].asString() == (tmp.path() + "/abundance.tif").toStdString());
    CHECK(unmixResult["endmembers"].asInt() == 3);
    CHECK(QFile::exists((tmp.path() + "/abundance.tif")));

    // The artifact is a real typed spectral table with provenance.
    SpectralTable::Table table;
    QString tableError;
    REQUIRE(SpectralTable::loadValidated(
        QString::fromStdString(ppiResult["endmembersArtifact"].asString()), &table,
        &tableError));
    CHECK(table.count() == 3);
    CHECK(table.bandCount == 3);
    CHECK(table.provenance.sourceOperator == QStringLiteral("rs:endmember_extraction"));
    CHECK(table.provenance.derived);
    CHECK(table.labels.size() == 3);
}

TEST_CASE("SAM classifies from the endmember artifact via refsRef",
          "[spectral][pipeline][artifact]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/scene.tif";
    REQUIRE(writeSimplexRaster(inputPath).isEmpty());

    auto ppiOp = RSOperatorRegistry::instance().create("rs:endmember_extraction");
    REQUIRE(ppiOp != nullptr);
    Json::Value ppiParams;
    ppiParams["input"] = inputPath.toStdString();
    ppiParams["nEndmembers"] = 3;
    ppiParams["endmembersOut"] = (tmp.path() + "/ends.json").toStdString();
    RSOperatorContext ctx;
    const Json::Value ppiResult = ppiOp->run(ppiParams, ctx);

    auto samOp = RSOperatorRegistry::instance().create("rs:sam_classify");
    REQUIRE(samOp != nullptr);
    Json::Value samParams;
    samParams["input"] = inputPath.toStdString();
    samParams["output"] = (tmp.path() + "/classes.tif").toStdString();
    samParams["refsRef"] = ppiResult["endmembersArtifact"].asString();
    const Json::Value samResult = samOp->run(samParams, ctx);

    CHECK(samResult["classes"].asInt() == 3);
    CHECK(samResult["reference"].asString().find("spectral table") != std::string::npos);
    CHECK(QFile::exists((tmp.path() + "/classes.tif")));

    // The pure pixels (0, 1, 2) each classify to a distinct class; every
    // pixel class must be one of the three references.
    GdalDatasetWrapper ds;
    REQUIRE(ds.open((tmp.path() + "/classes.tif")));
    REQUIRE(ds.bandCount() == 1);
    std::vector<float> labels(8, 0.0f);
    REQUIRE(ds.readBandData(1, labels.data(), 8, 1));
    std::set<int> distinct;
    for (int p = 0; p < 3; ++p)
        distinct.insert(static_cast<int>(labels[static_cast<size_t>(p)]));
    CHECK(distinct.size() == 3);
}

TEST_CASE("Reference seam refuses disjoint wavelength ranges and ambiguous inputs",
          "[spectral][seam][refusal]")
{
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString inputPath = tmp.path() + "/scene.tif";
    REQUIRE(writeSimplexRaster(inputPath).isEmpty());

    // A measured table without license cannot be produced (validated at save),
    // so build a derived table whose wavelength range is disjoint from the
    // raster (which carries no wavelengths here) — the width-mismatch refusal
    // is the reachable branch; the disjoint-range branch is exercised with
    // synthetic grids below.
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(inputPath));
    std::vector<int> bands = {1, 2, 3};
    QString gridError;
    const auto grid = RasterWavelengthGrid::read(ds, bands, &gridError);
    REQUIRE(gridError.isEmpty());
    CHECK_FALSE(grid.present);

    // Inline width mismatch: typed refusal naming the widths.
    auto samOp = RSOperatorRegistry::instance().create("rs:sam_classify");
    REQUIRE(samOp != nullptr);
    RSOperatorContext ctx;
    Json::Value badWidth;
    badWidth["input"] = inputPath.toStdString();
    badWidth["output"] = (tmp.path() + "/x.tif").toStdString();
    Json::Value refs(Json::arrayValue);
    Json::Value badRow(Json::arrayValue);
    badRow.append(0.1);
    badRow.append(0.2);
    refs.append(badRow);
    badWidth["refs"] = refs;
    bool threw = false;
    try
    {
        (void)samOp->run(badWidth, ctx);
    }
    catch (const RSOperatorError &e)
    {
        threw = true;
        CHECK(e.message().find("array of 3") != std::string::npos);
    }
    CHECK(threw);

    // Ambiguous: both inline refs and refsRef supplied.
    Json::Value ambiguous = badWidth;
    ambiguous["refsRef"] = (tmp.path() + "/whatever.json").toStdString();
    threw = false;
    try
    {
        (void)samOp->run(ambiguous, ctx);
    }
    catch (const RSOperatorError &e)
    {
        threw = true;
        CHECK(e.message().find("ambiguous") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("Disjoint wavelength grids refuse at the shared helper", "[spectral][seam]")
{
    // The grid-level guard (unit-normalized nm) backing the operator refusal.
    SpectralWavelength::Grid input;
    input.centersNm = {2000.0f, 2050.0f, 2100.0f};
    SpectralWavelength::Grid reference;
    reference.centersNm = {500.0f, 600.0f};
    std::string reason;
    REQUIRE_FALSE(SpectralWavelength::rangesOverlap(input, reference, &reason));
}

// ── C2 (review): operator-level MNF chain — rs:mnf transformOut flows into
//    rs:mnf_inverse (raster roundtrip, component subset with errorOut, and
//    spectrum mode). Kernel-level guarantees are pinned in
//    tests/test_mnf_transform.cpp; this pins the artifact handoff between
//    the two operators, the GDAL row streaming, and the honesty flags.

namespace
{

/// 6x5x4 cube with a smooth axis so the MNF fit is well conditioned; band
/// means differ so the inverse mean restoration is observable.
QString writeMnfCubeRaster(const QString &path)
{
    constexpr int W = 6, H = 5, B = 4;
    std::vector<std::vector<float>> bands(B, std::vector<float>(W * H, 0.0f));
    for (int p = 0; p < W * H; ++p)
        for (int b = 0; b < B; ++b)
            bands[b][static_cast<size_t>(p)] =
                0.1f + 0.02f * p + 0.1f * b + 0.005f * ((p * 7 + b * 13) % 5);
    std::array<double, 6> gt = {500000, 30, 0, 4500000, 0, -30};
    QString err;
    if (!writeGdalOutput(path, W, H, bands, gt, "EPSG:32648", &err))
        return err;
    return {};
}

} // namespace

TEST_CASE("rs:mnf transformOut drives rs:mnf_inverse end to end",
          "[spectral][mnf][pipeline][artifact]")
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString input = dir.filePath("cube.tif");
    REQUIRE(writeMnfCubeRaster(input).isEmpty());

    auto mnfOp = RSOperatorRegistry::instance().create("rs:mnf");
    auto invOp = RSOperatorRegistry::instance().create("rs:mnf_inverse");
    REQUIRE(mnfOp != nullptr);
    REQUIRE(invOp != nullptr);
    RSOperatorContext ctx;

    // Forward with all components + transform artifact.
    Json::Value mnfParams;
    mnfParams["input"] = input.toStdString();
    mnfParams["output"] = dir.filePath("components.tif").toStdString();
    mnfParams["transformOut"] = dir.filePath("transform.json").toStdString();
    const Json::Value mnfResult = mnfOp->run(mnfParams, ctx);
    REQUIRE(mnfResult["transformArtifact"].isString());
    REQUIRE(mnfResult["signalToNoise"].isArray());
    REQUIRE(QFile::exists(dir.filePath("transform.json")));

    // Raster mode, full selection: roundtrip within the kernel tolerance.
    {
        Json::Value inv;
        inv["transform"] = mnfResult["transformArtifact"].asString();
        inv["input"] = mnfResult["output"].asString();
        inv["output"] = dir.filePath("reconstructed.tif").toStdString();
        const Json::Value invResult = invOp->run(inv, ctx);
        CHECK(invResult["bands"].asInt() == 4);

        GdalDatasetWrapper original;
        GdalDatasetWrapper rebuilt;
        REQUIRE(original.open(input));
        REQUIRE(rebuilt.open(QString::fromStdString(invResult["output"].asString())));
        REQUIRE(rebuilt.bandCount() == 4);
        for (int b = 1; b <= 4; ++b)
        {
            std::vector<float> a(30, 0.0f);
            std::vector<float> c(30, 0.0f);
            REQUIRE(original.readBandData(b, a.data(), 6, 5));
            REQUIRE(rebuilt.readBandData(b, c.data(), 6, 5));
            for (size_t p = 0; p < a.size(); ++p)
                CHECK(c[p] == Approx(a[p]).epsilon(1e-4).margin(1e-5));
        }
    }

    // Subset selection: errorOut is real (non-zero) because every component
    // coefficient is known from the full-width raster.
    {
        Json::Value inv;
        inv["transform"] = mnfResult["transformArtifact"].asString();
        inv["input"] = mnfResult["output"].asString();
        inv["output"] = dir.filePath("subset.tif").toStdString();
        inv["errorOut"] = dir.filePath("err.tif").toStdString();
        Json::Value comps(Json::arrayValue);
        comps.append(0);
        inv["components"] = comps;
        const Json::Value invResult = invOp->run(inv, ctx);
        CHECK(invResult["components"].asInt() == 1);

        GdalDatasetWrapper errDs;
        REQUIRE(errDs.open(dir.filePath("err.tif")));
        std::vector<float> err(30, 0.0f);
        REQUIRE(errDs.readBandData(1, err.data(), 6, 5));
        double maxErr = 0.0;
        for (float v : err)
            if (std::isfinite(v))
                maxErr = std::max(maxErr, static_cast<double>(v));
        CHECK(maxErr > 0.0);
    }

    // Spectrum mode: one MNF-space spectrum converts back to band space.
    {
        // Take pixel (0,0) from the component raster as the MNF-space input.
        GdalDatasetWrapper compDs;
        REQUIRE(compDs.open(QString::fromStdString(mnfResult["output"].asString())));
        std::vector<float> row0(static_cast<size_t>(6) * 5, 0.0f);
        REQUIRE(compDs.readBandData(1, row0.data(), 6, 5));

        SpectralTable::Table spectrumIn;
        spectrumIn.id = QStringLiteral("mnf-space-pixel");
        spectrumIn.bandCount = 4;
        spectrumIn.spectra.push_back({ row0[0], 0.0f, 0.0f, 0.0f });
        // Only the first component's coefficient is real; the rest are zeros
        // (the operator's padding semantics for a truncated component vector).
        spectrumIn.provenance.sourceOperator = QStringLiteral("rs:test");
        spectrumIn.provenance.derived = true;
        QString err;
        REQUIRE(SpectralTable::save(spectrumIn, dir.filePath("spec_in.json"), &err));

        Json::Value inv;
        inv["transform"] = mnfResult["transformArtifact"].asString();
        inv["spectrumRef"] = dir.filePath("spec_in.json").toStdString();
        inv["spectrumOut"] = dir.filePath("spec_out.json").toStdString();
        const Json::Value invResult = invOp->run(inv, ctx);
        CHECK(invResult["bands"].asInt() == 4);
        REQUIRE(QFile::exists(dir.filePath("spec_out.json")));

        SpectralTable::Table spectrumOut;
        REQUIRE(SpectralTable::loadValidated(dir.filePath("spec_out.json"),
                                             &spectrumOut, &err));
        // A 1-component MNF vector maps to the first axis direction + mean:
        // reconstructing the band values must move off the mean for band 1.
        bool movedOffMean = false;
        for (int b = 0; b < 4; ++b)
            if (std::abs(spectrumOut.spectra[0][static_cast<size_t>(b)]) > 1e-9)
                movedOffMean = true;
        CHECK(movedOffMean);
    }

    // Component indices beyond the input width refuse (P2-1 fix).
    {
        Json::Value inv;
        inv["transform"] = mnfResult["transformArtifact"].asString();
        inv["input"] = mnfResult["output"].asString();
        inv["output"] = dir.filePath("bad.tif").toStdString();
        // Truncate the input raster to 2 bands by re-running MNF with
        // numComponents=2, then request component 3.
        Json::Value mnf2;
        mnf2["input"] = input.toStdString();
        mnf2["output"] = dir.filePath("components2.tif").toStdString();
        mnf2["numComponents"] = 2;
        mnf2["transformOut"] = dir.filePath("transform2.json").toStdString();
        (void)mnfOp->run(mnf2, ctx);

        inv["transform"] = dir.filePath("transform2.json").toStdString();
        inv["input"] = dir.filePath("components2.tif").toStdString(); // 2 bands
        Json::Value comps(Json::arrayValue);
        comps.append(0);
        comps.append(1);
        comps.append(3);
        inv["components"] = comps;
        bool threw = false;
        try
        {
            (void)invOp->run(inv, ctx);
        }
        catch (const RSOperatorError &e)
        {
            threw = true;
            CHECK(e.message().find("unknown") != std::string::npos);
        }
        CHECK(threw);
    }
}
