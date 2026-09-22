// tests/test_autonomy_projection.cpp
//
// RS14-12 teaching autonomy ladder — Slice E: the status projection.
//
// sicnu.autonomy-status/1 is what the UI shows and what a future agent
// reads: the effective level, the mode, and for every capability whether it
// is allowed, limited (downgraded), or forbidden — each with a typed reason
// and a Chinese explanation. Properties under test:
//   * the projection is a pure function of (policy, role, domain) — same
//     input, byte-identical output;
//   * bounded — at most one entry per closed capability;
//   * honest — every limited/forbidden entry carries a reason code and a
//     non-empty Chinese explanation;
//   * role is fail-closed (unknown role ⇒ student in the lab domain).
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"

#include <string>

using namespace sicnu::agent::autonomy;

namespace {

AutonomyPolicy policyOf( const std::string &json )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson( json );
    REQUIRE( parsed.ok );
    return parsed.policy;
}

bool hasCapability( const Json::Value &list, const std::string &capability )
{
    for ( const Json::Value &entry : list )
        if ( entry.isString() && entry.asString() == capability )
            return true;
    return false;
}

const Json::Value *findEntry( const Json::Value &list, const std::string &capability )
{
    for ( const Json::Value &entry : list )
        if ( entry.isObject() && entry[ "capability" ].asString() == capability )
            return &entry;
    return nullptr;
}

} // namespace

TEST_CASE( "projection reports level, mode and per-capability state", "[autonomy][projection]" )
{
    const Json::Value status = autonomyStatusProjection(
        policyOf( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"exam"})" ),
        "student", "lab" );

    REQUIRE( status[ "schema" ].asString() == kAutonomyStatusSchema );
    REQUIRE( status[ "level" ].asString() == "L2" ); // exam ceiling
    REQUIRE( status[ "mode" ].asString() == "exam" );
    REQUIRE( status[ "role" ].asString() == "student" );
    REQUIRE( status[ "domain" ].asString() == "lab" );

    REQUIRE( hasCapability( status[ "allowed" ], assistance_capabilities::kReadOnlyQuery ) );
    REQUIRE( hasCapability( status[ "allowed" ], assistance_capabilities::kConceptHint ) );
    REQUIRE( hasCapability( status[ "allowed" ], assistance_capabilities::kErrorLocalization ) );

    const Json::Value *limited = findEntry( status[ "limited" ], assistance_capabilities::kNextStepRecommendation );
    REQUIRE( limited != nullptr );
    REQUIRE( ( *limited )[ "downgrade_to" ].asString() == assistance_capabilities::kErrorLocalization );
    REQUIRE( ( *limited )[ "reason_code" ].asString() == autonomy_reason_codes::kDowngraded );
    REQUIRE_FALSE( ( *limited )[ "reason_zh" ].asString().empty() );

    const Json::Value *forbidden =
        findEntry( status[ "forbidden" ], assistance_capabilities::kAutonomousExecution );
    REQUIRE( forbidden != nullptr );
    REQUIRE( ( *forbidden )[ "reason_code" ].asString() == autonomy_reason_codes::kLabStudentExecution );
    REQUIRE_FALSE( ( *forbidden )[ "reason_zh" ].asString().empty() );
}

TEST_CASE( "projection is deterministic and bounded", "[autonomy][projection]" )
{
    const AutonomyPolicy policy =
        policyOf( R"({"schema":"sicnu.autonomy-policy/1","level":"L3","mode":"practice"})" );
    const Json::Value first = autonomyStatusProjection( policy, "student", "lab" );
    const Json::Value second = autonomyStatusProjection( policy, "student", "lab" );
    REQUIRE( first.toStyledString() == second.toStyledString() );

    int total = 0;
    for ( const char *key : { "allowed", "limited", "forbidden" } )
        total += static_cast<int>( first[ key ].size() );
    REQUIRE( total <= 6 );
}

