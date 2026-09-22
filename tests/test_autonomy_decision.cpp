// tests/test_autonomy_decision.cpp
//
// RS14-12 teaching autonomy ladder — Slice C: the decision engine.
//
// The engine turns (effective policy, request) into a typed allow / deny /
// downgrade with a machine-readable reason. Properties under test:
//   * the level×capability matrix (assistive capabilities downgrade,
//     execution never silently falls back);
//   * mode ceilings and course caps are the reported binding constraint;
//   * per-capability overrides are explicit and win over the level matrix,
//     but never over the structural lab-student / agent-mode rules;
//   * unknown capabilities deny (fail-closed);
//   * allowed execution always carries verificationRequired.
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_classification.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

#include <string>

using namespace sicnu::agent::autonomy;

namespace {

AutonomyPolicy policyOf( const std::string &level, const std::string &mode )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson(
        std::string( R"({"schema":"sicnu.autonomy-policy/1","level":")" ) + level +
        R"(","mode":")" + mode + R"("})" );
    REQUIRE( parsed.ok );
    return parsed.policy;
}

AutonomyRequest requestOf( const std::string &domain, const std::string &role,
                           const std::string &capability )
{
    AutonomyRequest request;
    request.domain = domain;
    request.role = role;
    request.capability = capability;
    return request;
}

} // namespace

TEST_CASE( "level x capability matrix in the lab domain (teacher)", "[autonomy][decision]" )
{
    struct Case
    {
        AutonomyLevel level;
        const char *capability;
        AutonomyDecisionKind kind;
        const char *reason;
        const char *downgradeTo;
    };
    const Case cases[] = {
        // L0: no assistance at all — assistive requests are denied, not
        // downgraded (a "read-only query" answer would still be help).
        { AutonomyLevel::L0, assistance_capabilities::kConceptHint, AutonomyDecisionKind::Deny,
          autonomy_reason_codes::kLevelTooLow, "" },
        { AutonomyLevel::L0, assistance_capabilities::kErrorLocalization, AutonomyDecisionKind::Deny,
          autonomy_reason_codes::kLevelTooLow, "" },
        // L1: concepts only.
        { AutonomyLevel::L1, assistance_capabilities::kConceptHint, AutonomyDecisionKind::Allow,
          autonomy_reason_codes::kAllowed, "" },
        { AutonomyLevel::L1, assistance_capabilities::kErrorLocalization, AutonomyDecisionKind::Downgrade,
          autonomy_reason_codes::kDowngraded, assistance_capabilities::kConceptHint },
        // L2: error localization.
        { AutonomyLevel::L2, assistance_capabilities::kErrorLocalization, AutonomyDecisionKind::Allow,
          autonomy_reason_codes::kAllowed, "" },
        { AutonomyLevel::L2, assistance_capabilities::kNextStepRecommendation, AutonomyDecisionKind::Downgrade,
          autonomy_reason_codes::kDowngraded, assistance_capabilities::kErrorLocalization },
        // L3: next-step recommendations.
        { AutonomyLevel::L3, assistance_capabilities::kNextStepRecommendation, AutonomyDecisionKind::Allow,
          autonomy_reason_codes::kAllowed, "" },
        { AutonomyLevel::L3, assistance_capabilities::kPlanGeneration, AutonomyDecisionKind::Downgrade,
          autonomy_reason_codes::kDowngraded, assistance_capabilities::kNextStepRecommendation },
        // L4: plan generation; execution still denied (never a silent fallback).
        { AutonomyLevel::L4, assistance_capabilities::kPlanGeneration, AutonomyDecisionKind::Allow,
          autonomy_reason_codes::kAllowed, "" },
        { AutonomyLevel::L4, assistance_capabilities::kAutonomousExecution, AutonomyDecisionKind::Deny,
          autonomy_reason_codes::kLevelTooLow, "" },
        // L5: execution allowed — verification stays mandatory.
        { AutonomyLevel::L5, assistance_capabilities::kAutonomousExecution, AutonomyDecisionKind::Allow,
          autonomy_reason_codes::kAllowed, "" },
    };

    const char *levelNames[] = { "L0", "L1", "L2", "L3", "L4", "L5" };
    for ( const Case &c : cases )
    {
        const AutonomyPolicy policy = policyOf( levelNames[ autonomyLevelOrdinal( c.level ) ], "instructor" );
        const AutonomyDecision decision =
            decideAutonomy( policy, requestOf( "lab", "teacher", c.capability ) );
        INFO( levelNames[ autonomyLevelOrdinal( c.level ) ] << " / " << c.capability );
        REQUIRE( decision.kind == c.kind );
        REQUIRE( decision.reasonCode == c.reason );
        if ( c.kind == AutonomyDecisionKind::Downgrade )
            REQUIRE( decision.downgradeTo == c.downgradeTo );
        if ( c.kind == AutonomyDecisionKind::Allow &&
             std::string( c.capability ) == assistance_capabilities::kAutonomousExecution )
            REQUIRE( decision.verificationRequired );
        else
            REQUIRE_FALSE( decision.verificationRequired );
    }
}

