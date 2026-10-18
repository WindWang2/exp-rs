// tests/test_agent_loop_seams.cpp
//
// RS14-11 Evidence-first Agent Loop — Slice B: seam interfaces, the
// deterministic offline doubles, and the session policy/budgets. Pure C++.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>
#include <json/writer.h>

#include "agent_loop/session_seams.h"
#include "agent_loop/session_policy.h"
#include "agent_loop/fake_seams.h"

#include <string>

using namespace sicnu::agent_loop;

namespace {

std::string jsonToString( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "commentStyle" ] = "None";
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

RepairProposal makeProposal( const std::string &ruleId, const std::string &riskClass )
{
    RepairProposal p;
    p.ruleId = ruleId;
    p.riskClass = riskClass;
    p.rationale = "test proposal";
    return p;
}

} // namespace

TEST_CASE( "run mode parses and serializes", "[agent_loop][seams]" )
{
    RunMode mode = RunMode::PlanOnly;
    REQUIRE( parseRunMode( "dry_run", mode ) );
    REQUIRE( mode == RunMode::DryRun );
    REQUIRE( parseRunMode( "plan_only", mode ) );
    REQUIRE( mode == RunMode::PlanOnly );
    REQUIRE( parseRunMode( "execute_with_verify", mode ) );
    REQUIRE( mode == RunMode::ExecuteWithVerify );
    REQUIRE( runModeToString( RunMode::DryRun ) == "dry_run" );
    REQUIRE( runModeToString( RunMode::PlanOnly ) == "plan_only" );
    REQUIRE( runModeToString( RunMode::ExecuteWithVerify ) == "execute_with_verify" );

    REQUIRE_FALSE( parseRunMode( "execute", mode ) );
    REQUIRE_FALSE( parseRunMode( "", mode ) );
    REQUIRE_FALSE( parseRunMode( "DRY_RUN", mode ) );
    REQUIRE( mode == RunMode::ExecuteWithVerify ); // untouched on failure
}

TEST_CASE( "session policy validates its bounds", "[agent_loop][seams]" )
{
    const SessionPolicy ok = SessionPolicy::defaults();
    std::string error;
    REQUIRE( ok.validate( &error ) );
    REQUIRE( error.empty() );

    SessionPolicy bad = ok;
    bad.maxReplans = -1;
    REQUIRE_FALSE( bad.validate( &error ) );
    REQUIRE( error.find( "max_replans" ) != std::string::npos );

    bad = ok;
    bad.noProgressThreshold = 0;
    REQUIRE_FALSE( bad.validate( &error ) );
    REQUIRE( error.find( "no_progress_threshold" ) != std::string::npos );

    bad = ok;
    bad.maxSteps = 0;
    REQUIRE_FALSE( bad.validate( &error ) );
    REQUIRE( error.find( "max_steps" ) != std::string::npos );

    bad = ok;
    bad.executorTimeoutMs = -5;
    REQUIRE_FALSE( bad.validate( &error ) );
    REQUIRE( error.find( "executor_timeout_ms" ) != std::string::npos );

    // Boundary values that must stay legal.
    SessionPolicy edges = ok;
    edges.maxReplans = 0;
    edges.noProgressThreshold = 1;
    edges.maxSteps = 1;
    edges.executorTimeoutMs = 0;
    REQUIRE( edges.validate( &error ) );

    // Policy identity carried into decision records.
    REQUIRE( ok.policyId() == "session_policy" );
    REQUIRE( ok.policyVersion() == "1.0" );
}

TEST_CASE( "repair approval policy is fail-closed", "[agent_loop][seams]" )
{
    const RepairApprovalPolicy policy; // defaults: shape_preserving auto only
    REQUIRE( policy.classAutoApproved( "shape_preserving" ) );
    REQUIRE_FALSE( policy.classAutoApproved( "radiometric" ) );
    REQUIRE_FALSE( policy.classAutoApproved( "science_changing" ) );
    REQUIRE_FALSE( policy.classAutoApproved( "" ) );
    REQUIRE_FALSE( policy.classAutoApproved( "shape_preserving " ) ); // no fuzzy match

    RepairApprovalPolicy open;
    open.autoApproveRiskClasses = { "shape_preserving", "radiometric" };
    REQUIRE( open.classAutoApproved( "radiometric" ) );
    REQUIRE_FALSE( open.classAutoApproved( "science_changing" ) );

    RepairApprovalPolicy none;
    none.autoApproveRiskClasses = {};
    REQUIRE_FALSE( none.classAutoApproved( "shape_preserving" ) );

    REQUIRE_FALSE( SessionPolicy::defaults().repairApproval.allowUnapprovedFixable );
}

