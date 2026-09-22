// tests/test_agent_loop_e2e.cpp
//
// RS14-11 Evidence-first Agent Loop — Slices D/E/F: the full loop end to
// end on deterministic offline doubles.
//
//   D: execute -> verify -> delivered (incl. warnings, teaching gate,
//      FAIL-can-never-be-success)
//   E: verify FAIL -> diagnose -> bounded replan -> delivered / refused
//   F: no-progress detection, replan budget, resource budget, cancellation
//
// No network, no LLM, no workflow engine: the fake seams stand in for the
// production adapter (src/agent/tools/agent_session_adapter.*).

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>
#include <json/writer.h>

#include "agent_loop/scientific_agent_session.h"
#include "agent_loop/fake_seams.h"

#include <string>
#include <vector>

using namespace sicnu::agent_loop;

namespace {

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "commentStyle" ] = "None";
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

ScientificAgentSession::Dependencies makeDependencies( FakeSeams &seams )
{
    return { &seams.dataProvider(), &seams.planner(), &seams.preflight(), &seams.executor(),
             &seams.verifier(), &seams.diagnoser() };
}

SessionRunRequest makeRequest()
{
    SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    return request;
}

RepairProposal proposal( const std::string &ruleId, const std::string &riskClass )
{
    RepairProposal p;
    p.ruleId = ruleId;
    p.riskClass = riskClass;
    p.operatorId = "rs:" + ruleId;
    p.rationale = "test";
    return p;
}

bool hasDecision( const SessionResult &result, const std::string &stage,
                  const std::string &action )
{
    for ( const DecisionRecord &decision : result.summary.decisions )
        if ( decision.stage == stage && decision.selected[ "action" ].asString() == action )
            return true;
    return false;
}

bool hasStage( const SessionResult &result, const std::string &stage )
{
    for ( const std::string &visited : result.summary.stages )
        if ( visited == stage )
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Slice D — execute → verify → delivered
// ---------------------------------------------------------------------------

TEST_CASE( "happy path delivers after a verified execution", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "" } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-happy" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( result.stopReason.empty() );

    const std::vector< std::string > expectedStages = {
        stages::kGoalNormalization, stages::kDataStateSnapshot, stages::kPlanRequest,
        stages::kPreflight,         stages::kExecute,           stages::kVerify,
        stages::kDelivery,
    };
    REQUIRE( result.summary.stages == expectedStages );

    REQUIRE( result.summary.verificationVerdict == "PASS" );
    REQUIRE( result.summary.artifacts.size() == 1 );
    REQUIRE( result.summary.artifacts[ 0 ] == "ndvi.tif" );
    REQUIRE( hasDecision( result, stages::kExecute, "verify" ) );
    REQUIRE( hasDecision( result, stages::kVerify, "deliver" ) );
    REQUIRE( hasDecision( result, stages::kDelivery, "deliver" ) );

    // Budgets are reported honestly.
    REQUIRE( result.summary.budgets[ "replans_used" ].asInt() == 0 );
    REQUIRE( result.summary.budgets[ "replan_limit" ].asInt() == policy.maxReplans );
    REQUIRE( result.summary.budgets[ "plan_estimate_mb" ].asInt() == 256 );

    // The summary document is complete and machine-readable.
    const Json::Value doc = result.summary.toJson();
    REQUIRE( doc[ "schema_version" ].asString() == "1.0" );
    REQUIRE( doc[ "outcome" ].asString() == "delivered" );
    REQUIRE( doc[ "decisions" ].isArray() );
    REQUIRE( doc[ "decisions" ].size() == result.summary.decisions.size() );
}

TEST_CASE( "pass-with-warnings delivers without silently upgrading", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "PASS", "nodata_fraction_high" } };
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-warn" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( result.summary.verificationVerdict == "PASS_WITH_WARNINGS" );

    // The warning is recorded in the verify decision's evidence — never
    // dropped, never upgraded to a clean PASS.
    bool warningEvidence = false;
    for ( const DecisionRecord &decision : result.summary.decisions )
    {
        if ( decision.stage != stages::kVerify )
            continue;
        for ( const DecisionEvidence &ev : decision.evidence )
            if ( ev.kind == "verification" && ev.ref.find( "nodata_fraction_high" ) != std::string::npos )
                warningEvidence = true;
    }
    REQUIRE( warningEvidence );
}