TEST_CASE( "read-only queries are allowed at every level including L0", "[autonomy][decision]" )
{
    const char *levelNames[] = { "L0", "L1", "L2", "L3", "L4", "L5" };
    for ( const char *level : levelNames )
    {
        const AutonomyDecision decision = decideAutonomy(
            policyOf( level, "instructor" ),
            requestOf( "lab", "student", assistance_capabilities::kReadOnlyQuery ) );
        INFO( level );
        REQUIRE( decision.kind == AutonomyDecisionKind::Allow );
        REQUIRE( decision.reasonCode == autonomy_reason_codes::kAllowed );
    }
}

TEST_CASE( "mode ceiling is the reported binding constraint", "[autonomy][decision]" )
{
    const AutonomyPolicy policy = policyOf( "L5", "exam" );
    // Assistive requests above the ceiling downgrade to the unlocked level.
    const AutonomyDecision downgraded = decideAutonomy(
        policy, requestOf( "lab", "teacher", assistance_capabilities::kNextStepRecommendation ) );
    REQUIRE( downgraded.kind == AutonomyDecisionKind::Downgrade );
    REQUIRE( downgraded.downgradeTo == assistance_capabilities::kErrorLocalization );

    const AutonomyDecision allowed = decideAutonomy(
        policy, requestOf( "lab", "teacher", assistance_capabilities::kErrorLocalization ) );
    REQUIRE( allowed.kind == AutonomyDecisionKind::Allow );

    // Execution above the ceiling is denied and names the ceiling.
    const AutonomyDecision stillDenied = decideAutonomy(
        policy, requestOf( "lab", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( stillDenied.kind == AutonomyDecisionKind::Deny );
    REQUIRE( stillDenied.reasonCode == autonomy_reason_codes::kModeCeiling );
}

TEST_CASE( "course cap is the reported binding constraint", "[autonomy][decision]" )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"instructor","max_level":"L1"})" );
    REQUIRE( parsed.ok );
    const AutonomyDecision decision = decideAutonomy(
        parsed.policy, requestOf( "lab", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
    REQUIRE( decision.reasonCode == autonomy_reason_codes::kCourseCap );

    // Assistive requests above the cap downgrade to the unlocked level.
    const AutonomyDecision downgraded = decideAutonomy(
        parsed.policy, requestOf( "lab", "teacher", assistance_capabilities::kErrorLocalization ) );
    REQUIRE( downgraded.kind == AutonomyDecisionKind::Downgrade );
    REQUIRE( downgraded.downgradeTo == assistance_capabilities::kConceptHint );
}

TEST_CASE( "explicit overrides win over the level matrix", "[autonomy][decision]" )
{
    const AutonomyPolicyParseResult allowed = parseAutonomyPolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L0","mode":"agent",
            "capability_overrides":{"autonomous_execution":{"decision":"allow"}}})" );
    REQUIRE( allowed.ok );
    const AutonomyDecision allow = decideAutonomy(
        allowed.policy, requestOf( "agent", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( allow.kind == AutonomyDecisionKind::Allow );
    REQUIRE( allow.verificationRequired );

    const AutonomyPolicyParseResult denied = parseAutonomyPolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L4","mode":"instructor",
            "capability_overrides":{"plan_generation":{"decision":"deny","reason_code":"AUTONOMY_OVERRIDE_DENIED"}}})" );
    REQUIRE( denied.ok );
    const AutonomyDecision deny = decideAutonomy(
        denied.policy, requestOf( "lab", "teacher", assistance_capabilities::kPlanGeneration ) );
    REQUIRE( deny.kind == AutonomyDecisionKind::Deny );
    REQUIRE( deny.reasonCode == autonomy_reason_codes::kOverrideDenied );
}