TEST_CASE( "data state snapshot round-trips and fails closed", "[agent_loop][seams]" )
{
    DataStateSnapshot snap;
    snap.schemaVersion = "1.0";
    AssetFact primary;
    primary.slot = "primary";
    primary.ref = "/data/scene.tif";
    primary.kind = "raster";
    primary.resolved = true;
    primary.facts[ "bands" ] = 12;
    snap.assets.push_back( primary );
    AssetFact missing;
    missing.slot = "secondary";
    missing.ref = "scene-b";
    missing.resolutionError = "DATASET_NOT_FOUND";
    snap.assets.push_back( missing );
    snap.summary[ "resolved" ] = 1;

    const Json::Value doc = snap.toJson();
    std::string error;
    const auto back = DataStateSnapshot::fromJson( doc, &error );
    REQUIRE( back.has_value() );
    REQUIRE( back->assets.size() == 2 );
    REQUIRE( back->assets[ 0 ].slot == "primary" );
    REQUIRE( back->assets[ 0 ].facts[ "bands" ].asInt() == 12 );
    REQUIRE_FALSE( back->assets[ 1 ].resolved );
    REQUIRE( back->assets[ 1 ].resolutionError == "DATASET_NOT_FOUND" );

    Json::Value badVersion = doc;
    badVersion[ "schema_version" ] = "2.0";
    REQUIRE_FALSE( DataStateSnapshot::fromJson( badVersion, &error ).has_value() );
    REQUIRE( error.find( "schema_version" ) != std::string::npos );

    Json::Value noAssets = doc;
    noAssets.removeMember( "assets" );
    REQUIRE_FALSE( DataStateSnapshot::fromJson( noAssets, &error ).has_value() );
}

TEST_CASE( "fake planner is deterministic and attempt-sensitive", "[agent_loop][seams]" )
{
    const FakeScenario scenario; // defaults
    FakeSeams first( scenario );
    FakeSeams second( scenario );

    DataStateSnapshot snap;
    AssetFact fact;
    fact.slot = "primary";
    fact.ref = "/data/scene.tif";
    fact.kind = "raster";
    fact.resolved = true;
    snap.assets.push_back( fact );

    PlanRequest request;
    request.goal = "compute NDVI";
    request.intent = "ndvi";
    request.snapshot = snap;
    request.attempt = 1;

    const PlanDraft a = first.planner().plan( request );
    const PlanDraft b = second.planner().plan( request );
    REQUIRE( a.valid );
    REQUIRE( a.fingerprint == b.fingerprint );
    REQUIRE( jsonToString( a.toJson() ) == jsonToString( b.toJson() ) );
    REQUIRE( a.planId == b.planId );

    // A later attempt is a DIFFERENT plan (fingerprint must move, else the
    // no-progress detector would see a repeat).
    request.attempt = 2;
    const PlanDraft c = first.planner().plan( request );
    REQUIRE( c.valid );
    REQUIRE( c.fingerprint != a.fingerprint );

    // Approved repairs participate in the identity.
    PlanRequest repaired = request;
    repaired.attempt = 2;
    repaired.approvedRepairs = { "align_to_reference" };
    const PlanDraft d = first.planner().plan( repaired );
    REQUIRE( d.fingerprint != c.fingerprint );
}

TEST_CASE( "fake preflight and verifier follow their scripts", "[agent_loop][seams]" )
{
    FakeScenario scenario;
    scenario.preflight = {
        { "ok", {} },
        { "fixable", { makeProposal( "align_to_reference", "shape_preserving" ) } },
        { "blocked", {} },
    };
    FakeSeams seams( scenario );
    DataStateSnapshot snap;
    PlanDraft plan = seams.planner().plan( PlanRequest{ "goal", "ndvi", snap, {}, Json::Value(), 1 } );
    plan.attempt = 1;

    const PreflightReport first = seams.preflight().check( plan, snap );
    REQUIRE( first.verdict == "ok" );
    REQUIRE( first.proposals.empty() );

    plan.attempt = 2;
    const PreflightReport second = seams.preflight().check( plan, snap );
    REQUIRE( second.verdict == "fixable" );
    REQUIRE( second.proposals.size() == 1 );
    REQUIRE( second.proposals[ 0 ].riskClass == "shape_preserving" );

    plan.attempt = 3;
    const PreflightReport third = seams.preflight().check( plan, snap );
    REQUIRE( third.verdict == "blocked" );

    // Verifier: aggregate tri-state over per-artifact scripts.
    scenario.verification = { { "PASS", "" }, { "PASS", "warn-check" }, { "FAIL", "nodata" } };
    FakeSeams verifySeams( scenario );
    ExecutionOutcome outcome;
    outcome.finished = true;
    outcome.succeeded = true;
    outcome.artifacts = { "a.tif", "b.tif", "c.tif" };
    const VerificationReport report =
        verifySeams.verifier().verify( plan, outcome );
    REQUIRE( report.artifacts.size() == 3 );
    REQUIRE( report.verdict() == "FAIL" );

    scenario.verification = { { "PASS", "" }, { "PASS", "warn-check" } };
    FakeSeams warnSeams( scenario );
    ExecutionOutcome warnOutcome;
    warnOutcome.finished = true;
    warnOutcome.succeeded = true;
    warnOutcome.artifacts = { "a.tif", "b.tif" };
    REQUIRE( warnSeams.verifier().verify( plan, warnOutcome ).verdict() == "PASS_WITH_WARNINGS" );
}

