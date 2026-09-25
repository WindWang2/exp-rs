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
#include "agent_ops/repair_approval.h"
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
#include "repair_planner/repair_planner.h"
#include "repair_planner/repair_provider.h"
#include "repair_planner/repair_schema.h"

#include <filesystem>
#include <fstream>
#include <map>
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

/// Provider fake over real-shaped capability entries — the same discipline
/// as the repair planner completion suite (candidates come from entries,
/// never from the bridge).
class FakeCapabilityProvider : public sicnu::repair::RepairCapabilityProvider
{
  public:
    void add(const std::string &kind, const Json::Value &entry) { mFamilies[kind].append(entry); }

    std::vector<Json::Value> capabilitiesForRequirement(
        const std::string &requirementKind) const override
    {
        const auto it = mFamilies.find(requirementKind);
        if (it == mFamilies.end())
            return {};
        std::vector<Json::Value> out;
        for (const Json::Value &entry : it->second)
            out.push_back(entry);
        return out;
    }

    bool knowsRequirementKind(const std::string &requirementKind) const override
    {
        return mFamilies.count(requirementKind) > 0;
    }

  private:
    std::map<std::string, Json::Value> mFamilies;
};

Json::Value capabilityOf(const char *id, const char *family, const char *costClass)
{
    Json::Value entry(Json::objectValue);
    entry["id"] = id;
    entry["family"] = family;
    entry["resource"]["cost_class"] = costClass;
    return entry;
}

