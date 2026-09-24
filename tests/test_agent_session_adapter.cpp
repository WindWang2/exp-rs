// tests/test_agent_session_adapter.cpp
//
// R3 agent-ops live driver: the production verifier seam. The harness
// verifier must pass a REAL raster, fail missing/corrupt artifacts, and —
// end-to-end through the loop — make it impossible for fake executor
// artifacts to be reported as delivered success.

#include <catch2/catch_test_macros.hpp>

// agent_loop/fake_seams first: Qt's `slots` macro would break FakeScenario.
#include "agent_loop/fake_seams.h"
#include "agent_loop/scientific_agent_session.h"
#include "agent/tools/agent_session_adapter.h"
#include "agent/harness/harness_verification.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>

#include <filesystem>
#include <string>

using namespace sicnu::agent_loop;

namespace {

ScientificAgentSession::Dependencies makeDeps(FakeSeams &seams)
{
    return {&seams.dataProvider(), &seams.planner(), &seams.preflight(), &seams.executor(),
            &seams.verifier(), &seams.diagnoser()};
}

SessionRunRequest ndviRequest()
{
    SessionRunRequest r;
    r.goal = "compute NDVI for the scene";
    r.intent = "ndvi";
    return r;
}

/// Writes a small real GeoTIFF; returns its path (empty on failure).
std::string writeRealRaster(const std::string &path)
{
    std::vector<std::vector<float>> bands(1, std::vector<float>(16, 1.0f));
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    const bool ok = sicnu::processing::writeGdalOutput(QString::fromStdString(path), 4, 4,
                                                       bands, gt,
                                                       QStringLiteral("EPSG:4326"), &err);
    return ok ? path : std::string();
}

} // namespace

TEST_CASE("harness verifier adapter verifies a real raster", "[agent_ops][adapter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string raster = writeRealRaster(dir.filePath("out.tif").toStdString());
    REQUIRE_FALSE(raster.empty());

    sicnu::agent::HarnessVerifier verifier;
    PlanDraft plan;
    plan.valid = true;
    Json::Value output(Json::objectValue);
    output["kind"] = "raster";
    plan.outputs.append(output);

    ExecutionOutcome good;
    good.finished = true;
    good.succeeded = true;
    good.artifacts = {raster};
    auto report = verifier.verify(plan, good);
    REQUIRE(report.artifacts.size() == 1);
    REQUIRE(report.verdict() == "PASS");

    // A missing artifact FAILS — the verifier never grades absent outputs.
    ExecutionOutcome ghost;
    ghost.finished = true;
    ghost.succeeded = true;
    ghost.artifacts = {dir.filePath("missing.tif").toStdString()};
    report = verifier.verify(plan, ghost);
    REQUIRE(report.verdict() == "FAIL");

    // One good + one missing artifact still FAILs (any FAIL -> FAIL).
    ExecutionOutcome mixed;
    mixed.finished = true;
    mixed.succeeded = true;
    mixed.artifacts = {raster, dir.filePath("missing2.tif").toStdString()};
    report = verifier.verify(plan, mixed);
    REQUIRE(report.verdict() == "FAIL");
}

TEST_CASE("fake executor artifacts never pass a real verifier", "[agent_ops][adapter]")
{
    // The fake executor reports a nonexistent artifact path. With the
    // PRODUCTION verifier in the loop the session must refuse to deliver —
    // this is the never-claims-success contract against fake evidence.
    FakeScenario scenario;
    scenario.execution = {ExecutionScript{true, "", {"/nonexistent/run42/out.tif"}}};
    scenario.verification = {VerifyScript{"PASS", ""}}; // the fake verifier WOULD pass
    scenario.diagnosis = {DiagnoseScript{"CRS_MISMATCH", {}}}; // no proposals -> refuse
    FakeSeams seams(scenario);
    auto deps = makeDeps(seams);

    sicnu::agent::HarnessVerifier verifier;
    deps.verifier = &verifier; // production seam replaces the fake

    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    ScientificAgentSession session(policy, deps, {}, "sess-adapter");
    auto result = session.run(ndviRequest());

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.terminalState == terminal_states::kRefused);
    REQUIRE(result.stopReason == stop_reasons::kOutputInvalid);
    REQUIRE(result.summary.verificationVerdict == "FAIL");
}