TEST_CASE( "an instructor agent session may execute with verification", "[autonomy][projection]" )
{
    const Json::Value status = autonomyStatusProjection(
        policyOf( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"agent"})" ),
        "teacher", "agent" );
    REQUIRE( status[ "level" ].asString() == "L5" );
    REQUIRE( hasCapability( status[ "allowed" ], assistance_capabilities::kAutonomousExecution ) );
    REQUIRE( status[ "forbidden" ].empty() );
    REQUIRE( status[ "verification_required" ].asBool() );
}

TEST_CASE( "an empty policy projects as fully closed", "[autonomy][projection]" )
{
    const Json::Value status = autonomyStatusProjection( AutonomyPolicy{}, "student", "lab" );
    REQUIRE( status[ "level" ].asString() == "L0" );
    REQUIRE( hasCapability( status[ "allowed" ], assistance_capabilities::kReadOnlyQuery ) );
    const Json::Value *forbidden =
        findEntry( status[ "forbidden" ], assistance_capabilities::kConceptHint );
    REQUIRE( forbidden != nullptr );
    REQUIRE( ( *forbidden )[ "reason_code" ].asString() == autonomy_reason_codes::kLevelTooLow );
}

TEST_CASE( "unknown roles are students in the lab domain", "[autonomy][projection]" )
{
    const Json::Value status = autonomyStatusProjection(
        policyOf( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"instructor"})" ),
        "", "lab" );
    const Json::Value *forbidden =
        findEntry( status[ "forbidden" ], assistance_capabilities::kAutonomousExecution );
    REQUIRE( forbidden != nullptr );
    REQUIRE( ( *forbidden )[ "reason_code" ].asString() == autonomy_reason_codes::kLabStudentExecution );
}

TEST_CASE( "every limited and forbidden entry carries a Chinese explanation", "[autonomy][projection]" )
{
    const Json::Value status = autonomyStatusProjection(
        policyOf( R"({"schema":"sicnu.autonomy-policy/1","level":"L1","mode":"practice"})" ),
        "student", "lab" );
    for ( const char *key : { "limited", "forbidden" } )
        for ( const Json::Value &entry : status[ key ] )
        {
            INFO( key << " / " << entry[ "capability" ].asString() );
            REQUIRE_FALSE( entry[ "reason_zh" ].asString().empty() );
            REQUIRE( isKnownAutonomyReasonCode( entry[ "reason_code" ].asString() ) );
        }
}

TEST_CASE( "effective level clamps by mode ceiling and course cap", "[autonomy][projection]" )
{
    REQUIRE( effectiveAutonomyLevel( policyOf(
                 R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"exam"})" ) ) ==
             AutonomyLevel::L2 );
    REQUIRE( effectiveAutonomyLevel( policyOf(
                 R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"practice"})" ) ) ==
             AutonomyLevel::L4 );
    REQUIRE( effectiveAutonomyLevel( policyOf(
                 R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"instructor","max_level":"L1"})" ) ) ==
             AutonomyLevel::L1 );
    REQUIRE( effectiveAutonomyLevel( AutonomyPolicy{} ) == AutonomyLevel::L0 );
}

TEST_CASE( "reason explanations exist for every closed reason code", "[autonomy][projection]" )
{
    static const char *const kCodes[] = {
        autonomy_reason_codes::kAllowed,
        autonomy_reason_codes::kUnknownCapability,
        autonomy_reason_codes::kLevelTooLow,
        autonomy_reason_codes::kDowngraded,
        autonomy_reason_codes::kModeCeiling,
        autonomy_reason_codes::kCourseCap,
        autonomy_reason_codes::kOverrideDenied,
        autonomy_reason_codes::kLabStudentExecution,
        autonomy_reason_codes::kAgentModeRequired,
    };
    for ( const char *code : kCodes )
    {
        INFO( code );
        REQUIRE_FALSE( autonomyReasonZh( code ).empty() );
    }
    REQUIRE( autonomyReasonZh( "NOT_A_CODE" ).empty() );
}
