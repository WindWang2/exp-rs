// tests/test_agent_loop_modes.cpp
//
// RS14-11 Evidence-first Agent Loop — Slice C: the session driver with
// dry-run / plan-only modes (and the execute-with-verify smoke path that
// slices D/E/F extend). Deterministic offline doubles only.

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

/// Builds the standard offline dependency bundle.
ScientificAgentSession::Dependencies makeDependencies( FakeSeams &seams )
{
    return { &seams.dataProvider(), &seams.planner(), &seams.preflight(), &seams.executor(),
             &seams.verifier(), &seams.diagnoser() };
}

/// Builds the standard offline request.
SessionRunRequest makeRequest()
{
    SessionRunRequest request;
    request.goal = "compute NDVI for the scene";
    request.intent = "ndvi";
    return request;
}

} // namespace

TEST_CASE( "dry-run never executes and delivers the would-execute plan", "[agent_loop][modes]" )
{
    FakeScenario scenario; // defaults: preflight ok, execution succeeds, verify PASS
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::DryRun;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-dry" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( result.stopReason.empty() );

    // The executor and verifier were NEVER invoked — a real oracle from the
    // fake call counters, not a vacuous "nothing failed" check.
    REQUIRE( seams.executorFake().beginCount() == 0 );
    REQUIRE( seams.verifierFake().verifyCount() == 0 );
    REQUIRE( seams.diagnoserFake().diagnoseCount() == 0 );
    REQUIRE( seams.plannerFake().planCount() == 1 );
    REQUIRE( seams.preflightFake().checkCount() == 1 );

    // The summary says what WOULD have run.
    REQUIRE( result.summary.mode == "dry_run" );
    REQUIRE( result.summary.wouldExecute );
    REQUIRE( result.summary.verificationVerdict.empty() );
    REQUIRE( result.summary.artifacts.empty() );

    // Journal proves the stage chain stopped before execute.
    const std::vector< std::string > expectedStages = {
        stages::kGoalNormalization, stages::kDataStateSnapshot, stages::kPlanRequest,
        stages::kPreflight,         stages::kDelivery,
    };
    REQUIRE( result.summary.stages == expectedStages );
    REQUIRE( result.summary.replay[ "final_stage" ].asString() == stages::kDelivery );
    REQUIRE( result.summary.replay[ "terminal_state" ].asString() == terminal_states::kDelivered );
}

TEST_CASE( "plan-only stops after a clean preflight", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::PlanOnly;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-plan" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( seams.executorFake().beginCount() == 0 );
    REQUIRE( seams.verifierFake().verifyCount() == 0 );

    const std::vector< std::string > expectedStages = {
        stages::kGoalNormalization, stages::kDataStateSnapshot, stages::kPlanRequest,
        stages::kPreflight,         stages::kDelivery,
    };
    REQUIRE( result.summary.stages == expectedStages );
    REQUIRE_FALSE( result.summary.wouldExecute ); // would_execute is dry-run only
    REQUIRE( result.summary.decisions.size() >= 4 ); // goal, snapshot, plan, preflight, delivery
}

TEST_CASE( "plan-only with a blocked preflight refuses with a typed reason", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    scenario.preflight = { { "blocked", {} } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::PlanOnly;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-blocked" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kPreflightBlocked );
    REQUIRE( result.summary.stopReason == stop_reasons::kPreflightBlocked );
    REQUIRE( seams.executorFake().beginCount() == 0 );
    REQUIRE( seams.verifierFake().verifyCount() == 0 );

    // The refusal itself is a decision with a reason and evidence.
    bool foundRefusal = false;
    for ( const DecisionRecord &decision : result.summary.decisions )
    {
        if ( decision.selected[ "action" ] == "refuse" )
        {
            foundRefusal = true;
            REQUIRE( decision.reason.find( "preflight" ) != std::string::npos );
            REQUIRE( decision.stage == stages::kPreflight );
        }
    }
    REQUIRE( foundRefusal );
}

TEST_CASE( "dry-run reports a fixable preflight without applying repairs", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    scenario.preflight = { { "fixable",
                             [] {
                                 RepairProposal p;
                                 p.ruleId = "align_to_reference";
                                 p.riskClass = "shape_preserving";
                                 p.operatorId = "gdal:reproject";
                                 p.rationale = "align grids";
                                 return std::vector< RepairProposal >{ p };
                             }() } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::DryRun;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-dry-fixable" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( result.summary.wouldExecute );

    // The preflight decision lists the proposal and the approval preview.
    bool foundPreview = false;
    for ( const DecisionRecord &decision : result.summary.decisions )
    {
        if ( decision.stage != stages::kPreflight )
            continue;
        const Json::Value &proposals = decision.inputs[ "proposals" ];
        if ( proposals.isArray() && proposals.size() == 1 )
        {
            foundPreview = true;
            REQUIRE( proposals[ 0 ][ "rule_id" ].asString() == "align_to_reference" );
            REQUIRE( proposals[ 0 ][ "would_auto_approve" ].asBool() );
        }
    }
    REQUIRE( foundPreview );

    // Dry-run never enters the repair-approval stage and never replans.
    for ( const std::string &stage : result.summary.stages )
        REQUIRE( stage != stages::kRepairApproval );
    REQUIRE( result.summary.replay[ "replan_count" ].asInt() == 0 );
    REQUIRE( seams.plannerFake().planCount() == 1 );
    REQUIRE( seams.executorFake().beginCount() == 0 );
}

TEST_CASE( "an empty goal refuses before any seam runs", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-empty-goal" );

    SessionRunRequest request;
    request.goal = "   ";
    const SessionResult result = session.run( request );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kInvalidGoal );
    REQUIRE( seams.dataProviderFake().snapshotCount() == 0 );
    REQUIRE( seams.plannerFake().planCount() == 0 );
    REQUIRE( result.summary.decisions.size() == 1 );
    REQUIRE( result.summary.decisions[ 0 ].reason == "goal is empty after normalization" );
}

TEST_CASE( "an unbounded policy refuses before anything runs", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    policy.maxReplans = -1; // cannot be bounded
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-bad-policy" );

    SessionRunRequest request;
    request.goal = "compute NDVI";
    const SessionResult result = session.run( request );

    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kInvalidPolicy );
    REQUIRE( seams.dataProviderFake().snapshotCount() == 0 );
    REQUIRE( seams.plannerFake().planCount() == 0 );
}

