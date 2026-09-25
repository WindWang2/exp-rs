// tests/test_agent_ops_driver.cpp
//
// OpsDriver: per-session bookkeeping, journal reconciliation, the seam
// gate, and the cancel disarm round-trip — all over the shared session
// surface contract (fake seams; the loop stays the only state machine).

#include <catch2/catch_test_macros.hpp>

#include "agent_loop/fake_seams.h"
#include "agent_ops/ops_driver.h"
#include "agent_ops/live_session_recorder.h"
#include "agent_ops/session_surface.h"

#include <filesystem>
#include <string>

using namespace sicnu::agent_ops;
using namespace sicnu::agent_loop;
namespace fs = std::filesystem;

namespace {

std::string uniqueTemp(const char *tag)
{
    auto base = fs::temp_directory_path() / (std::string("ops_driver_") + tag);
    fs::create_directories(base);
    return base.string();
}

OpsDriver::Options wiredOptions(FakeSeams &seams)
{
    OpsDriver::Options options;
    options.deps.seams = {&seams.dataProvider(), &seams.planner(), &seams.preflight(),
                          &seams.executor(), &seams.verifier(), &seams.diagnoser()};
    return options;
}

Json::Value runArgs(const std::string &goal = "compute NDVI for the scene")
{
    Json::Value args(Json::objectValue);
    args["goal"] = goal;
    args["intent"] = "ndvi";
    return args;
}

} // namespace

TEST_CASE("ops driver refuses run before launch when seams are unavailable",
          "[ops_driver]")
{
    // The default CLI host injects NO production science seams yet: run
    // must fail closed with the missing names — never launch a fake session.
    OpsDriver driver(OpsDriver::Options{});

    auto doc = driver.apply("run", runArgs());
    REQUIRE_FALSE(doc["ok"].asBool());
    REQUIRE(doc["error"].asString() == "SEAMS_UNAVAILABLE");
    const std::string missing = doc["missing_seams"].asString();
    REQUIRE(missing.find("data") != std::string::npos);
    REQUIRE(missing.find("planner") != std::string::npos);
    REQUIRE(missing.find("preflight") != std::string::npos);
    REQUIRE(missing.find("executor") != std::string::npos);
    REQUIRE(missing.find("verifier") != std::string::npos);

    // A dry-run does not need executor/verifier: fewer names missing.
    Json::Value dry = runArgs();
    dry["mode"] = "dry_run";
    auto dryDoc = driver.apply("run", dry);
    REQUIRE_FALSE(dryDoc["ok"].asBool());
    REQUIRE(dryDoc["missing_seams"].asString().find("executor") == std::string::npos);
    REQUIRE(dryDoc["missing_seams"].asString().find("planner") != std::string::npos);

    // Unknown mode is a typed argument error.
    Json::Value bogus = runArgs();
    bogus["mode"] = "yolo";
    REQUIRE(driver.apply("run", bogus)["error"].asString() == "UNKNOWN_MODE");

    // Discovery and reconcile stay live without seams.
    auto actions = driver.apply("actions");
    REQUIRE(actions["actions"].isArray());
    REQUIRE(actions["schema"].asString() == kOpsDriverSchema);
    REQUIRE(driver.apply("reconcile", {})["error"].asString() == "MISSING_ARGS");
    Json::Value recon;
    recon["journal_directory"] = uniqueTemp("nosuch");
    recon["session_id"] = "sess-none";
    auto reconDoc = driver.apply("reconcile", recon);
    REQUIRE(reconDoc["ok"].asBool());
    REQUIRE(reconDoc["reconcile"]["reason_code"].asString() == "CORRUPTED_OR_MISSING_JOURNAL");
}

TEST_CASE("ops driver keeps per-session status/timeline/export", "[ops_driver]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    // Two sessions on one driver.
    auto first = driver.apply("run", runArgs("first goal"));
    REQUIRE(first["ok"].asBool());
    const std::string firstId = first["session_id"].asString();

    auto second = driver.apply("run", runArgs("second goal"));
    REQUIRE(second["ok"].asBool());
    const std::string secondId = second["session_id"].asString();
    REQUIRE(firstId != secondId);

    // Default status follows the most recent session...
    auto latest = driver.apply("status");
    REQUIRE(latest["session_id"].asString() == secondId);
    REQUIRE(latest["delivery"]["goal"].asString() == "second goal");

    // ...and an explicit id returns THAT session's data, not the last run.
    Json::Value named;
    named["session_id"] = firstId;
    auto namedStatus = driver.apply("status", named);
    REQUIRE(namedStatus["session_id"].asString() == firstId);
    REQUIRE(namedStatus["delivery"]["goal"].asString() == "first goal");

    auto namedExport = driver.apply("export", named);
    REQUIRE(namedExport["schema"].asString() == "sicnu.agent_ops.capsule_export/v1");
    REQUIRE(namedExport["goal"].asString() == "first goal");

    auto namedTimeline = driver.apply("timeline", named);
    REQUIRE(namedTimeline["projection"].isObject());

    // Unknown ids are typed, not silently mapped to another session.
    Json::Value ghost;
    ghost["session_id"] = "sess-unknown";
    REQUIRE(driver.apply("status", ghost)["error"].asString() == "UNKNOWN_SESSION");
    REQUIRE(driver.apply("timeline", ghost)["error"].asString() == "UNKNOWN_SESSION");
    REQUIRE(driver.apply("export", ghost)["error"].asString() == "UNKNOWN_SESSION");
    REQUIRE(driver.apply("status")["driver_schema"].asString() == kOpsDriverSchema);
}

