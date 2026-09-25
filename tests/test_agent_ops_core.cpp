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

// ---------------------------------------------------------------------------
// Production-session consistency round (see
// .planning/completion-agent-ops-production-session/recon-matrix.md G1..G7).
// ---------------------------------------------------------------------------

namespace {

sicnu::agent_loop::DecisionRecord journalDecision(const std::string &sessionId,
                                                  const std::string &stage,
                                                  const std::string &decisionId,
                                                  const std::string &action,
                                                  const std::string &runId = {})
{
    sicnu::agent_loop::DecisionRecord d;
    d.decisionId = decisionId;
    d.sessionId = sessionId;
    d.stage = stage;
    d.reason = "hand-built journal entry for reconciler tests";
    d.selected["action"] = action;
    if (!runId.empty())
        d.inputs["run_id"] = runId;
    return d;
}

} // namespace

TEST_CASE("agent_ops resume reconciler projects loop resume authority (pre-plan only)",
          "[agent_ops][resume]")
{
    ResumeReconciler recon;

    // (a) Journal parked mid-pipeline (crash before terminal): the loop
    // refuses to resume anything past the plan seam, so the reconciler
    // must not advertise resumability the coordinator cannot honor.
    sicnu::agent_loop::SessionJournal parked("sess-past-plan");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("stage_enter", "plan_request", {}, 2));
    REQUIRE(parked.append("stage_enter", "verify", {}, 3));
    auto pastPlan = recon.reconcile(parked);
    REQUIRE(pastPlan.ok);
    REQUIRE_FALSE(pastPlan.resumable);
    REQUIRE(pastPlan.reasonCode == "RESUME_PAST_PLAN_SEAM");

    // (b) Pre-plan journal: resumable per the loop contract.
    sicnu::agent_loop::SessionJournal prePlan("sess-preplan");
    REQUIRE(prePlan.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(prePlan.append("stage_enter", "data_state_snapshot", {}, 2));
    auto pre = recon.reconcile(prePlan);
    REQUIRE(pre.ok);
    REQUIRE(pre.resumable);
    REQUIRE(pre.reasonCode == "RESUMABLE");

    // (c) Execute-stage journal with a real loop run decision: run ids are
    // collected from the loop's wire shape (inputs.run_id + action "run"),
    // but the journal is not resumable past the plan seam.
    sicnu::agent_loop::SessionJournal exec("sess-exec");
    REQUIRE(exec.append("stage_enter", "execute", {}, 1));
    REQUIRE(exec.append("decision", "execute", {}, 2,
                        journalDecision("sess-exec", "execute", "dec-1", "run", "run-abc")));
    auto afterExecute = recon.reconcile(exec);
    REQUIRE(afterExecute.ok);
    REQUIRE_FALSE(afterExecute.resumable);
    REQUIRE(afterExecute.reasonCode == "RESUME_PAST_PLAN_SEAM");
    REQUIRE(afterExecute.submittedRunIds.count("run-abc") == 1);
}

TEST_CASE("agent_ops coordinator resume surfaces the reconciler reason (no silent mismatch)",
          "[agent_ops][resume]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("resume-authority");

    // Execute-stage journal persisted under the recorder's file convention.
    sicnu::agent_loop::SessionJournal exec("sess-exec-persist");
    REQUIRE(exec.append("stage_enter", "execute", {}, 1));
    REQUIRE(exec.append("decision", "execute", {}, 2,
                        journalDecision("sess-exec-persist", "execute", "dec-1", "run",
                                        "run-xyz")));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(exec, dir, &err));

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.resume(dir, "sess-exec-persist", req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.error == "RESUME_PAST_PLAN_SEAM");

    // Delivered journals stay duplicate-submit-refused with the typed reason.
    OpsRunRequest persistReq = req;
    persistReq.journalDirectory = dir;
    auto done = coord.run(persistReq);
    REQUIRE(done.ok);
    auto dup = coord.resume(dir, done.session.sessionId, req);
    REQUIRE_FALSE(dup.ok);
    REQUIRE(dup.reconcile.reasonCode == "ALREADY_TERMINAL");
    REQUIRE(dup.error == "DUPLICATE_SUBMIT_REFUSED");
}

TEST_CASE("agent_ops pause gates session launch instead of being a no-op flag",
          "[agent_ops][pause]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();

    coord.requestPause();
    auto paused = coord.run(req);
    REQUIRE_FALSE(paused.ok);
    REQUIRE(paused.error == "PAUSED");
    REQUIRE(paused.session.journal.size() == 0); // no loop work started

    // resume() is gated the same way.
    const std::string dir = uniqueTemp("pause-resume");
    auto pausedResume = coord.resume(dir, "missing-session", req);
    REQUIRE(pausedResume.error == "PAUSED");

    coord.clearPause();
    auto out = coord.run(req);
    REQUIRE(out.ok);
}

TEST_CASE("agent_ops delivery claims are gated on verifier evidence", "[agent_ops][delivery]")
{
    OperationsCoordinator::Dependencies deps;
    FakeScenario scenario;
    FakeSeams seams(scenario);
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    // Verified delivery: high-confidence claim with a journal evidence ref.
    OpsRunRequest req;
    req.session = ndviRequest();
    auto good = coord.run(req);
    REQUIRE(good.ok);
    REQUIRE(good.delivery.claims.size() == 1);
    REQUIRE(good.delivery.claims[0]["confidence"].asDouble() == 0.9);
    REQUIRE(good.delivery.claims[0]["evidence_ref"].asString() ==
            "journal:" + good.session.sessionId);

    // Unverified delivery (dry run never verifies): unknown ≠ success, so
    // the outcome claim must not claim 0.9 confidence.
    OpsRunRequest dry = req;
    dry.policy.mode = sicnu::agent_loop::RunMode::DryRun;
    auto dryRun = coord.run(dry);
    REQUIRE(dryRun.ok);
    REQUIRE(dryRun.delivery.outcome == "delivered");
    REQUIRE(dryRun.session.summary.verificationVerdict.empty());
    REQUIRE(dryRun.delivery.claims.size() == 1);
    REQUIRE(dryRun.delivery.claims[0]["confidence"].asDouble() == 0.0);
    REQUIRE(dryRun.delivery.claims[0]["evidence_missing"].size() == 1);
    REQUIRE(dryRun.delivery.claims[0]["evidence_missing"][0].asString() == "verifier_verdict");
}