TEST_CASE( "teaching mode withholds execution; agent mode is unchanged", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };

    SessionPolicy teaching = SessionPolicy::defaults();
    teaching.mode = RunMode::ExecuteWithVerify;
    teaching.teaching.intentDomain = "lab";
    teaching.teaching.role = "student"; // pre-normalized by the adapter
    FakeSeams teachingSeams( scenario );
    ScientificAgentSession teachingSession( teaching, makeDependencies( teachingSeams ), {},
                                            "sess-e2e-student" );

    const SessionResult student = teachingSession.run( makeRequest() );

    REQUIRE_FALSE( student.ok );
    REQUIRE( student.terminalState == terminal_states::kRefused );
    REQUIRE( student.stopReason == stop_reasons::kTeachingRefusal );
    REQUIRE( teachingSeams.executorFake().beginCount() == 0 );
    REQUIRE( teachingSeams.verifierFake().verifyCount() == 0 );
    REQUIRE( hasDecision( student, stages::kExecute, "withhold_execution" ) );

    // Same scenario, agent role: identical machine, execution proceeds.
    SessionPolicy agent = teaching;
    agent.teaching.role = "agent";
    FakeSeams agentSeams( scenario );
    ScientificAgentSession agentSession( agent, makeDependencies( agentSeams ), {},
                                         "sess-e2e-agent" );
    const SessionResult agentRun = agentSession.run( makeRequest() );

    REQUIRE( agentRun.ok );
    REQUIRE( agentRun.terminalState == terminal_states::kDelivered );
    REQUIRE( agentSeams.executorFake().beginCount() == 1 );
    REQUIRE( agentRun.summary.verificationVerdict == "PASS" );

    // Parity: the student run walks exactly the same stages as the agent
    // run up to the gate — the gate adds no stage and skips none.
    REQUIRE( student.summary.stages.size() < agentRun.summary.stages.size() );
    for ( std::size_t i = 0; i < student.summary.stages.size(); ++i )
        REQUIRE( student.summary.stages[ i ] == agentRun.summary.stages[ i ] );
    REQUIRE( hasDecision( agentRun, stages::kExecute, "run" ) );
    REQUIRE_FALSE( hasDecision( agentRun, stages::kExecute, "withhold_execution" ) );
}

// ---------------------------------------------------------------------------
// Slice E — verify fail → diagnose → bounded replan
// ---------------------------------------------------------------------------

TEST_CASE( "verify failure diagnoses, replans with an approved repair, and delivers",
           "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } }, { true, "", { "ndvi-aligned.tif" } } };
    // Attempt 1 verifies FAIL; attempt 2 (after the repair) passes.
    scenario.verification = { { "FAIL", "extent_short" }, { "PASS", "" } };
    scenario.diagnosis = { { "GRID_MISMATCH",
                             { proposal( "align_to_reference", "shape_preserving" ) } } };
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-replan" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( result.summary.verificationVerdict == "PASS" );

    // The loop ran exactly once: diagnose -> replan -> plan -> preflight ->
    // execute -> verify.
    REQUIRE( result.summary.replay[ "replan_count" ].asInt() == 1 );
    REQUIRE( hasStage( result, stages::kDiagnose ) );
    REQUIRE( hasStage( result, stages::kReplan ) );
    REQUIRE( hasDecision( result, stages::kDiagnose, "replan" ) );
    REQUIRE( hasDecision( result, stages::kReplan, "replan" ) );
    REQUIRE( seams.executorFake().beginCount() == 2 );
    REQUIRE( seams.verifierFake().verifyCount() == 2 );

    // The approved repair is what makes attempt 2 a DIFFERENT plan.
    REQUIRE( result.summary.budgets[ "replans_used" ].asInt() == 1 );
    REQUIRE( result.summary.budgets[ "replans_used" ].asInt() <= policy.maxReplans );
}