TEST_CASE( "fake executor runs, fails and cancels on script", "[agent_loop][seams]" )
{
    FakeScenario scenario;
    scenario.execution = {
        { true, "", { "out.tif" } },
        { false, "EXECUTION_FAILED", {} },
    };
    FakeSeams seams( scenario );
    DataStateSnapshot snap;
    PlanDraft plan = seams.planner().plan( PlanRequest{ "goal", "ndvi", snap, {}, Json::Value(), 1 } );

    plan.attempt = 1;
    const ExecutionStart started = seams.executor().begin( plan );
    REQUIRE( started.started );
    REQUIRE_FALSE( started.runId.empty() );
    const ExecutionOutcome done = seams.executor().poll( started, 1000 );
    REQUIRE( done.finished );
    REQUIRE( done.succeeded );
    REQUIRE( done.artifacts.size() == 1 );

    plan.attempt = 2;
    const ExecutionStart second = seams.executor().begin( plan );
    const ExecutionOutcome failed = seams.executor().poll( second, 1000 );
    REQUIRE( failed.finished );
    REQUIRE_FALSE( failed.succeeded );
    REQUIRE( failed.errorCode == "EXECUTION_FAILED" );

    // Cancel is honored: a cancelled run never reports success.
    plan.attempt = 1;
    const ExecutionStart third = seams.executor().begin( plan );
    seams.executor().cancel( third );
    const ExecutionOutcome cancelled = seams.executor().poll( third, 1000 );
    REQUIRE( cancelled.finished );
    REQUIRE_FALSE( cancelled.succeeded );
    REQUIRE( cancelled.errorCode == "CANCELLED" );
}

TEST_CASE( "fake diagnoser reports scripted root causes", "[agent_loop][seams]" )
{
    FakeScenario scenario;
    scenario.diagnosis = { { "GRID_MISMATCH",
                             { makeProposal( "align_to_reference", "shape_preserving" ) } } };
    FakeSeams seams( scenario );
    DataStateSnapshot snap;
    const PlanDraft plan =
        seams.planner().plan( PlanRequest{ "goal", "ndvi", snap, {}, Json::Value(), 1 } );
    ExecutionOutcome outcome;
    outcome.finished = true;
    outcome.succeeded = false;
    outcome.errorCode = "EXECUTION_FAILED";
    VerificationReport verification;
    verification.verdictValue = "FAIL";

    const Diagnosis diagnosis = seams.diagnoser().diagnose( plan, outcome, verification );
    REQUIRE( diagnosis.rootCauseCode == "GRID_MISMATCH" );
    REQUIRE( diagnosis.proposals.size() == 1 );
    REQUIRE( diagnosis.proposals[ 0 ].ruleId == "align_to_reference" );
}

TEST_CASE( "fake data provider resolves its configured slots", "[agent_loop][seams]" )
{
    FakeScenario scenario;
    scenario.slots = { { "primary", "/data/scene.tif", "raster", true },
                       { "secondary", "", "", false } };
    FakeSeams seams( scenario );
    const DataStateSnapshot snap = seams.dataProvider().snapshot( "goal", Json::Value() );
    REQUIRE( snap.assets.size() == 2 );
    REQUIRE( snap.assets[ 0 ].resolved );
    REQUIRE( snap.assets[ 0 ].kind == "raster" );
    REQUIRE_FALSE( snap.assets[ 1 ].resolved );
    REQUIRE( snap.assets[ 1 ].resolutionError == "DATASET_NOT_FOUND" );

    // Deterministic across instances.
    FakeSeams other( scenario );
    const DataStateSnapshot again = other.dataProvider().snapshot( "goal", Json::Value() );
    REQUIRE( jsonToString( again.toJson() ) == jsonToString( snap.toJson() ) );
}