TEST_CASE("agent_ops coordinator persists live trajectory refs through the benchmark sink",
          "[agent_ops][benchmark]")
{
    FakeScenario scenario;
    scenario.execution = {ExecutionScript{true, "", {"/tmp/run42/out.tif"}}};
    FakeSeams seams(scenario);
    InMemoryBenchmarkSink sink;
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    deps.benchmarkSink = &sink;
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE(out.ok);
    REQUIRE(out.benchmarkError.empty());

    REQUIRE(sink.saved().size() == 1);
    const auto &doc = sink.saved().front();
    REQUIRE(doc.status == "completed");
    // Result ids stay unique across resumes of the same session id: the
    // adopted journal only grows, so the entry count disambiguates.
    REQUIRE(doc.resultId == "bpr-live-" + out.session.sessionId + "-" +
                                std::to_string(out.session.summary.journalEntries));
    REQUIRE(doc.summary["session_id"].asString() == out.session.sessionId);
    REQUIRE(doc.summary["trace_id"].asString() == out.trace->traceId);
    // Trajectory refs, not recomputed metrics: decision ids and evidence
    // refs recorded as the loop wrote them.
    REQUIRE(doc.rawReport["kind"].asString() == "live_trajectory");
    REQUIRE(doc.rawReport["decision_refs"].isArray());
    REQUIRE(doc.rawReport["decision_refs"].size() > 0);
    REQUIRE_FALSE(doc.summary.isMember("pass"));
    REQUIRE_FALSE(doc.summary.isMember("fail"));

    REQUIRE(out.delivery.benchmarkRefs.size() == 1);
    REQUIRE(out.delivery.benchmarkRefs[0]["result_id"].asString() == doc.resultId);
    // The loop's submitted runs surface in the delivery (real wire field).
    REQUIRE(out.delivery.runIds.size() >= 1);

    // A failed session persists with a failed status (still real refs).
    FakeScenario bad;
    bad.verification = {VerifyScript{"FAIL", ""}};
    bad.diagnosis = {DiagnoseScript{"CRS_MISMATCH", {}}}; // no proposals → refuse
    FakeSeams badSeams(bad);
    InMemoryBenchmarkSink badSink;
    OperationsCoordinator::Dependencies badDeps;
    badDeps.seams = makeDeps(badSeams);
    badDeps.benchmarkSink = &badSink;
    OperationsCoordinator badCoord(badDeps);
    auto badOut = badCoord.run(req);
    REQUIRE_FALSE(badOut.ok);
    REQUIRE(badSink.saved().size() == 1);
    REQUIRE(badSink.saved().front().status == "failed");
    REQUIRE(badSink.saved().front().summary["stop_reason"].asString() ==
            badOut.session.stopReason);
}

TEST_CASE("agent_ops session surface implements every advertised action",
          "[agent_ops][surface]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    // Parity: no advertised action may fall through to UNKNOWN_ACTION.
    auto actions = sessionSurfaceActions()["actions"];
    REQUIRE(actions.isArray());
    for (const auto &a : actions)
    {
        const std::string action = a.asString();
        auto doc = sessionSurfaceApply(coord, action, {});
        INFO("action: " << action);
        REQUIRE(doc["error"].asString() != "UNKNOWN_ACTION");
    }

    // status before any run: typed no-session, not success.
    auto empty = sessionSurfaceApply(coord, "status", {});
    REQUIRE_FALSE(empty["ok"].asBool());
    REQUIRE(empty["error"].asString() == "NO_SESSION");

    // The discovery loop above set pause/cancel control state; a driver
    // relaunching after discovery (or after a cancel) clears it first.
    coord.clearPause();
    coord.clearCancel();

    // run through the surface, then status/timeline/export see the result.
    Json::Value runArgs(Json::objectValue);
    runArgs["goal"] = "compute NDVI for the scene";
    runArgs["intent"] = "ndvi";
    auto ran = sessionSurfaceApply(coord, "run", runArgs);
    REQUIRE(ran["ok"].asBool());
    const std::string sessionId = ran["session_id"].asString();
    REQUIRE_FALSE(sessionId.empty());

    auto status = sessionSurfaceApply(coord, "status", {});
    REQUIRE(status["ok"].asBool());
    REQUIRE(status["session_id"].asString() == sessionId);

    auto timeline = sessionSurfaceApply(coord, "timeline", {});
    REQUIRE(timeline["projection"].isObject());

    auto exportDoc = sessionSurfaceApply(coord, "export", {});
    REQUIRE(exportDoc["schema"].asString() == "sicnu.agent_ops.capsule_export/v1");

    // resume without a journal directory is a typed argument error.
    auto noArgs = sessionSurfaceApply(coord, "resume", {});
    REQUIRE_FALSE(noArgs["ok"].asBool());
    REQUIRE(noArgs["error"].asString() == "MISSING_ARGS");

    // The journalled goal must be restated: a goal-less resume would be
    // refused by the loop (SESSION_GOAL_MISMATCH) AFTER overwriting the
    // parked journal with a refused terminal one.
    Json::Value dirOnly(Json::objectValue);
    dirOnly["journal_directory"] = "/tmp/agent_ops_whatever";
    dirOnly["session_id"] = "sess-x";
    auto noGoal = sessionSurfaceApply(coord, "resume", dirOnly);
    REQUIRE_FALSE(noGoal["ok"].asBool());
    REQUIRE(noGoal["error"].asString() == "MISSING_ARGS");

    // approve_repair records real pending state consumed by the next run.
    Json::Value approveArgs(Json::objectValue);
    approveArgs["approve"] = true;
    auto approved = sessionSurfaceApply(coord, "approve_repair", approveArgs);
    REQUIRE(approved["ok"].asBool());
    REQUIRE(coord.isPendingRepairApproval());

    // The approval is one-shot: the next launch consumes it, so the
    // science-changing repair gate re-arms for later sessions.
    coord.clearPause();
    coord.clearCancel();
    REQUIRE_FALSE(coord.isCancelRequested());
    auto consume = sessionSurfaceApply(coord, "run", runArgs);
    REQUIRE(consume["ok"].asBool());
    REQUIRE_FALSE(coord.isPendingRepairApproval());
}