TEST_CASE( "a diagnosis with no repair proposal refuses with the typed failure",
           "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "FAIL", "all_nodata" } };
    scenario.diagnosis = { { "OUTPUT_INVALID", {} } }; // nothing to propose
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-norepair" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kOutputInvalid );
    REQUIRE( hasDecision( result, stages::kDiagnose, "refuse" ) );
    // No blind retry: exactly one execution attempt.
    REQUIRE( seams.executorFake().beginCount() == 1 );
}

TEST_CASE( "diagnosis repairs that are not auto-approved are withheld, not applied",
           "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "FAIL", "dn_values" } };
    // A radiometric repair changes pixel semantics: never auto-applied.
    scenario.diagnosis = { { "INVALID_RADIOMETRY",
                             { proposal( "calibrate_toa", "radiometric" ) } } };
    SessionPolicy policy = SessionPolicy::defaults();
    REQUIRE_FALSE( policy.repairApproval.allowUnapprovedFixable );
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-withhold" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kOutputInvalid );
    REQUIRE( hasDecision( result, stages::kDiagnose, "withhold_repair" ) );
    REQUIRE( hasDecision( result, stages::kDiagnose, "refuse" ) );
    REQUIRE( seams.executorFake().beginCount() == 1 );
}

TEST_CASE( "the replan budget aborts the loop with a typed reason", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    // Every attempt fails; every diagnosis proposes an auto-approved repair.
    scenario.execution = { { false, "EXECUTION_FAILED", {} } };
    scenario.diagnosis = { { "GRID_MISMATCH",
                             { proposal( "align_to_reference", "shape_preserving" ) } } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.maxReplans = 2;
    policy.noProgressThreshold = 5; // high: the replan limit must fire first
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-limit" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kAborted );
    REQUIRE( result.stopReason == stop_reasons::kReplanLimit );
    REQUIRE( hasDecision( result, stages::kReplan, "abort" ) );
    // maxReplans executed replans, then the budget refused one more.
    REQUIRE( result.summary.replay[ "replan_count" ].asInt() == 3 );
    REQUIRE( result.summary.budgets[ "replans_used" ].asInt() == 3 );
    REQUIRE( result.summary.budgets[ "replan_limit" ].asInt() == 2 );
}

// ---------------------------------------------------------------------------
// Slice F — no-progress / resource budget / cancellation
// ---------------------------------------------------------------------------

TEST_CASE( "repeating the same failing science aborts as no-progress", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { false, "EXECUTION_FAILED", {} } };
    // The same repair is proposed every time: after it is applied once the
    // plan identity stops moving, and the same failure repeats.
    scenario.diagnosis = { { "GRID_MISMATCH",
                             { proposal( "align_to_reference", "shape_preserving" ) } } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.noProgressThreshold = 2;
    policy.maxReplans = 5; // generous: no-progress must fire first
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-noprogress" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kAborted );
    REQUIRE( result.stopReason == stop_reasons::kNoProgress );
    REQUIRE( hasDecision( result, stages::kReplan, "abort" ) );
    // The decision names the exact repeated failure key.
    bool namesKey = false;
    for ( const DecisionRecord &decision : result.summary.decisions )
        if ( decision.stage == stages::kReplan && decision.selected[ "action" ] == "abort" &&
             decision.reason.find( "same science" ) != std::string::npos )
            namesKey = true;
    REQUIRE( namesKey );
}

TEST_CASE( "genuinely different replans do not trigger no-progress", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { false, "EXECUTION_FAILED", {} } };
    // Each diagnosis proposes a DIFFERENT repair: the science moves.
    scenario.diagnosis = {
        { "GRID_MISMATCH", { proposal( "align_to_reference", "shape_preserving" ) } },
        { "CRS_MISMATCH", { proposal( "reproject_to_target", "shape_preserving" ) } },
    };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.noProgressThreshold = 2;
    policy.maxReplans = 1;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-moving" );

    const SessionResult result = session.run( makeRequest() );

    // The loop stops on the replan BUDGET, never on a false no-progress.
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kAborted );
    REQUIRE( result.stopReason == stop_reasons::kReplanLimit );
    REQUIRE( seams.executorFake().beginCount() == 2 );
}