/// A failed-session diagnostic whose structured evidence carries a real
/// preflight issue — the shape the live BridgedDiagnoser produces.
OpDiagnostic preflightFixableDiagnostic(const std::string &code = "GRID_MISMATCH",
                                        const std::string &severity = "error")
{
    OpDiagnostic diag;
    diag.code = "ops.preflight.FIXABLE";
    diag.rootCauseCode = "PREFLIGHT_FIXABLE";
    diag.repairable = true;
    diag.advisoryNext = recovery_action::kRepair;
    diag.confidence = 0.9;
    Json::Value report(Json::objectValue);
    report["verdict"] = "fixable";
    Json::Value issues(Json::arrayValue);
    Json::Value issue(Json::objectValue);
    issue["code"] = code;
    issue["severity"] = severity;
    issue["message"] = "grid does not match the reference grid";
    issues.append(issue);
    report["issues"] = issues;
    diag.sources["preflight"] = report;
    return diag;
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
    FakeCapabilityProvider provider;
    provider.add("radiometric_state",
                 capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    RecoveryBridge bridge(&provider);
    auto diag = preflightFixableDiagnostic("INVALID_RADIOMETRY");
    diag.advisoryNext = recovery_action::kRepair;

    RecoveryContext ctx;
    ctx.leadingRiskClass = "science_changing";
    ctx.humanApprovedRepair = false;
    auto ask = bridge.decide(diag, ctx);
    REQUIRE(ask.action == recovery_action::kAsk);
    REQUIRE(ask.needsApproval);
    REQUIRE(ask.reasonCode == "REPAIR_NEEDS_APPROVAL");

    // Without a provider there is no provider-backed plan at all: the
    // honest decision is still ask, but for the typed no-plan reason —
    // nothing may proceed on a fabricated projection.
    RecoveryBridge bareBridge;
    OpDiagnostic noFindings;
    noFindings.code = "ops.preflight.FIXABLE";
    noFindings.rootCauseCode = "PREFLIGHT_FIXABLE";
    noFindings.repairable = true;
    noFindings.advisoryNext = recovery_action::kRepair;
    noFindings.proposals = {"reproject"};
    auto bareAsk = bareBridge.decide(noFindings, ctx);
    REQUIRE(bareAsk.action == recovery_action::kAsk);
    REQUIRE(bareAsk.needsApproval);
    REQUIRE(bareAsk.reasonCode == "NO_SAFE_REPAIR_PLAN");

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

    // approve_repair without a projected recovery plan is a typed refusal —
    // an approval must bind to a real plan, never arm a bare flag.
    Json::Value approveArgs(Json::objectValue);
    approveArgs["now_ms"] = Json::Int64(1000);
    approveArgs["ttl_ms"] = Json::Int64(5000);
    auto approved = sessionSurfaceApply(coord, "approve_repair", approveArgs);
    REQUIRE_FALSE(approved["ok"].asBool());
    REQUIRE(approved["error"].asString() == "NO_PENDING_REPAIR_PLAN");
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
// R3 repair-planner integration: the recovery projection is real planning
// over live capability knowledge, never fabricated facts.
// ---------------------------------------------------------------------------

TEST_CASE("recovery repair projection is provider-backed planning with real facts",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    provider.add("grid_align", capabilityOf("rs:align", "preprocess", "medium"));
    RecoveryBridge bridge(&provider);

    auto diag = preflightFixableDiagnostic();
    RecoveryContext ctx;

    auto decision = bridge.decide(diag, ctx);
    REQUIRE(decision.repairPlan.isObject());
    REQUIRE(decision.repairPlan["kind"].asString() == "repair_plan");
    REQUIRE(decision.repairPlan["status"].asString() == "planned");
    REQUIRE(decision.repairPlan["resolves_all_blockers"].asBool());

    // The candidate comes from the provider entry: operator id, cost rank
    // (light -> 2) and risk class are the entry's/contract's facts — nothing
    // invented by the bridge.
    const Json::Value &selected = decision.repairPlan["selected"];
    REQUIRE(selected.isArray());
    REQUIRE(selected.size() == 1);
    REQUIRE(selected[0]["operator_id"].asString() == "rs:resample");
    REQUIRE(selected[0]["risk_class"].asString() == "shape_preserving");
    REQUIRE(selected[0]["cost"]["rank"].asInt() == 2);
    REQUIRE(selected[0]["cost"]["cost_class"].asString() == "light");
    REQUIRE(selected[0]["kind"].asString() == "capability_ref");

    // The findings digest ties the plan to the exact findings it planned.
    REQUIRE(decision.repairPlan["provenance"]["findings_digest"].isString());
    REQUIRE(decision.repairPlan["provenance"]["findings_digest"].asString().size() == 16);

    // Determinism: the same findings + context project byte-identical plans.
    auto again = bridge.decide(diag, ctx);
    REQUIRE(sicnu::repair::jsonToString(again.repairPlan) ==
            sicnu::repair::jsonToString(decision.repairPlan));

    // Proceeding to repair records that a repair is NOT done until fresh
    // preflight + verification ran again.
    REQUIRE(decision.action == recovery_action::kRepair);
    REQUIRE(decision.toJson()["requires_reverification"].asBool());
}

TEST_CASE("without capability knowledge the recovery bridge plans nothing (fail-closed)",
          "[agent_ops][recovery][repair]")
{
    RecoveryBridge bridge; // no provider wired
    auto diag = preflightFixableDiagnostic();
    RecoveryContext ctx;

    auto decision = bridge.decide(diag, ctx);
    REQUIRE(decision.action == recovery_action::kAsk);
    REQUIRE(decision.reasonCode == "NO_SAFE_REPAIR_PLAN");
    REQUIRE(decision.needsApproval);
    REQUIRE(decision.repairPlan.isObject());
    REQUIRE(decision.repairPlan["status"].asString() == "no_safe_repair");
    const Json::Value &selected = decision.repairPlan["selected"];
    REQUIRE((!selected.isArray() || selected.empty()));
    REQUIRE(decision.repairPlan["no_safe_repair"]["cause"].asString() == "no_provider");
}

TEST_CASE("unsupported finding codes stay typed through the recovery projection",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    RecoveryBridge bridge(&provider);

    auto diag = preflightFixableDiagnostic("MYSTERY_CODE");
    RecoveryContext ctx;

    auto decision = bridge.decide(diag, ctx);
    // An unknown finding is never guessed into a similar-looking repair.
    REQUIRE(decision.repairPlan["status"].asString() == "no_safe_repair");
    REQUIRE(decision.repairPlan["unresolved"].isArray());
    REQUIRE(decision.repairPlan["unresolved"].size() == 1);
    REQUIRE(decision.repairPlan["unresolved"][0]["cause"].asString() ==
            "unsupported_finding");
    REQUIRE_FALSE(decision.repairPlan["resolves_all_blockers"].asBool());
    REQUIRE(decision.action == recovery_action::kAsk);
}

TEST_CASE("the plan's own risk class drives the science-changing gate, not the caller claim",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("radiometric_state",
                 capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    RecoveryBridge bridge(&provider);

    auto diag = preflightFixableDiagnostic("INVALID_RADIOMETRY");
    RecoveryContext ctx;
    // The caller claims a cheap shape-preserving repair; the projected plan
    // knows better (radiometric contract). The gate must follow the plan.
    ctx.leadingRiskClass = "shape_preserving";
    ctx.humanApprovedRepair = false;

    auto ask = bridge.decide(diag, ctx);
    REQUIRE(ask.action == recovery_action::kAsk);
    REQUIRE(ask.reasonCode == "REPAIR_NEEDS_APPROVAL");
    REQUIRE(ask.needsApproval);
    REQUIRE(ask.repairPlan["selected"][0]["risk_class"].asString() == "radiometric");

    // With an approval bound to THIS repair science the repair proceeds —
    // still radiometric — and still demands re-verification afterwards.
    const std::string plannedDigest =
        ask.repairPlan["provenance"]["findings_digest"].asString();
    REQUIRE_FALSE(plannedDigest.empty());
    ctx.humanApprovedRepair = true;
    // A bare bool (or an approval for a different finding set) never
    // satisfies the gate.
    auto wrongBinding = bridge.decide(diag, ctx);
    REQUIRE(wrongBinding.action == recovery_action::kAsk);
    REQUIRE(wrongBinding.reasonCode == "REPAIR_NEEDS_APPROVAL");
    REQUIRE(wrongBinding.approvalError == "APPROVAL_WRONG_PLAN");
    ctx.approvedFindingsDigest = plannedDigest;
    auto proceed = bridge.decide(diag, ctx);
    REQUIRE(proceed.action == recovery_action::kRepair);
    REQUIRE(proceed.repairPlan["selected"][0]["risk_class"].asString() == "radiometric");
    REQUIRE(proceed.toJson()["requires_reverification"].asBool());
}

TEST_CASE("a removed capability cannot revive through the recovery projection",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    RecoveryBridge bridge(&provider);
    auto diag = preflightFixableDiagnostic();
    RecoveryContext ctx;
    auto with = bridge.decide(diag, ctx);
    REQUIRE(with.repairPlan["status"].asString() == "planned");
    REQUIRE(with.repairPlan["selected"][0]["operator_id"].asString() == "rs:resample");

    // The knowledge layer no longer ships rs:resample: a provider built
    // from the shrunken entries cannot offer it — the projection refuses
    // instead of resurfacing a stale candidate, while the still-shipped
    // rs:align legitimately takes over.
    FakeCapabilityProvider shrunken;
    shrunken.add("grid_align", capabilityOf("rs:align", "preprocess", "medium"));
    RecoveryBridge bridgeWithout(&shrunken);
    auto without = bridgeWithout.decide(diag, ctx);
    REQUIRE(without.repairPlan["status"].asString() == "planned");
    const Json::Value &withoutPlan = without.repairPlan;
    REQUIRE(withoutPlan["selected"][0]["operator_id"].asString() == "rs:align");
    for (const Json::Value &alternative : withoutPlan["alternatives"])
        REQUIRE(alternative["operator_id"].asString() != "rs:resample");

    // And when NO shipped operator serves the kind any more, the plan
    // honestly reports no candidate instead of reviving one.
    FakeCapabilityProvider empty;
    RecoveryBridge bridgeEmpty(&empty);
    auto none = bridgeEmpty.decide(diag, ctx);
    REQUIRE(none.repairPlan["status"].asString() == "no_safe_repair");
    REQUIRE(none.repairPlan["unresolved"][0]["cause"].asString() == "no_candidate");
}

// ---------------------------------------------------------------------------
// R3 repair approval: a bound, expiring, single-use token — never a bare
// UI flag.
// ---------------------------------------------------------------------------

TEST_CASE("repair approval tokens bind findings, coordinator and clock window",
          "[agent_ops][approval]")
{
    const std::string findings = "aaaabbbbccccdddd";
    const Json::Value token = mintRepairApprovalToken(findings, 7, 1000, 5000);
    REQUIRE(token.isObject());
    REQUIRE(token["kind"].asString() == std::string(kRepairApprovalKind));
    REQUIRE(token["findings_digest"].asString() == findings);
    const auto verify = [](const Json::Value &doc, const std::string &digest,
                           long long coordinator, long long now) {
        return std::string(verifyRepairApprovalToken(doc, digest, coordinator, now));
    };
    REQUIRE(verify(token, findings, 7, 1000) == approval_check::kOk);
    REQUIRE(verify(token, findings, 7, 6000) == approval_check::kOk);

    // Expiry: past the window (and with no usable clock) the approval is
    // dead — fail closed, never "still valid".
    REQUIRE(verify(token, findings, 7, 6001) == approval_check::kExpired);
    REQUIRE(verify(token, findings, 7, 0) == approval_check::kExpired);

    // Cross-plan / cross-coordinator / tamper / malformed are typed, not
    // lumped into one bool.
    REQUIRE(verify(token, "ffffffffffffffff", 7, 2000) == approval_check::kWrongPlan);
    REQUIRE(verify(token, findings, 8, 2000) == approval_check::kWrongCoordinator);
    Json::Value tampered = token;
    tampered["expires_at_ms"] = Json::Int64(999999);
    REQUIRE(verify(tampered, findings, 7, 2000) == approval_check::kTampered);
    Json::Value digestFlip = token;
    digestFlip["digest"] = "0000000000000000";
    REQUIRE(verify(digestFlip, findings, 7, 2000) == approval_check::kTampered);
    REQUIRE(verify(Json::Value(), findings, 7, 2000) == approval_check::kMalformed);

    // Minting never invents an approval: unusable arguments produce nothing.
    CHECK(mintRepairApprovalToken("", 7, 1000, 5000).isNull());
    CHECK(mintRepairApprovalToken(findings, 0, 1000, 5000).isNull());
    CHECK(mintRepairApprovalToken(findings, 7, 1000, 0).isNull());
}

TEST_CASE("coordinator repair approval: arm, consume once, replay refused",
          "[agent_ops][approval]")
{
    OperationsCoordinator::Dependencies deps;
    FakeScenario scenario;
    FakeSeams seams(scenario);
    deps.seams = makeDeps(seams);
    OperationsCoordinator coord(deps);

    // No plan projected yet: there is nothing an approval could bind to.
    Json::Value premature = mintRepairApprovalToken("aaaabbbbccccdddd",
                                                    coord.instanceId(), 1000, 5000);
    REQUIRE(coord.armRepairApproval(premature, 1000) == "NO_PENDING_REPAIR_PLAN");

    // Driver flow: evaluateRecovery projects a plan; the surface binds the
    // approval to exactly that repair science (findings digest).
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    OperationsCoordinator::Dependencies wiredDeps;
    FakeScenario wiredScenario;
    wiredScenario.verification = {{"FAIL", ""}};
    wiredScenario.execution = {{false, "VERIFY_FAILED", {}}};
    FakeSeams wiredSeams(wiredScenario);
    wiredDeps.seams = makeDeps(wiredSeams);
    wiredDeps.repairCapabilityProvider = &provider;
    OperationsCoordinator wired(wiredDeps);

    OpsRunRequest failedReq;
    failedReq.session = ndviRequest();
    auto failed = wired.run(failedReq);
    REQUIRE_FALSE(failed.ok);
    RecoveryContext ctx;
    auto decision = wired.evaluateRecovery(preflightFixableDiagnostic(), ctx);
    REQUIRE(decision.repairPlan["status"].asString() == "planned");
    const std::string planDigest =
        decision.repairPlan["provenance"]["findings_digest"].asString();
    REQUIRE(wired.lastProjectedFindingsDigest() == planDigest);

    // A token naming a different finding set is refused before anything
    // arms.
    REQUIRE(wired.armRepairApproval(
                mintRepairApprovalToken("ffffffffffffffff", wired.instanceId(), 1000,
                                        5000),
                1000) == "APPROVAL_WRONG_PLAN");
    REQUIRE_FALSE(wired.hasPendingRepairApproval());

    // A token from ANOTHER coordinator cannot arm here.
    OperationsCoordinator::Dependencies otherDeps;
    FakeSeams otherSeams(scenario);
    otherDeps.seams = makeDeps(otherSeams);
    OperationsCoordinator otherCoord(otherDeps);
    Json::Value foreign = mintRepairApprovalToken(planDigest, otherCoord.instanceId(),
                                                  1000, 5000);
    REQUIRE(wired.armRepairApproval(foreign, 1000) == "APPROVAL_WRONG_COORDINATOR");

    // Expiry at arm time is refused.
    Json::Value token = mintRepairApprovalToken(planDigest, wired.instanceId(), 1000, 5000);
    REQUIRE(wired.armRepairApproval(token, 1000 + 5001) == "APPROVAL_EXPIRED");

    // Arming ok, then the next launch consumes it — whatever the outcome.
    REQUIRE(wired.armRepairApproval(token, 2000).empty());
    REQUIRE(wired.hasPendingRepairApproval());
    OpsRunRequest consumeReq;
    consumeReq.session = ndviRequest();
    consumeReq.approvalNowMs = 3000;
    auto consumedRun = wired.run(consumeReq);
    REQUIRE_FALSE(wired.hasPendingRepairApproval());

    // Replay of the SAME consumed token is refused even though its digest
    // is still intact: one approval authorizes exactly one launch.
    REQUIRE(wired.armRepairApproval(token, 4000) == "APPROVAL_REPLAYED");

    // An armed token that expires before the launch fails closed at
    // consumption: the run happens WITHOUT the approval and the refusal is
    // surfaced (and projected on the session-surface wire). A fresh
    // projection restores the binding target first — after a run whose
    // decision carried no plan, arming against the stale digest is refused.
    // A genuinely NEW token (different clock window -> different digest)
    // for the stale digest is refused on the binding, not on replay.
    Json::Value staleDigestToken = mintRepairApprovalToken(planDigest,
                                                           wired.instanceId(), 4500,
                                                           5000);
    REQUIRE(wired.armRepairApproval(staleDigestToken, 5000) ==
            "NO_PENDING_REPAIR_PLAN");
    RecoveryContext refreshCtx;
    auto refreshed =
        wired.evaluateRecovery(preflightFixableDiagnostic(), refreshCtx);
    REQUIRE(wired.lastProjectedFindingsDigest() == planDigest);
    Json::Value shortToken2 = mintRepairApprovalToken(planDigest, wired.instanceId(),
                                                      5000, 100);
    REQUIRE(wired.armRepairApproval(shortToken2, 5000).empty());
    OpsRunRequest expiredReq;
    expiredReq.session = ndviRequest();
    expiredReq.approvalNowMs = 99999;
    auto expiredRun = wired.run(expiredReq);
    REQUIRE(expiredRun.approvalError == "APPROVAL_EXPIRED");
    REQUIRE_FALSE(wired.hasPendingRepairApproval());

    // The evaluateRecovery flow consumes the armed token itself: the second
    // evaluation derives the approval from the token (never from a bare
    // ctx bool), and the deterministic re-projection of the SAME findings
    // is the same binding target.
    OperationsCoordinator::Dependencies flowDeps;
    FakeSeams flowSeams(scenario);
    flowDeps.seams = makeDeps(flowSeams);
    FakeCapabilityProvider flowProvider;
    flowProvider.add("radiometric_state",
                     capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    flowDeps.repairCapabilityProvider = &flowProvider;
    OperationsCoordinator flow(flowDeps);
    RecoveryContext firstCtx;
    auto firstDecision =
        flow.evaluateRecovery(preflightFixableDiagnostic("INVALID_RADIOMETRY"), firstCtx);
    REQUIRE(firstDecision.action == recovery_action::kAsk);
    REQUIRE(firstDecision.reasonCode == "REPAIR_NEEDS_APPROVAL");
    const std::string radiometricDigest =
        firstDecision.repairPlan["provenance"]["findings_digest"].asString();
    Json::Value flowToken =
        mintRepairApprovalToken(radiometricDigest, flow.instanceId(), 1000, 50000);
    REQUIRE(flow.armRepairApproval(flowToken, 1000).empty());
    RecoveryContext secondCtx;
    secondCtx.humanApprovedRepair = false; // bare bool never trusted
    secondCtx.approvalNowMs = 2000;
    auto secondDecision = flow.evaluateRecovery(
        preflightFixableDiagnostic("INVALID_RADIOMETRY"), secondCtx);
    REQUIRE(secondDecision.action == recovery_action::kRepair);
    REQUIRE(secondCtx.approvedFindingsDigest == radiometricDigest);
    REQUIRE_FALSE(flow.hasPendingRepairApproval());

    // An approval minted for one finding set cannot arm the repair of
    // another: the binding is the findings digest, enforced at the gate.
    OperationsCoordinator::Dependencies crossDeps;
    FakeSeams crossSeams(scenario);
    crossDeps.seams = makeDeps(crossSeams);
    FakeCapabilityProvider crossProvider; // both families plannable
    crossProvider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    crossProvider.add("radiometric_state",
                      capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    crossDeps.repairCapabilityProvider = &crossProvider;
    OperationsCoordinator cross(crossDeps);
    RecoveryContext gridCtx;
    auto gridDecision =
        cross.evaluateRecovery(preflightFixableDiagnostic("GRID_MISMATCH"), gridCtx);
    const std::string gridDigest =
        gridDecision.repairPlan["provenance"]["findings_digest"].asString();
    Json::Value gridToken =
        mintRepairApprovalToken(gridDigest, cross.instanceId(), 1000, 50000);
    REQUIRE(cross.armRepairApproval(gridToken, 1000).empty());
    RecoveryContext radioCtx;
    radioCtx.approvalNowMs = 2000;
    auto radioDecision = cross.evaluateRecovery(
        preflightFixableDiagnostic("INVALID_RADIOMETRY"), radioCtx);
    REQUIRE(radioDecision.action == recovery_action::kAsk);
    REQUIRE(radioDecision.reasonCode == "REPAIR_NEEDS_APPROVAL");
    REQUIRE(radioDecision.approvalError == "APPROVAL_WRONG_PLAN");
}
TEST_CASE("the surface approve_repair action mints and arms a bound token",
          "[agent_ops][approval][surface]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    OperationsCoordinator::Dependencies deps;
    FakeScenario scenario;
    FakeSeams seams(scenario);
    deps.seams = makeDeps(seams);
    deps.repairCapabilityProvider = &provider;
    OperationsCoordinator coord(deps);

    // Without a projected plan: typed refusal, not a silent flag.
    auto noPlan = sessionSurfaceApply(coord, "approve_repair",
                                      Json::Value(Json::objectValue));
    REQUIRE_FALSE(noPlan["ok"].asBool());
    REQUIRE(noPlan["error"].asString() == "NO_PENDING_REPAIR_PLAN");

    RecoveryContext ctx;
    auto decision = coord.evaluateRecovery(preflightFixableDiagnostic(), ctx);
    REQUIRE(decision.repairPlan["status"].asString() == "planned");

    // A driver claiming a finding set the coordinator never projected is
    // refused before minting.
    Json::Value wrongPlanArgs(Json::objectValue);
    wrongPlanArgs["findings_digest"] = "ffffffffffffffff";
    wrongPlanArgs["now_ms"] = Json::Int64(1000);
    wrongPlanArgs["ttl_ms"] = Json::Int64(5000);
    auto wrong = sessionSurfaceApply(coord, "approve_repair", wrongPlanArgs);
    REQUIRE_FALSE(wrong["ok"].asBool());
    REQUIRE(wrong["error"].asString() == "APPROVAL_WRONG_PLAN");

    // The honest flow: no digest argument (approve what you last saw),
    // clock window from the driver, token bound to the projected science.
    Json::Value args(Json::objectValue);
    args["now_ms"] = Json::Int64(1000);
    args["ttl_ms"] = Json::Int64(5000);
    auto approved = sessionSurfaceApply(coord, "approve_repair", args);
    REQUIRE(approved["ok"].asBool());
    REQUIRE(approved["findings_digest"].asString() ==
            coord.lastProjectedFindingsDigest());
    REQUIRE(approved["repair_approval"].isObject());
    REQUIRE(coord.hasPendingRepairApproval());
    REQUIRE(std::string(verifyRepairApprovalToken(approved["repair_approval"],
                                                  coord.lastProjectedFindingsDigest(),
                                                  coord.instanceId(), 6000)) ==
            approval_check::kOk);
}

TEST_CASE("a mixed-risk plan is approval-gated as a whole (max risk over all candidates)",
          "[agent_ops][recovery][repair]")
{
    // A plan is executed whole: one radiometric candidate among
    // shape-preserving ones makes the WHOLE launch approval-gated — the
    // gate reads the maximum risk over every selected candidate, not just
    // the first.
    FakeCapabilityProvider provider;
    provider.add("crs_align", capabilityOf("gdal:reproject", "grid", "medium"));
    provider.add("radiometric_state",
                 capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    RecoveryBridge bridge(&provider);

    auto diag = preflightFixableDiagnostic("CRS_MISMATCH");
    Json::Value &issues = diag.sources["preflight"]["issues"];
    Json::Value second(Json::objectValue);
    second["code"] = "INVALID_RADIOMETRY";
    second["severity"] = "error";
    second["message"] = "mixed radiometric domains";
    issues.append(second);

    RecoveryContext ctx;
    auto decision = bridge.decide(diag, ctx);
    REQUIRE(decision.repairPlan["status"].asString() == "planned");
    REQUIRE(decision.repairPlan["selected"].size() == 2);
    REQUIRE(decision.repairPlan["selected"][0]["risk_class"].asString() ==
            "shape_preserving");
    REQUIRE(decision.repairPlan["selected"][1]["risk_class"].asString() == "radiometric");
    REQUIRE(decision.action == recovery_action::kAsk);
    REQUIRE(decision.reasonCode == "REPAIR_NEEDS_APPROVAL");
    REQUIRE(decision.needsApproval);

    // With the approval bound to this plan's findings, the whole plan —
    // both candidates — may proceed.
    ctx.humanApprovedRepair = true;
    ctx.approvedFindingsDigest =
        decision.repairPlan["provenance"]["findings_digest"].asString();
    auto proceed = bridge.decide(diag, ctx);
    REQUIRE(proceed.action == recovery_action::kRepair);
    REQUIRE(proceed.toJson()["requires_reverification"].asBool());
}

TEST_CASE("a code-bearing issue without usable severity refuses to plan (never dropped)",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    RecoveryBridge bridge(&provider);

    // The severity-less issue cannot be classified: evidence that names
    // itself is never silently dropped, and the plannable sibling does not
    // excuse it — the typed no_safe_repair puts the human back in charge.
    auto diag = preflightFixableDiagnostic("GRID_MISMATCH");
    Json::Value &issues = diag.sources["preflight"]["issues"];
    issues[0].removeMember("severity");
    Json::Value second(Json::objectValue);
    second["code"] = "CRS_MISMATCH";
    second["severity"] = "error";
    second["message"] = "plannable sibling";
    issues.append(second);

    RecoveryContext ctx;
    auto decision = bridge.decide(diag, ctx);
    REQUIRE(decision.action == recovery_action::kAsk);
    REQUIRE(decision.repairPlan["status"].asString() == "no_safe_repair");
    REQUIRE(decision.repairPlan["no_safe_repair"]["cause"].asString() ==
            "invalid_findings");
}

TEST_CASE("a hostile findings flood stays bounded and deterministic",
          "[agent_ops][recovery][repair]")
{
    FakeCapabilityProvider provider;
    provider.add("grid_align", capabilityOf("rs:resample", "preprocess", "light"));
    RecoveryBridge bridge(&provider);

    auto diag = preflightFixableDiagnostic("GRID_MISMATCH");
    Json::Value &issues = diag.sources["preflight"]["issues"];
    for (int i = 0; i < 4000; ++i)
    {
        Json::Value flood(Json::objectValue);
        flood["code"] = "GRID_MISMATCH";
        flood["severity"] = "warning";
        flood["message"] = "flood";
        issues.append(flood);
    }

    RecoveryContext ctx;
    auto decision = bridge.decide(diag, ctx);
    // The bridge bounds its input before planning; the plan is still
    // produced deterministically and reports the visible truncation.
    REQUIRE(decision.repairPlan["status"].asString() == "planned");
    REQUIRE(decision.repairPlan["bounds"]["requirements_truncated"].asBool());
    auto again = bridge.decide(diag, ctx);
    REQUIRE(sicnu::repair::jsonToString(again.repairPlan) ==
            sicnu::repair::jsonToString(decision.repairPlan));
}

TEST_CASE("round-2 residuals: forged ctx refused, direct token symmetric, pause keeps arming",
          "[agent_ops][approval]")
{
    FakeCapabilityProvider provider;
    provider.add("radiometric_state",
                 capabilityOf("rs:radiometric_calibration", "preprocess", "medium"));
    OperationsCoordinator::Dependencies deps;
    FakeScenario scenario;
    FakeSeams seams(scenario);
    deps.seams = makeDeps(seams);
    deps.repairCapabilityProvider = &provider;
    OperationsCoordinator coord(deps);

    // R1: with nothing armed, a forged caller ctx (bool + digest read off
    // the wire) is refused — the token is the only human gate.
    RecoveryContext forgedCtx;
    forgedCtx.approvalNowMs = 1000;
    auto probe = coord.evaluateRecovery(preflightFixableDiagnostic("INVALID_RADIOMETRY"),
                                        forgedCtx);
    REQUIRE(probe.action == recovery_action::kAsk);
    REQUIRE(probe.reasonCode == "REPAIR_NEEDS_APPROVAL");
    const std::string digest =
        probe.repairPlan["provenance"]["findings_digest"].asString();
    RecoveryContext forged;
    forged.humanApprovedRepair = true;
    forged.approvedFindingsDigest = digest;
    forged.approvalNowMs = 1000;
    auto refused = coord.evaluateRecovery(
        preflightFixableDiagnostic("INVALID_RADIOMETRY"), forged);
    REQUIRE(refused.action == recovery_action::kAsk);
    REQUIRE(refused.approvalError == "APPROVAL_REQUIRED");
    REQUIRE_FALSE(forged.humanApprovedRepair);

    // The honest flow arms the token; the SAME evaluation then proceeds.
    Json::Value token = mintRepairApprovalToken(digest, coord.instanceId(), 1000, 50000);
    REQUIRE(coord.armRepairApproval(token, 1000).empty());
    RecoveryContext honest;
    honest.approvalNowMs = 2000;
    auto proceed = coord.evaluateRecovery(
        preflightFixableDiagnostic("INVALID_RADIOMETRY"), honest);
    REQUIRE(proceed.action == recovery_action::kRepair);

    // R2: the direct request.repairApproval path is symmetric — a consumed
    // token presented with the request is a replay, never a second use.
    coord.armRepairApproval(token, 3000); // re-arm of the consumed token: replayed
    REQUIRE(coord.armRepairApproval(token, 3000) == "APPROVAL_REPLAYED");
    OpsRunRequest directReq;
    directReq.session = ndviRequest();
    directReq.repairApproval = token;
    directReq.approvalNowMs = 3000;
    auto directRun = coord.run(directReq);
    REQUIRE(directRun.approvalError == "APPROVAL_REPLAYED");
    REQUIRE_FALSE(coord.hasPendingRepairApproval());

    // Pause gates the launch BEFORE approval consumption: the armed state
    // survives a paused launch for the next one.
    OperationsCoordinator::Dependencies pauseDeps;
    FakeSeams pauseSeams(scenario);
    pauseDeps.seams = makeDeps(pauseSeams);
    pauseDeps.repairCapabilityProvider = &provider;
    OperationsCoordinator paused(pauseDeps);
    RecoveryContext pauseCtx;
    auto pauseProbe = paused.evaluateRecovery(
        preflightFixableDiagnostic("INVALID_RADIOMETRY"), pauseCtx);
    const std::string pauseDigest =
        pauseProbe.repairPlan["provenance"]["findings_digest"].asString();
    Json::Value pauseToken =
        mintRepairApprovalToken(pauseDigest, paused.instanceId(), 1000, 50000);
    REQUIRE(paused.armRepairApproval(pauseToken, 1000).empty());
    paused.requestPause();
    OpsRunRequest pausedReq;
    pausedReq.session = ndviRequest();
    pausedReq.approvalNowMs = 2000;
    auto pausedRun = paused.run(pausedReq);
    REQUIRE_FALSE(pausedRun.ok);
    REQUIRE(pausedRun.error == "PAUSED");
    REQUIRE(paused.hasPendingRepairApproval());
    paused.clearPause();
    auto resumedRun = paused.run(pausedReq);
    REQUIRE_FALSE(paused.hasPendingRepairApproval());
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