TEST_CASE("agent_ops failed-session diagnostic stays inside the evidence", "[agent_ops][diagnostic]")
{
    // Preflight-blocked refusal: no verification FAIL and no diagnose
    // decisions exist, so the only typed evidence is the loop's own stop
    // reason. The diagnostic must carry it without inventing repairability.
    FakeScenario scenario;
    scenario.preflight = {PreflightScript{"blocked", {}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.session.stopReason == "PREFLIGHT_BLOCKED");
    REQUIRE(out.lastDiagnostic);
    REQUIRE(out.lastDiagnostic->rootCauseCode == "PREFLIGHT_BLOCKED");
    REQUIRE(out.lastDiagnostic->code == "ops.session.FAILED");
    REQUIRE_FALSE(out.lastDiagnostic->repairable);
    REQUIRE_FALSE(out.lastDiagnostic->retryable);
    REQUIRE(out.lastDiagnostic->confidence <= 0.5);
    REQUIRE(out.lastDiagnostic->advisoryNext == recovery_action::kAsk);
    REQUIRE(out.lastDiagnostic->sources["session"]["stop_reason"].asString() ==
            "PREFLIGHT_BLOCKED");
    REQUIRE(out.lastDiagnostic->evidence["evidence_unavailable"].asBool());

    // The recovery decision inherits the honesty: ask, never an automatic
    // mutating replan on unknown evidence.
    REQUIRE(out.lastRecovery);
    REQUIRE(out.lastRecovery->action == recovery_action::kAsk);
    REQUIRE(out.lastRecovery->reasonCode == "NEEDS_HUMAN");

    // Refused sessions carry the typed stop reason on the outcome claim.
    REQUIRE(out.delivery.claims.size() == 1);
    REQUIRE(out.delivery.claims[0]["confidence"].asDouble() == 0.85);
    REQUIRE(out.delivery.claims[0]["stop_reason"].asString() == "PREFLIGHT_BLOCKED");
}

TEST_CASE("agent_ops stale bridged diagnostic is not attributed to a later failure",
          "[agent_ops][diagnostic]")
{
    // One coordinator, one attempt-scripted scenario: attempt 1 verifies
    // FAIL and diagnoses CRS_MISMATCH (the bridge caches a diagnostic),
    // attempt 2 is blocked at preflight. The refusal diagnostic must
    // describe THIS failure from this session's evidence, never replay the
    // cached verify-FAIL entry.
    FakeScenario scenario;
    scenario.preflight = {PreflightScript{"ok", {}}, PreflightScript{"blocked", {}}};
    scenario.verification = {VerifyScript{"FAIL", ""}, VerifyScript{"FAIL", ""}};
    scenario.diagnosis = {DiagnoseScript{"CRS_MISMATCH", {proposal("reproject")}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.session.stopReason == "PREFLIGHT_BLOCKED");
    REQUIRE(out.lastDiagnostic);
    // The stop reason is the terminal fact and always the headline; the
    // stale-cache bug would have surfaced ops.verify.MISSING_OUTPUT here.
    REQUIRE(out.lastDiagnostic->code == "ops.session.FAILED");
    REQUIRE(out.lastDiagnostic->rootCauseCode == "PREFLIGHT_BLOCKED");
    REQUIRE_FALSE(out.lastDiagnostic->repairable);
    REQUIRE(out.lastDiagnostic->advisoryNext == recovery_action::kAsk);
    REQUIRE(out.lastDiagnostic->sources["session"]["stop_reason"].asString() ==
            "PREFLIGHT_BLOCKED");
    // The session's structured evidence (live bridge or journal diagnose
    // record) is attached as a source, never discarded.
    REQUIRE((out.lastDiagnostic->sources.isMember("bridge") ||
             out.lastDiagnostic->sources.isMember("journal_diagnose")));
}

TEST_CASE("agent_ops benchmark sink failure is surfaced, never silent",
          "[agent_ops][benchmark]")
{
    struct FailingSink : IBenchmarkResultSink
    {
        bool save(const BenchmarkPersistDocument &, std::string *error) override
        {
            if (error)
                *error = "store offline";
            return false;
        }
    };

    FakeScenario scenario;
    FakeSeams seams(scenario);
    FailingSink sink;
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    deps.benchmarkSink = &sink;
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE(out.ok); // session outcome stays authoritative
    REQUIRE(out.benchmarkError == "store offline");
    REQUIRE(out.delivery.benchmarkRefs.size() == 0);

    auto status = sessionSurfaceStatus(out);
    REQUIRE(status["benchmark_error"].asString() == "store offline");
}

TEST_CASE("agent_ops resume succeeds from a parked pre-plan journal", "[agent_ops][resume]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("resume-preplan");
    const std::string goal = "compute NDVI for the scene";

    // Hand-build the journal the loop would have parked at the snapshot
    // stage: goal normalization decision + stage enter, no terminal.
    sicnu::agent_loop::DecisionRecord goalDecision;
    goalDecision.decisionId = "dec-goal-1";
    goalDecision.sessionId = "sess-preplan-resume";
    goalDecision.stage = "goal_normalization";
    goalDecision.reason = "goal accepted as stated: " + goal;
    goalDecision.selected["action"] = "accept_goal";
    goalDecision.inputs["goal"] = goal;

    sicnu::agent_loop::SessionJournal parked("sess-preplan-resume");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("decision", "goal_normalization", {}, 2, goalDecision));
    REQUIRE(parked.append("stage_enter", "data_state_snapshot", {}, 3));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(parked, dir, &err));

    OpsRunRequest req;
    req.session.goal = goal;
    req.session.intent = "ndvi";
    auto out = coord.resume(dir, "sess-preplan-resume", req);
    REQUIRE(out.ok);
    REQUIRE(out.delivery.outcome == "delivered");
}