TEST_CASE( "a plan over the resource budget aborts before execution", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.resourceBudgetMb = 100; // the plan declares 256 MB
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-budget" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kAborted );
    REQUIRE( result.stopReason == stop_reasons::kResourceOverBudget );
    REQUIRE( hasDecision( result, stages::kPlanRequest, "abort" ) );
    REQUIRE( seams.executorFake().beginCount() == 0 );
    REQUIRE( seams.preflightFake().checkCount() == 0 );
}

TEST_CASE( "cancellation stops the session at a stage boundary", "[agent_loop][e2e]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession::Dependencies deps = makeDependencies( seams );

    // The injected clock doubles as the cancellation trigger: on the 3rd
    // tick (while the plan stage is being journalled) the "user" cancels,
    // so the session must stop before the executor ever runs.
    ScientificAgentSession session( policy, deps, [ &session, tick = 0 ]() mutable -> long long {
        const long long at = tick++;
        if ( at == 3 )
            session.requestCancel();
        return at;
    }, "sess-e2e-cancel" );
    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kAborted );
    REQUIRE( result.stopReason == stop_reasons::kCancelled );
    REQUIRE( seams.executorFake().beginCount() == 0 );
    // The cancellation itself is visible in the journal.
    bool terminalNoted = false;
    for ( const JournalEntry &entry : result.journal.entries() )
        if ( entry.event == "terminal" && entry.stage == terminal_states::kAborted )
            terminalNoted = true;
    REQUIRE( terminalNoted );
}

TEST_CASE( "a session that never executed never claims success", "[agent_loop][e2e]" )
{
    // Defense in depth for the FAIL-never-success rule: even if every seam
    // misbehaved, a session without a PASS verification cannot report
    // delivered-success with artifacts.
    FakeScenario scenario;
    scenario.execution = { { true, "", { "ndvi.tif" } } };
    scenario.verification = { { "FAIL", "bad" } };
    scenario.diagnosis = { { "OUTPUT_INVALID", {} } };
    SessionPolicy policy = SessionPolicy::defaults();
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-e2e-neversuccess" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.summary.verificationVerdict == "FAIL" );
    REQUIRE_FALSE( result.summary.verificationVerdict == "PASS" );
}

TEST_CASE( "re-approved repair proposals keep the plan identity stable", "[agent_loop][e2e]" )
{
    // The no-progress detector keys on the attempt-independent plan
    // identity. When the same repair is proposed again on a later attempt
    // and re-approved, it must NOT be appended twice: a duplicated rule id
    // would change the identity without changing the science, so the same
    // failing science would look like fresh progress forever and only the
    // replan budget would stop the loop.
    FakeScenario scenario;
    scenario.preflight = {
        { "ok", {} }, // attempt 1: clean
        { "fixable", { { "fix-1", "shape_preserving", "rs:reproject" } } }, // attempts 2+
    };
    scenario.execution = {
        { false, "EXEC_FAILED", {} },
        { false, "EXEC_FAILED", {} },
    };
    scenario.diagnosis = { { "BAD_GEOMETRY", { { "fix-1", "shape_preserving", "rs:reproject" } } } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-dedup" );

    const SessionResult result = session.run( makeRequest() );

    // The repeated science repeats its failure key: no-progress fires
    // (the identity stayed h(ndvi|fix-1) across attempts 2 and 3), BEFORE
    // the replan budget is spent. A duplicated approval would keep
    // mutating the identity, the failure keys would never repeat, and the
    // loop would instead run to SESSION_REPLAN_LIMIT.
    REQUIRE( result.stopReason == stop_reasons::kNoProgress );
    REQUIRE( result.summary.budgets[ "replans_used" ].asInt() == 3 );
}