TEST_CASE( "lab students never receive autonomous execution — structurally", "[autonomy][decision]" )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"instructor",
            "capability_overrides":{"autonomous_execution":{"decision":"allow"}}})" );
    REQUIRE( parsed.ok );
    const AutonomyDecision decision = decideAutonomy(
        parsed.policy, requestOf( "lab", "student", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
    REQUIRE( decision.reasonCode == autonomy_reason_codes::kLabStudentExecution );

    // A forged/unknown role is a student, not an escalation.
    const AutonomyDecision forged = decideAutonomy(
        parsed.policy, requestOf( "lab", "teacher ", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( forged.kind == AutonomyDecisionKind::Deny );
    REQUIRE( forged.reasonCode == autonomy_reason_codes::kLabStudentExecution );
}

TEST_CASE( "agent-domain execution requires the explicit agent mode", "[autonomy][decision]" )
{
    const AutonomyPolicy research = policyOf( "L5", "agent" );
    const AutonomyDecision allowed = decideAutonomy(
        research, requestOf( "agent", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( allowed.kind == AutonomyDecisionKind::Allow );
    REQUIRE( allowed.verificationRequired );

    const AutonomyPolicy notOptedIn = policyOf( "L5", "instructor" );
    const AutonomyDecision denied = decideAutonomy(
        notOptedIn, requestOf( "agent", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( denied.kind == AutonomyDecisionKind::Deny );
    REQUIRE( denied.reasonCode == autonomy_reason_codes::kAgentModeRequired );
}

TEST_CASE( "unknown capabilities deny (fail-closed)", "[autonomy][decision]" )
{
    const AutonomyDecision decision = decideAutonomy(
        policyOf( "L5", "agent" ), requestOf( "agent", "teacher", "do_everything" ) );
    REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
    REQUIRE( decision.reasonCode == autonomy_reason_codes::kUnknownCapability );
}

TEST_CASE( "an empty policy fails closed", "[autonomy][decision]" )
{
    const AutonomyPolicy empty;
    const AutonomyDecision assistive = decideAutonomy(
        empty, requestOf( "lab", "teacher", assistance_capabilities::kConceptHint ) );
    REQUIRE( assistive.kind == AutonomyDecisionKind::Deny );
    REQUIRE( assistive.reasonCode == autonomy_reason_codes::kLevelTooLow );

    const AutonomyDecision execution = decideAutonomy(
        empty, requestOf( "agent", "teacher", assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( execution.kind == AutonomyDecisionKind::Deny );
    REQUIRE( execution.reasonCode == autonomy_reason_codes::kAgentModeRequired );
}

TEST_CASE( "decision serializes deterministically", "[autonomy][decision]" )
{
    const AutonomyDecision decision = decideAutonomy(
        policyOf( "L1", "practice" ),
        requestOf( "lab", "student", assistance_capabilities::kNextStepRecommendation ) );
    REQUIRE( decision.kind == AutonomyDecisionKind::Downgrade );
    const Json::Value first = decision.toJson();
    const Json::Value second = decision.toJson();
    REQUIRE( first.toStyledString() == second.toStyledString() );
    REQUIRE( first[ "decision" ].asString() == "downgrade" );
    REQUIRE( first[ "reason_code" ].asString() == autonomy_reason_codes::kDowngraded );
    REQUIRE( first[ "downgrade_to" ].asString() == assistance_capabilities::kConceptHint );
}