TEST_CASE("agent_ops wrong-goal resume refuses before touching the parked journal",
          "[agent_ops][resume]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("resume-mismatch");
    const std::string goal = "compute NDVI for the scene";

    sicnu::agent_loop::DecisionRecord goalDecision;
    goalDecision.decisionId = "dec-goal-1";
    goalDecision.sessionId = "sess-preplan-mismatch";
    goalDecision.stage = "goal_normalization";
    goalDecision.reason = "goal accepted as stated: " + goal;
    goalDecision.selected["action"] = "accept_goal";
    goalDecision.inputs["goal"] = goal;

    sicnu::agent_loop::SessionJournal parked("sess-preplan-mismatch");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("decision", "goal_normalization", {}, 2, goalDecision));
    REQUIRE(parked.append("stage_enter", "data_state_snapshot", {}, 3));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(parked, dir, &err));

    // A well-formed resume request with the WRONG goal: refused before the
    // journal is adopted, so nothing overwrites the parked resumable state.
    OpsRunRequest wrong;
    wrong.session.goal = "classify land cover instead";
    wrong.session.intent = "ndvi";
    auto mismatch = coord.resume(dir, "sess-preplan-mismatch", wrong);
    REQUIRE_FALSE(mismatch.ok);
    REQUIRE(mismatch.error == "SESSION_GOAL_MISMATCH");

    ResumeReconciler recon;
    auto after = recon.reconcileFile(dir, "sess-preplan-mismatch", &err);
    REQUIRE(after.ok);
    REQUIRE(after.resumable);
    REQUIRE(after.terminalState.empty());

    // The correct goal still resumes the same parked journal.
    OpsRunRequest right;
    right.session.goal = goal;
    right.session.intent = "ndvi";
    auto resumed = coord.resume(dir, "sess-preplan-mismatch", right);
    REQUIRE(resumed.ok);
    REQUIRE(resumed.delivery.outcome == "delivered");
}

TEST_CASE("agent_ops capsule export emits portable refs (no absolute paths)",
          "[agent_ops][delivery]")
{
    DeliveryAssembler assembler;
    sicnu::agent_loop::SessionResult result;
    result.sessionId = "sess-portable";
    result.summary.outcome = "delivered";
    result.summary.verificationVerdict = "PASS";
    result.summary.artifacts = {"/tmp/run42/out.tif", "/r1/out.tif", "/r2/out.tif",
                                "relative.tif", "C:\\data\\win.tif"};

    auto delivery = assembler.assemble(result, {});
    REQUIRE(delivery.outputs.size() == 5);
    REQUIRE(delivery.outputs[0]["path"].asString() == "/tmp/run42/out.tif"); // local truth kept
    // Absolute paths become basename + stable path fingerprint: no machine
    // paths leak, and same-basename artifacts stay distinguishable.
    const std::string portable0 = delivery.outputs[0]["portable_ref"].asString();
    REQUIRE(portable0.rfind("out.tif@", 0) == 0);
    REQUIRE(portable0.find('/') == std::string::npos);
    REQUIRE(portable0.find('\\') == std::string::npos);
    REQUIRE(delivery.outputs[1]["portable_ref"].asString() !=
            delivery.outputs[2]["portable_ref"].asString());
    REQUIRE(delivery.outputs[3]["portable_ref"].asString() == "relative.tif");
    const std::string portable4 = delivery.outputs[4]["portable_ref"].asString();
    REQUIRE(portable4.rfind("win.tif@", 0) == 0);
    REQUIRE(portable4.find('\\') == std::string::npos);

    auto capsule = assembler.capsuleExportDocument(delivery);
    REQUIRE(capsule["outputs"][0]["portable_ref"].asString() == portable0);
    REQUIRE(capsule["outputs"][0]["path"].isNull());
    REQUIRE(capsule["outputs"][1]["portable_ref"].asString() ==
            capsule["outputs"][1]["portable_ref"].asString());
}

// ---------------------------------------------------------------------------
// R3 agent-ops live driver: cancel disarm, resume parity, crash
// checkpointing, fail-closed repair plans and the approval ask carrier.
// ---------------------------------------------------------------------------

