// tests/test_agent_ops_core.cpp
//
// Agent Operations & Recovery Control Center — core closed-loop suites on
// fake seams + real-adjacent journal/trace/autonomy/repair schema seams.
//

#include <catch2/catch_test_macros.hpp>

#include "agent_loop/fake_seams.h"
#include "agent_loop/scientific_agent_session.h"
#include "agent_ops/autonomy_ops_gate.h"
#include "agent_ops/benchmark_adapter.h"
#include "agent_ops/delivery_assembler.h"
#include "agent_ops/diagnostic_bridge.h"
#include "agent_ops/live_session_recorder.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/ops_projection.h"
#include "agent_ops/recovery_bridge.h"
#include "agent_ops/resume_reconciler.h"
#include "agent_ops/secret_redactor.h"
#include "agent_ops/session_surface.h"
#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agentbench/suite.h"
#include "agentbench/trace.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace sicnu::agent_ops;
using namespace sicnu::agent_loop;
namespace fs = std::filesystem;

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

RepairProposal proposal(const std::string &ruleId, const std::string &risk = "shape_preserving")
{
    RepairProposal p;
    p.ruleId = ruleId;
    p.riskClass = risk;
    p.operatorId = "rs:" + ruleId;
    p.rationale = "test";
    return p;
}

std::string uniqueTemp(const char *tag)
{
    auto base = fs::temp_directory_path() / (std::string("agent_ops_") + tag);
    fs::create_directories(base);
    return base.string();
}

sicnu::agent::autonomy::AutonomyPolicy restrictiveL0()
{
    using namespace sicnu::agent::autonomy;
    AutonomyPolicy p = AutonomyPolicyHolder::researchDefaultPolicy();
    p.hasLevel = true;
    p.level = AutonomyLevel::L0;
    p.mode = autonomy_modes::kExam;
    return p;
}

} // namespace

TEST_CASE("agent_ops success path: plan→…→delivery→trace→FinalDelivery", "[agent_ops]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    req.journalDirectory = uniqueTemp("success");

    auto out = coord.run(req);
    REQUIRE(out.session.ok);
    REQUIRE(out.delivery.outcome == "delivered");
    REQUIRE(out.trace);
    REQUIRE(out.trace->outcomeClaimSuccess);
    REQUIRE(out.projection.timeline.size() > 0);
    REQUIRE(out.projection.controls["bypasses_loop"].asBool() == false);

    // FinalDelivery round-trip
    auto round = FinalDelivery::fromJson(out.delivery.toJson());
    REQUIRE(round);
    REQUIRE(round->sessionId == out.delivery.sessionId);

    // Capsule export
    DeliveryAssembler asmblr;
    auto cap = asmblr.capsuleExportDocument(out.delivery);
    REQUIRE(cap["schema"].asString() == "sicnu.agent_ops.capsule_export/v1");

    // MCP/Pi surface parity
    auto status = sessionSurfaceStatus(out);
    REQUIRE(status["tool"].asString() == kSessionSurfaceTool);
    REQUIRE(sessionSurfaceActions()["actions"].isArray());
}