TEST_CASE( "execute-with-verify smoke path reaches delivery", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    scenario.execution = { { true, "", { "out.tif" } } };
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::ExecuteWithVerify;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-exec" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kDelivered );
    REQUIRE( seams.executorFake().beginCount() == 1 );
    REQUIRE( seams.verifierFake().verifyCount() == 1 );
    REQUIRE( result.summary.verificationVerdict == "PASS" );
    REQUIRE( result.summary.artifacts.size() == 1 );

    const std::vector< std::string > expectedStages = {
        stages::kGoalNormalization, stages::kDataStateSnapshot, stages::kPlanRequest,
        stages::kPreflight,         stages::kExecute,           stages::kVerify,
        stages::kDelivery,
    };
    REQUIRE( result.summary.stages == expectedStages );
}

TEST_CASE( "every decision carries a reason and the journal is replayable", "[agent_loop][modes]" )
{
    FakeScenario scenario;
    SessionPolicy policy = SessionPolicy::defaults();
    policy.mode = RunMode::PlanOnly;
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-decisions" );

    const SessionResult result = session.run( makeRequest() );

    int expectedSeq = 1;
    for ( const DecisionRecord &decision : result.summary.decisions )
    {
        REQUIRE( decision.decisionId == "decision-" + std::to_string( expectedSeq++ ) );
        REQUIRE_FALSE( decision.reason.empty() );
        REQUIRE( decision.sessionId == "sess-decisions" );
        REQUIRE( decision.policy[ "policy_id" ].asString() == "session_policy" );
        REQUIRE( decision.policy[ "version" ].asString() == "1.0" );
        REQUIRE( isKnownStage( decision.stage ) );
    }

    // The journal replays to exactly the delivered state.
    const SessionJournal::ReplayResult replay = result.journal.replay();
    REQUIRE( replay.finalStage == stages::kDelivery );
    REQUIRE( replay.terminalState == terminal_states::kDelivered );
    REQUIRE( replay.replanCount == 0 );

    // And the summary's stage list matches the journal's stage_enter order.
    std::vector< std::string > journalStages;
    for ( const JournalEntry &entry : result.journal.entries() )
        if ( entry.event == "stage_enter" )
            journalStages.push_back( entry.stage );
    REQUIRE( journalStages == result.summary.stages );
}

TEST_CASE( "identical sessions serialize byte-identically", "[agent_loop][modes]" )
{
    auto runOnce = []( const std::string &sessionId ) {
        FakeScenario scenario;
        SessionPolicy policy = SessionPolicy::defaults();
        policy.mode = RunMode::PlanOnly;
        FakeSeams seams( scenario );
        ScientificAgentSession session( policy, makeDependencies( seams ), {}, sessionId );
        return session.run( makeRequest() );
    };

    const SessionResult first = runOnce( "sess-det" );
    const SessionResult second = runOnce( "sess-det" );
    REQUIRE( jsonToString( first.summary.toJson() ) == jsonToString( second.summary.toJson() ) );
    REQUIRE( jsonToString( first.journal.toJson() ) == jsonToString( second.journal.toJson() ) );
}

TEST_CASE( "a preflight ok verdict carrying proposals is never executed silently",
           "[agent_loop][modes]" )
{
    // Fail-closed contract: a preflight implementation that reports "ok"
    // while still carrying repair proposals must not sail through to
    // execution. The proposals route through repair approval — and a
    // radiometric (strictest-class) proposal is never auto-approved, so
    // the session refuses with the typed preflight reason instead of
    // running the plan.
    FakeScenario scenario;
    RepairProposal radiometric;
    radiometric.ruleId = "dem-radiometric-normalize";
    radiometric.riskClass = "radiometric";
    radiometric.operatorId = "rs:radiometric_normalize";
    scenario.preflight = { { "ok", { radiometric } } };
    SessionPolicy policy = SessionPolicy::defaults(); // execute_with_verify
    FakeSeams seams( scenario );
    ScientificAgentSession session( policy, makeDependencies( seams ), {}, "sess-ok-proposals" );

    const SessionResult result = session.run( makeRequest() );

    REQUIRE( seams.executorFake().beginCount() == 0 );
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.terminalState == terminal_states::kRefused );
    REQUIRE( result.stopReason == stop_reasons::kPreflightBlocked );
}