TEST_CASE("agent_ops surface can disarm cancel and pause (relaunch path)",
          "[agent_ops][surface]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    // cancel latches: the next run aborts as CANCELLED without any seam
    // work (the loop is the authority for the abort).
    auto cancelled = sessionSurfaceApply(coord, "cancel");
    REQUIRE(cancelled["ok"].asBool());
    Json::Value runArgs(Json::objectValue);
    runArgs["goal"] = "compute NDVI for the scene";
    runArgs["intent"] = "ndvi";
    auto aborted = sessionSurfaceApply(coord, "run", runArgs);
    REQUIRE_FALSE(aborted["ok"].asBool());
    REQUIRE(aborted["stop_reason"].asString() == "CANCELLED");
    REQUIRE(seams.executorFake().beginCount() == 0);

    // The wire disarm: after observing the aborted run, clear_cancel arms
    // the coordinator for new work — before this action existed, a driver
    // that cancelled once could never launch again over the wire.
    auto disarm = sessionSurfaceApply(coord, "clear_cancel");
    REQUIRE(disarm["ok"].asBool());
    REQUIRE_FALSE(coord.isCancelRequested());
    auto relaunched = sessionSurfaceApply(coord, "run", runArgs);
    REQUIRE(relaunched["ok"].asBool());
    REQUIRE(relaunched["outcome"].asString() == "delivered");

    // clear_pause on the wire (the legacy resume_clear_pause stays valid).
    REQUIRE(sessionSurfaceApply(coord, "pause")["ok"].asBool());
    REQUIRE(coord.isPauseRequested());
    auto pausedRun = sessionSurfaceApply(coord, "run", runArgs);
    REQUIRE_FALSE(pausedRun["ok"].asBool());
    REQUIRE(pausedRun["error"].asString() == "PAUSED");
    REQUIRE(sessionSurfaceApply(coord, "clear_pause")["ok"].asBool());
    REQUIRE_FALSE(coord.isPauseRequested());
    REQUIRE(sessionSurfaceApply(coord, "resume_clear_pause")["ok"].asBool());

    // The parity loop over advertised actions never falls through.
    for (const auto &a : sessionSurfaceActions()["actions"])
    {
        auto doc = sessionSurfaceApply(coord, a.asString(), {});
        INFO("action: " << a.asString());
        REQUIRE(doc["error"].asString() != "UNKNOWN_ACTION");
    }
}

TEST_CASE("agent_ops resume applies the same autonomy gate and budgets as run",
          "[agent_ops][resume]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    deps.autonomyPolicy = restrictiveL0();
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("resume-gate");
    const std::string goal = "compute NDVI for the scene";

    sicnu::agent_loop::DecisionRecord goalDecision;
    goalDecision.decisionId = "dec-goal-1";
    goalDecision.sessionId = "sess-gate-parity";
    goalDecision.stage = "goal_normalization";
    goalDecision.reason = "goal accepted as stated: " + goal;
    goalDecision.selected["action"] = "accept_goal";
    goalDecision.inputs["goal"] = goal;

    sicnu::agent_loop::SessionJournal parked("sess-gate-parity");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("decision", "goal_normalization", {}, 2, goalDecision));
    REQUIRE(parked.append("stage_enter", "data_state_snapshot", {}, 3));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(parked, dir, &err));

    // An execute-capable resume must pass the SAME gate as a launch: L0
    // exam policy denies it before the journal is even loaded.
    OpsRunRequest executeReq;
    executeReq.session.goal = goal;
    executeReq.session.intent = "ndvi";
    executeReq.policy.mode = sicnu::agent_loop::RunMode::ExecuteWithVerify;
    executeReq.domain = "lab";
    executeReq.role = "student";
    auto denied = coord.resume(dir, "sess-gate-parity", executeReq);
    REQUIRE_FALSE(denied.ok);
    REQUIRE(denied.error.find("AUTONOMY") != std::string::npos);
    REQUIRE(denied.lastRecovery);
    REQUIRE(denied.lastRecovery->action == recovery_action::kAbort);

    // Budget parity: the resumed session's summary must show the caller's
    // bounds, not the policy defaults the old resume silently used.
    OpsRunRequest bounded;
    bounded.session.goal = goal;
    bounded.session.intent = "ndvi";
    bounded.policy.mode = sicnu::agent_loop::RunMode::PlanOnly;
    bounded.budgets.noProgressThreshold = 7;
    bounded.budgets.maxReplans = 5;
    bounded.budgets.resourceBudgetMb = 8192;
    auto resumed = coord.resume(dir, "sess-gate-parity", bounded);
    REQUIRE(resumed.ok);
    REQUIRE(resumed.session.summary.policy["no_progress_threshold"].asInt() == 7);
    REQUIRE(resumed.session.summary.policy["max_replans"].asInt() == 5);
    REQUIRE(resumed.session.summary.policy["resource_budget_mb"].asInt() == 8192);
}

namespace {

/// Executor decorator that observes the journal directory at the SUBMIT
/// boundary (poll runs right after the loop journaled the run decision):
/// the crash-evidence checkpoint must already be on disk there.
class ProbeExecutor : public sicnu::agent_loop::IExecutor
{
  public:
    ProbeExecutor(sicnu::agent_loop::IExecutor &inner, std::string journalDir,
                  std::vector<std::size_t> &observedSizes)
        : mInner(inner), mDir(std::move(journalDir)), mObserved(observedSizes)
    {
    }

    sicnu::agent_loop::ExecutionStart begin(
        const sicnu::agent_loop::PlanDraft &plan) override
    {
        return mInner.begin(plan);
    }