TEST_CASE("ops driver wire parity with the session surface envelope", "[ops_driver][parity]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    auto doc = driver.apply("run", runArgs());
    // The driver document IS the surface document (same typed fields) plus
    // the driver schema tag — CLI/MCP/panel cannot drift from the core.
    REQUIRE(doc["schema"].asString() == kSessionSurfaceSchema);
    REQUIRE(doc["tool"].asString() == kSessionSurfaceTool);
    REQUIRE(doc["driver_schema"].asString() == kOpsDriverSchema);
    for (const char *field : {"ok", "session_id", "outcome", "stop_reason", "projection",
                              "delivery", "error", "reconcile"})
        INFO("field: " << field);
    REQUIRE(doc.isMember("ok"));
    REQUIRE(doc.isMember("session_id"));
    REQUIRE(doc.isMember("outcome"));
    REQUIRE(doc.isMember("stop_reason"));
    REQUIRE(doc.isMember("projection"));
    REQUIRE(doc.isMember("delivery"));
    REQUIRE(doc.isMember("error"));
    REQUIRE(doc.isMember("reconcile"));
}

TEST_CASE("ops driver cancel/clear_cancel round-trip over the wire", "[ops_driver]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    REQUIRE(driver.apply("cancel")["ok"].asBool());
    auto aborted = driver.apply("run", runArgs());
    REQUIRE_FALSE(aborted["ok"].asBool());
    REQUIRE(aborted["stop_reason"].asString() == "CANCELLED");
    REQUIRE(seams.executorFake().beginCount() == 0);

    REQUIRE(driver.apply("clear_cancel")["ok"].asBool());
    auto relaunched = driver.apply("run", runArgs());
    REQUIRE(relaunched["ok"].asBool());
}

TEST_CASE("ops driver refuses a blind re-run of a session with submitted runs",
          "[ops_driver][crash]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OpsDriver driver(wiredOptions(seams));

    const std::string dir = uniqueTemp("dup-guard");
    // The journal of a session killed after submit (execute stage + run
    // decision journaled by the loop's own wire shape).
    sicnu::agent_loop::SessionJournal crashed("sess-dup-guard");
    REQUIRE(crashed.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(crashed.append("stage_enter", "plan_request", {}, 2));
    REQUIRE(crashed.append("stage_enter", "execute", {}, 3));
    sicnu::agent_loop::DecisionRecord runDecision;
    runDecision.decisionId = "dec-1";
    runDecision.sessionId = "sess-dup-guard";
    runDecision.stage = "execute";
    runDecision.reason = "plan submitted through the executor seam";
    runDecision.selected["action"] = "run";
    runDecision.inputs["run_id"] = "run-guarded";
    REQUIRE(crashed.append("decision", "execute", {}, 4, runDecision));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(crashed, dir, &err));

    // A blind re-run naming that session is refused with the evidence
    // attached — restart as a NEW session or reconcile and decide.
    Json::Value blind = runArgs();
    blind["journal_directory"] = dir;
    blind["session_id"] = "sess-dup-guard";
    auto refused = driver.apply("run", blind);
    REQUIRE_FALSE(refused["ok"].asBool());
    REQUIRE(refused["error"].asString() == "SUBMITTED_RUN_EXISTS");
    REQUIRE(refused["reconcile"]["submitted_run_ids"][0].asString() == "run-guarded");
    REQUIRE(seams.executorFake().beginCount() == 0);

    // reconcile exposes the same verdict without any launch attempt.
    Json::Value recon;
    recon["journal_directory"] = dir;
    recon["session_id"] = "sess-dup-guard";
    auto reconDoc = driver.apply("reconcile", recon);
    REQUIRE(reconDoc["reconcile"]["duplicate_submit_risk"].asBool());
    REQUIRE(reconDoc["reconcile"]["resumable"].asBool() == false);
    REQUIRE(reconDoc["reconcile"]["reason_code"].asString() == "RESUME_PAST_PLAN_SEAM");

    // A session WITHOUT submitted runs never trips the guard: the parked
    // pre-plan journal is resumable, not blocked.
    sicnu::agent_loop::SessionJournal parked("sess-parked");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(rec.persistJournal(parked, dir, &err));
    Json::Value resumeArgs = runArgs();
    resumeArgs["journal_directory"] = dir;
    resumeArgs["session_id"] = "sess-parked";
    auto resumed = driver.apply("resume", resumeArgs);
    REQUIRE(resumed["ok"].asBool());
}