TEST_CASE("agent_ops verifier-fail→diagnose→replan→pass closed loop", "[agent_ops]")
{
    FakeScenario scenario;
    scenario.verification = {VerifyScript{"FAIL", ""}, VerifyScript{"PASS", ""}};
    scenario.diagnosis = {DiagnoseScript{"CRS_MISMATCH", {proposal("reproject")}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    req.budgets.maxReplans = 3;

    auto out = coord.run(req);
    REQUIRE(out.session.ok);
    REQUIRE(out.delivery.outcome == "delivered");
    bool sawDiagnose = false, sawReplan = false;
    for (const auto &s : out.session.summary.stages)
    {
        if (s == "diagnose")
            sawDiagnose = true;
        if (s == "replan")
            sawReplan = true;
    }
    REQUIRE(sawDiagnose);
    REQUIRE(sawReplan);
    REQUIRE(seams.diagnoserFake().diagnoseCount() >= 1);
    REQUIRE(seams.verifierFake().verifyCount() >= 2);
}

TEST_CASE("agent_ops recovery: retryable / non-retryable / max replan / no-progress", "[agent_ops]")
{
    RecoveryBridge bridge;
    OpDiagnostic retryable;
    retryable.code = "ops.runtime.TIMEOUT";
    retryable.rootCauseCode = "TIMEOUT";
    retryable.retryable = true;
    retryable.advisoryNext = recovery_action::kRetry;
    retryable.confidence = 0.9;

    RecoveryContext ctx;
    ctx.budgets.maxRetries = 2;
    auto d1 = bridge.decide(retryable, ctx);
    REQUIRE(d1.action == recovery_action::kRetry);

    ctx.retryCount = 2;
    auto d2 = bridge.decide(retryable, ctx);
    REQUIRE(d2.action == recovery_action::kAbort);
    REQUIRE(d2.reasonCode == "MAX_RETRIES");

    OpDiagnostic nonRetry;
    nonRetry.code = "ops.verify.FAIL";
    nonRetry.rootCauseCode = "VERIFICATION_FAILED";
    nonRetry.repairable = true;
    nonRetry.advisoryNext = recovery_action::kReplan;
    ctx.retryCount = 0;
    ctx.replanCount = 3;
    ctx.budgets.maxReplans = 3;
    auto d3 = bridge.decide(nonRetry, ctx);
    REQUIRE(d3.action == recovery_action::kAbort);
    REQUIRE(d3.reasonCode == "MAX_REPLANS");

    ctx.replanCount = 0;
    ctx.identicalFailureCount = 2;
    ctx.budgets.noProgressThreshold = 2;
    auto d4 = bridge.decide(nonRetry, ctx);
    REQUIRE(d4.action == recovery_action::kAbort);
    REQUIRE(d4.reasonCode == "NO_PROGRESS");
}

TEST_CASE("agent_ops repair needs approval + autonomy deny", "[agent_ops]")
{
    RecoveryBridge bridge;
    OpDiagnostic diag;
    diag.code = "ops.preflight.FIXABLE";
    diag.rootCauseCode = "PREFLIGHT_FIXABLE";
    diag.repairable = true;
    diag.advisoryNext = recovery_action::kRepair;
    diag.proposals = {"reproject"};
    diag.confidence = 0.9;

    RecoveryContext ctx;
    ctx.leadingRiskClass = "science_changing";
    ctx.humanApprovedRepair = false;
    auto ask = bridge.decide(diag, ctx);
    REQUIRE(ask.action == recovery_action::kAsk);
    REQUIRE(ask.needsApproval);
    REQUIRE(ask.reasonCode == "REPAIR_NEEDS_APPROVAL");

    // Autonomy deny on execute
    OpsAutonomyRequest ar;
    ar.mutateKind = ops_mutate::kExecute;
    ar.domain = "lab";
    ar.role = "student";
    auto gate = gateMutatingOp(restrictiveL0(), ar);
    REQUIRE_FALSE(gate.allowed);

    OperationsCoordinator::Dependencies deps;
    FakeScenario scenario;
    FakeSeams seams(scenario);
    deps.seams = makeDeps(seams);
    deps.autonomyPolicy = restrictiveL0();
    OperationsCoordinator coord(deps);
    OpsRunRequest req;
    req.session = ndviRequest();
    req.domain = "lab";
    req.role = "student";
    auto out = coord.run(req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.error.find("AUTONOMY") != std::string::npos);
}

TEST_CASE("agent_ops cancel during run", "[agent_ops]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);
    coord.requestCancel();
    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    // Cancelled sessions must not claim delivered success.
    REQUIRE_FALSE( (out.delivery.outcome == "delivered" && out.session.ok) );
}

TEST_CASE("agent_ops crash/restart, duplicate-resume, corrupted journal", "[agent_ops]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("resume");
    OpsRunRequest req;
    req.session = ndviRequest();
    req.journalDirectory = dir;
    auto out = coord.run(req);
    REQUIRE(out.session.ok);

    ResumeReconciler recon;
    auto terminal = recon.reconcile(out.session.journal);
    REQUIRE(terminal.ok);
    REQUIRE_FALSE(terminal.resumable);
    REQUIRE(terminal.duplicateSubmitRisk); // delivered

    // Duplicate resume of delivered session refused
    auto dup = coord.resume(dir, out.session.sessionId, req);
    REQUIRE_FALSE(dup.ok);

    // Corrupted journal
    {
        std::ofstream f(fs::path(dir) / "bogus-session.json");
        f << "{not json";
    }
    std::string err;
    auto bad = recon.reconcileFile(dir, "bogus-session", &err);
    REQUIRE_FALSE(bad.ok);
    REQUIRE(bad.reasonCode == "CORRUPTED_OR_MISSING_JOURNAL");
    // unknown ≠ success
    REQUIRE_FALSE(bad.resumable);
}

TEST_CASE("agent_ops diagnostic: missing output, indeterminate, debugger incomplete", "[agent_ops]")
{
    DiagnosticBridge bridge;

    DiagnosticInputs missing;
    missing.missingOutput = true;
    auto d1 = bridge.diagnose(missing);
    REQUIRE(d1);
    REQUIRE(d1->rootCauseCode == "MISSING_OUTPUT");

    DiagnosticInputs indet;
    indet.indeterminateVerifier = true;
    auto d2 = bridge.diagnose(indet);
    REQUIRE(d2);
    REQUIRE(d2->advisoryNext == recovery_action::kAsk);

    DiagnosticInputs dbg;
    dbg.debuggerIncompleteEvidence = true;
    dbg.debugger["code"] = "experiment.debugger.insufficient_evidence";
    auto d3 = bridge.diagnose(dbg);
    REQUIRE(d3);
    REQUIRE(d3->rootCauseCode == "DEBUGGER_INCOMPLETE_EVIDENCE");

    // LLM prose alone is insufficient
    DiagnosticInputs proseOnly;
    sicnu::agent_loop::Diagnosis empty;
    empty.summary = "the model thinks CRS is wrong";
    proseOnly.diagnoseRun = empty;
    std::string err;
    auto d4 = bridge.diagnose(proseOnly, &err);
    REQUIRE_FALSE(d4);
}

TEST_CASE("agent_ops secrets redacted; fault markers preserved; trace budget", "[agent_ops]")
{
    Json::Value payload(Json::objectValue);
    payload["password"] = "s3cret";
    payload["api_key"] = "xyz";
    payload["fault"] = "transient_io";
    payload["ok_field"] = "visible";
    bool truncated = false;
    auto red = redactAndBound(payload, 4096, &truncated);
    REQUIRE(red["password"].asString() == "[REDACTED]");
    REQUIRE(red["api_key"].asString() == "[REDACTED]");
    REQUIRE(red["fault"].asString() == "transient_io");
    REQUIRE(red["ok_field"].asString() == "visible");

    // Truncation flags
    Json::Value big(Json::objectValue);
    big["blob"] = std::string(100, 'x');
    auto t = redactAndBound(big, 10, &truncated);
    REQUIRE(truncated);
    REQUIRE(t["blob"]["truncated"].asBool());

    // Trace budget: tiny max forces compaction path
    FakeScenario scenario;
    FakeSeams seams(scenario);
    SessionPolicy policy = SessionPolicy::defaults();
    ScientificAgentSession session(policy, makeDeps(seams));
    auto result = session.run(ndviRequest());
    REQUIRE(result.ok);

    LiveSessionRecorder::Options opt;
    opt.maxTraceBytes = 200; // tiny
    opt.maxPayloadChars = 32;
    LiveSessionRecorder rec(opt);
    std::string err;
    auto trace = rec.projectTrace(result.journal, "live", &err);
    // Either succeeds via compaction or fails closed — never silent oversize.
    if (!trace)
        REQUIRE_FALSE(err.empty());
    else
        REQUIRE(trace->raw.isObject());
}

TEST_CASE("agent_ops benchmark projection + in-memory store adapter", "[agent_ops]")
{
    sicnu::agentbench::SuiteReport report;
    report.suiteId = "suite-demo";
    report.version = "1";
    report.packDigest = "deadbeefcafebabe";
    report.digest = "0123456789abcdef";
    report.summary.passCount = 1;
    report.summary.failCount = 0;
    sicnu::agentbench::SuiteCaseResult c;
    c.caseId = "case-1";
    c.verdict = "PASS";
    c.taskFamily = "ndvi";
    report.cases.push_back(c);

    BenchmarkAdapter adapter;
    auto doc = adapter.projectSuiteReport(report);
    REQUIRE(doc.suiteId == "suite-demo");
    REQUIRE(doc.status == "completed");

    InMemoryBenchmarkSink sink;
    REQUIRE(adapter.persist(doc, sink));
    REQUIRE(sink.saved().size() == 1);
    auto round = BenchmarkPersistDocument::fromJson(doc.toJson());
    REQUIRE(round);
}

TEST_CASE("agent_ops Windows-safe journal path + atomic persist", "[agent_ops]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    SessionPolicy policy = SessionPolicy::defaults();
    ScientificAgentSession session(policy, makeDeps(seams));
    auto result = session.run(ndviRequest());
    REQUIRE(SessionJournal::isSafeSessionId(result.sessionId));

    const std::string dir = uniqueTemp("atomic");
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(result.journal, dir, &err));
    auto loaded = rec.loadJournal(dir, result.sessionId, &err);
    REQUIRE(loaded);
    REQUIRE(loaded->sessionId() == result.sessionId);
}

TEST_CASE("agent_ops control surface pause/cancel without bypassing loop", "[agent_ops]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);
    auto applied = sessionSurfaceApply(coord, "pause");
    REQUIRE(applied["ok"].asBool());
    REQUIRE(coord.isPauseRequested());
    applied = sessionSurfaceApply(coord, "cancel");
    REQUIRE(applied["ok"].asBool());
    REQUIRE(coord.isCancelRequested());
}