    sicnu::agent_loop::ExecutionOutcome poll(const sicnu::agent_loop::ExecutionStart &start,
                                             long long timeoutMs) override
    {
        // The checkpoint that exists when control returns to the engine is
        // what a hard kill at this moment would leave behind.
        for (const auto &entry : std::filesystem::directory_iterator(mDir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            std::string err;
            auto journal = sicnu::agent_loop::SessionJournal::load(
                mDir, entry.path().stem().string(), &err);
            if (!journal)
                continue;
            mObserved.push_back(journal->size());
            mObservedRunIds = sicnu::agent_ops::ResumeReconciler{}.reconcile(*journal);
        }
        return mInner.poll(start, timeoutMs);
    }

    void cancel(const sicnu::agent_loop::ExecutionStart &start) override
    {
        mInner.cancel(start);
    }

    sicnu::agent_ops::ReconcileResult mObservedRunIds;

  private:
    sicnu::agent_loop::IExecutor &mInner;
    std::string mDir;
    std::vector<std::size_t> &mObserved;
};

} // namespace

TEST_CASE("agent_ops journal is flushed at the submit boundary (crash evidence)",
          "[agent_ops][crash]")
{
    FakeScenario scenario;
    scenario.execution = {ExecutionScript{true, "", {"/tmp/run42/out.tif"}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);

    const std::string dir = uniqueTemp("checkpoint-submit");
    std::filesystem::remove_all(dir); // no leftovers from earlier runs
    std::vector<std::size_t> observed;
    ProbeExecutor probe(seams.executor(), dir, observed);
    deps.seams.executor = &probe;
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    req.journalDirectory = dir;
    auto out = coord.run(req);
    REQUIRE(out.ok);
    REQUIRE(out.checkpointError.empty());

    // At the submit boundary the on-disk journal already carried the run
    // decision — a kill exactly there leaves typed submitted-run evidence,
    // not an empty trail.
    REQUIRE_FALSE(observed.empty());
    REQUIRE(probe.mObservedRunIds.ok);
    REQUIRE(probe.mObservedRunIds.reasonCode == "RESUME_PAST_PLAN_SEAM");
    REQUIRE(probe.mObservedRunIds.duplicateSubmitRisk);
    REQUIRE_FALSE(probe.mObservedRunIds.submittedRunIds.empty());
    REQUIRE(probe.mObservedRunIds.submittedRunIds ==
            out.reconcile.submittedRunIds);

    // The delivered journal refuses re-submission as always.
    REQUIRE(out.reconcile.reasonCode == "ALREADY_TERMINAL");
    REQUIRE(out.reconcile.duplicateSubmitRisk);
}

TEST_CASE("agent_ops resumed sessions checkpoint the adopted journal too",
          "[agent_ops][crash][resume]")
{
    FakeScenario scenario;
    scenario.execution = {ExecutionScript{true, "", {"/tmp/run42/out.tif"}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);

    const std::string dir = uniqueTemp("checkpoint-resume");
    std::filesystem::remove_all(dir);
    std::vector<std::size_t> observed;
    ProbeExecutor probe(seams.executor(), dir, observed);
    deps.seams.executor = &probe;
    OperationsCoordinator coord(deps);

    // Park a pre-plan journal (3 entries), then resume under checkpointing.
    sicnu::agent_loop::SessionJournal parked("sess-resume-ckpt");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("decision", "goal_normalization", {}, 2,
                           journalDecision("sess-resume-ckpt", "goal_normalization",
                                           "dec-1", "accept_goal")));
    REQUIRE(parked.append("stage_enter", "data_state_snapshot", {}, 3));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(parked, dir, &err));

    OpsRunRequest req;
    req.session.goal = "compute NDVI for the scene";
    req.session.intent = "ndvi";
    req.journalDirectory = dir;
    auto out = coord.resume(dir, "sess-resume-ckpt", req);
    REQUIRE(out.ok);
    REQUIRE(out.checkpointError.empty());

    // Every boundary the resumed leg passed left a STRICTLY GROWING prefix
    // on disk (the adopted journal plus the new appends) — a kill during
    // the resumed leg loses nothing either.
    REQUIRE_FALSE(observed.empty());
    for (const std::size_t size : observed)
        REQUIRE(size > parked.size());
    REQUIRE(out.reconcile.duplicateSubmitRisk); // delivered WITH submitted runs
}

TEST_CASE("agent_ops resume refuses a crashed past-plan journal with submitted runs",
          "[agent_ops][crash][resume]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    const std::string dir = uniqueTemp("crash-past-plan");

    // The journal a hard kill during execute would have checkpointed.
    sicnu::agent_loop::SessionJournal crashed("sess-crash-submit");
    REQUIRE(crashed.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(crashed.append("decision", "goal_normalization", {}, 2,
                           journalDecision("sess-crash-submit", "goal_normalization",
                                           "dec-1", "accept_goal")));
    REQUIRE(crashed.append("stage_enter", "plan_request", {}, 3));
    REQUIRE(crashed.append("stage_enter", "preflight", {}, 4));
    REQUIRE(crashed.append("stage_enter", "execute", {}, 5));
    REQUIRE(crashed.append("decision", "execute", {}, 6,
                           journalDecision("sess-crash-submit", "execute", "dec-2", "run",
                                           "run-crash-1")));
    LiveSessionRecorder rec;
    std::string err;
    REQUIRE(rec.persistJournal(crashed, dir, &err));

    // Resume: refused (loop contract), and the duplicate-submit hazard is
    // on the wire WITHOUT hijacking the resume-contract reason code.
    OpsRunRequest req;
    req.session.goal = "compute NDVI for the scene";
    req.session.intent = "ndvi";
    auto out = coord.resume(dir, "sess-crash-submit", req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.error == "RESUME_PAST_PLAN_SEAM");
    REQUIRE(out.reconcile.duplicateSubmitRisk);
    REQUIRE(out.reconcile.submittedRunIds.count("run-crash-1") == 1);
    REQUIRE(out.reconcile.submittedRunIds.size() == 1);

    // The surface status doc carries the reconcile verdict (driver wire).
    OpsRunResult holder;
    holder.reconcile = out.reconcile;
    auto doc = sessionSurfaceStatus(out);
    REQUIRE(doc["reconcile"]["duplicate_submit_risk"].asBool());
    REQUIRE(doc["reconcile"]["submitted_run_ids"][0].asString() == "run-crash-1");

    // A pre-plan checkpointed journal WITHOUT submitted runs stays
    // duplicate-free: the flag keys on evidence, not on stage.
    sicnu::agent_loop::SessionJournal parked("sess-crash-preplan");
    REQUIRE(parked.append("stage_enter", "goal_normalization", {}, 1));
    REQUIRE(parked.append("stage_enter", "data_state_snapshot", {}, 2));
    REQUIRE(rec.persistJournal(parked, dir, &err));
    ResumeReconciler recon;
    auto before = recon.reconcileFile(dir, "sess-crash-preplan", &err);
    REQUIRE(before.resumable);
    REQUIRE_FALSE(before.duplicateSubmitRisk);
    auto pre = coord.resume(dir, "sess-crash-preplan", req);
    REQUIRE(pre.ok); // resumes cleanly to delivery
    REQUIRE(pre.delivery.outcome == "delivered");
}

TEST_CASE("agent_ops checkpoint persistence failure is surfaced, never silent",
          "[agent_ops][crash]")
{
    FakeScenario scenario;
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    // A FILE used as the journal directory: every persist (checkpoint and
    // terminal) fails; the session outcome stays authoritative but the
    // failure is on the wire.
    const std::string notADir = (std::filesystem::path(uniqueTemp("notadir")) / "plain-file").string();
    {
        std::ofstream f(notADir);
        f << "x";
    }

    OpsRunRequest req;
    req.session = ndviRequest();
    req.journalDirectory = notADir;
    auto out = coord.run(req);
    REQUIRE(out.session.ok); // the loop's outcome is untouched
    REQUIRE_FALSE(out.error.empty());       // terminal persist failure
    REQUIRE_FALSE(out.checkpointError.empty()); // mid-run checkpoint failure

    auto doc = sessionSurfaceStatus(out);
    REQUIRE_FALSE(doc["checkpoint_error"].asString().empty());
    REQUIRE(doc["error"].asString() == out.error);
}

TEST_CASE("agent_ops withheld science-changing repair becomes a typed question",
          "[agent_ops][approval]")
{
    FakeScenario scenario;
    // Preflight finds a science-changing fix and the policy refuses to
    // proceed unfixed: the session refuses, and the ask must survive on the
    // delivery wire instead of dying inside the decision log.
    scenario.preflight = {PreflightScript{"fixable", {proposal("qa_mask", "science_changing")}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE_FALSE(out.ok); // never executed past the withheld repair
    REQUIRE(seams.executorFake().beginCount() == 0);

    REQUIRE(out.delivery.questions.size() >= 1);
    bool sawAsk = false;
    for (const auto &q : out.delivery.questions)
    {
        if (q["kind"].asString() != "repair_approval")
            continue;
        sawAsk = true;
        REQUIRE(q["rule_id"].asString() == "qa_mask");
        REQUIRE(q["risk_class"].asString() == "science_changing");
        // The question anchors the loop's own evidence: the decision id of
        // the withhold record it was derived from.
        bool anchored = false;
        for (const auto &dec : out.session.summary.decisions)
            if (dec.decisionId == q["decision_id"].asString() &&
                dec.selected["action"].asString() == "withhold_repair")
                anchored = true;
        REQUIRE(anchored);
        REQUIRE(q["stage"].asString() == "repair_approval");
        REQUIRE(q["ask"].asString().find("qa_mask") != std::string::npos);
    }
    REQUIRE(sawAsk);

    // The capsule carries the ask too: an exported outcome must not drop
    // the pending science gate.
    DeliveryAssembler asmblr;
    auto cap = asmblr.capsuleExportDocument(out.delivery);
    REQUIRE(cap["questions"].size() == out.delivery.questions.size());
}

TEST_CASE("agent_ops recovery ask rides the delivery questions as well",
          "[agent_ops][approval]")
{
    FakeScenario scenario;
    // Verify FAIL, diagnose proposes nothing → session refuses; the
    // post-hoc recovery decision is ask (needs approval).
    scenario.verification = {VerifyScript{"FAIL", ""}};
    scenario.diagnosis = {DiagnoseScript{"CRS_MISMATCH", {}}};
    FakeSeams seams(scenario);
    OperationsCoordinator::Dependencies deps;
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    OpsRunRequest req;
    req.session = ndviRequest();
    auto out = coord.run(req);
    REQUIRE_FALSE(out.ok);
    REQUIRE(out.lastRecovery);
    REQUIRE(out.lastRecovery->action == recovery_action::kAsk);
    bool sawRecoveryAsk = false;
    for (const auto &q : out.delivery.questions)
        if (q["kind"].asString() == "recovery_ask")
        {
            sawRecoveryAsk = true;
            REQUIRE(q["reason_code"].asString() == "NEEDS_HUMAN");
        }
    REQUIRE(sawRecoveryAsk);
}

TEST_CASE("agent_ops projected repair plans are fail-closed on risk class",
          "[agent_ops][approval]")
{
    RecoveryBridge bridge;

    // (a) The diagnostic carries real risk evidence: the plan reflects it.
    OpDiagnostic science;
    science.code = "ops.diagnose.X";
    science.rootCauseCode = "X";
    science.proposals = {"qa_mask"};
    science.advisoryNext = recovery_action::kRepair; // route decide() to the repair gate
    Json::Value detail(Json::objectValue);
    detail["rule_id"] = "qa_mask";
    detail["risk_class"] = "science_changing";
    science.proposalDetails.append(detail);

    RecoveryBridge::PlanHints hints;
    hints.leadingRiskClass = "shape_preserving"; // must NOT win over evidence
    auto plan = bridge.projectRepairPlan(science, "ops", hints);
    REQUIRE(plan["selected"][0]["risk_class"].asString() == "science_changing");
    REQUIRE(plan["selected"][0]["risk"]["severity"].asString() == "high");
    REQUIRE(plan["policy"]["auto_executable"].asBool() == false);
    REQUIRE(plan["policy"]["actions"][0]["decision"].asString() == "needs_confirmation");

    // (b) Unknown risk class: STRICTEST class, never shape_preserving.
    OpDiagnostic unknown;
    unknown.code = "ops.diagnose.Y";
    unknown.rootCauseCode = "Y";
    unknown.proposals = {"mystery_rule"};
    Json::Value bogus(Json::objectValue);
    bogus["rule_id"] = "mystery_rule";
    bogus["risk_class"] = "harmless_looking";
    unknown.proposalDetails.append(bogus);
    auto plan2 = bridge.projectRepairPlan(unknown, "ops");
    REQUIRE(plan2["selected"][0]["risk_class"].asString() == "science_changing");

    // (c) No per-proposal evidence: the leading hint applies (validated).
    OpDiagnostic bare;
    bare.code = "ops.diagnose.Z";
    bare.rootCauseCode = "Z";
    bare.proposals = {"reproject"};
    RecoveryBridge::PlanHints shape;
    shape.leadingRiskClass = "shape_preserving";
    auto plan3 = bridge.projectRepairPlan(bare, "ops", shape);
    REQUIRE(plan3["selected"][0]["risk_class"].asString() == "shape_preserving");
    // Even the shape-preserving projection stays needs_confirmation: an
    // ops projection carries no executable action key.
    REQUIRE(plan3["policy"]["actions"][0]["decision"].asString() == "needs_confirmation");

    // (d) Round-trip: proposal_details survive OpDiagnostic serde.
    auto round = OpDiagnostic::fromJson(science.toJson());
    REQUIRE(round);
    REQUIRE(round->proposalDetails[0]["risk_class"].asString() == "science_changing");

    // (e) decide(): the proposal EVIDENCE wins over a permissive context —
    // a science-changing proposal with a context claiming shape_preserving
    // still asks; reverting decide() to the ctx-only comparison fails here.
    RecoveryContext permissive;
    permissive.leadingRiskClass = "shape_preserving";
    permissive.humanApprovedRepair = false;
    auto askScience = bridge.decide(science, permissive);
    REQUIRE(askScience.action == recovery_action::kAsk);
    REQUIRE(askScience.needsApproval);
    REQUIRE(askScience.reasonCode == "REPAIR_NEEDS_APPROVAL");

    // (f) Empty context (the wire default): unknown evidence asks too.
    RecoveryContext silent;
    auto askUnknown = bridge.decide(science, silent);
    REQUIRE(askUnknown.action == recovery_action::kAsk);
    REQUIRE(askUnknown.reasonCode == "REPAIR_NEEDS_APPROVAL");
TEST_CASE("agent_ops unified verifier report gates delivery claims fail-closed",
          "[agent_ops][delivery][verify]")
{
    DeliveryAssembler assembler;
    sicnu::agent_loop::SessionResult result;
    result.sessionId = "sess-unified";
    result.summary.outcome = "delivered";
    result.summary.verificationVerdict = "PASS";

    // A unified report whose overall is the engine's fail-closed lattice.
    Json::Value unifiedReport(Json::objectValue);
    unifiedReport["schema"] = "sicnu.verification.report/1";
    unifiedReport["specId"] = "spec.product";
    unifiedReport["overall"] = "indeterminate";
    Json::Value counts(Json::objectValue);
    counts["pass"] = 1;
    counts["fail"] = 0;
    counts["indeterminate"] = 1;
    unifiedReport["counts"] = counts;

    DeliveryExtras extras;
    extras.verificationReport = unifiedReport;

    // "indeterminate" can NEVER read as a success: the claim loses its
    // high confidence and names the non-passing evidence, while the
    // projection keeps the engine's own words.
    auto delivery = assembler.assemble(result, extras);
    REQUIRE(delivery.verifier["unified"]["overall"].asString() == "indeterminate");
    REQUIRE(delivery.verifier["unified"]["verdict"].asString() == "FAIL");
    REQUIRE(delivery.verifier["unified"]["spec_id"].asString() == "spec.product");
    REQUIRE(delivery.claims.size() == 1);
    REQUIRE(delivery.claims[0]["confidence"].asDouble() == 0.0);
    REQUIRE(delivery.claims[0]["evidence_missing"][0].asString() == "passing_verifier_verdict");

    // A passing unified report keeps the high-confidence claim.
    unifiedReport["overall"] = "pass";
    extras.verificationReport = unifiedReport;
    auto passed = assembler.assemble(result, extras);
    REQUIRE(passed.verifier["unified"]["verdict"].asString() == "PASS");
    REQUIRE(passed.claims[0]["confidence"].asDouble() == 0.9);

    // Without a unified report the legacy gate is untouched.
    auto legacy = assembler.assemble(result, {});
    REQUIRE(legacy.claims[0]["confidence"].asDouble() == 0.9);
    REQUIRE(legacy.verifier.isMember("unified") == false);

    // The assembled document round-trips with the unified record intact.
    auto round = FinalDelivery::fromJson(delivery.toJson());
    REQUIRE(round);
    REQUIRE(round->verifier["unified"]["overall"].asString() == "indeterminate");
}

TEST_CASE("agent_ops hostile unified report never crashes assembly", "[agent_ops][delivery][verify]")
{
    DeliveryAssembler assembler;
    sicnu::agent_loop::SessionResult result;
    result.sessionId = "sess-hostile";
    result.summary.outcome = "delivered";
    result.summary.verificationVerdict = "PASS";

    // A malformed unified report (overall is an object): assembly records a
    // non-pass instead of throwing or upgrading.
    DeliveryExtras extras;
    extras.verificationReport["schema"] = "sicnu.verification.report/1";
    extras.verificationReport["overall"]["bogus"] = true;

    auto delivery = assembler.assemble(result, extras);
    REQUIRE(delivery.verifier["unified"]["verdict"].asString() == "FAIL");
    REQUIRE(delivery.claims[0]["confidence"].asDouble() == 0.0);
}
