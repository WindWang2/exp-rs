// tests/test_autonomy_policy.cpp
//
// RS14-12 teaching-autonomy ladder — Slice A: policy schema + level semantics.
//
// Pure value-object suite: no Qt, no QGIS, no network. It pins
//   * the L0..L5 autonomy ladder (parse/serialize/ordering),
//   * the closed capability taxonomy and its minimum levels,
//   * the versioned sicnu.autonomy-policy/1 schema — strict parse, typed
//     errors, deterministic round-trip.
// Every rejection is a typed error string; nothing is silently accepted.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

#include <json/json.h>

#include <string>
#include <vector>

using namespace sicnu::agent::autonomy;

namespace {

Json::Value parseOrThrow( const std::string &text )
{
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) );
    return doc;
}

} // namespace

TEST_CASE( "autonomy level parse and serialize round-trips", "[autonomy][policy]" )
{
    const AutonomyLevel levels[] = { AutonomyLevel::L0, AutonomyLevel::L1, AutonomyLevel::L2,
                                     AutonomyLevel::L3, AutonomyLevel::L4, AutonomyLevel::L5 };
    for ( AutonomyLevel level : levels )
    {
        const std::string text = autonomyLevelToString( level );
        AutonomyLevel parsed = AutonomyLevel::L0;
        REQUIRE( autonomyLevelFromString( text, parsed ) );
        REQUIRE( parsed == level );
        REQUIRE( isKnownAutonomyLevelString( text ) );
    }
}

TEST_CASE( "autonomy level rejects unknown spellings", "[autonomy][policy]" )
{
    AutonomyLevel out = AutonomyLevel::L5;
    for ( const std::string bad : { "", "l3", "L6", "3", "L", "level3", "L3 " } )
    {
        REQUIRE_FALSE( autonomyLevelFromString( bad, out ) );
        REQUIRE_FALSE( isKnownAutonomyLevelString( bad ) );
    }
}

TEST_CASE( "autonomy level ordering is total over the ladder", "[autonomy][policy]" )
{
    REQUIRE( AutonomyLevel::L0 < AutonomyLevel::L1 );
    REQUIRE( AutonomyLevel::L1 < AutonomyLevel::L2 );
    REQUIRE( AutonomyLevel::L2 < AutonomyLevel::L3 );
    REQUIRE( AutonomyLevel::L3 < AutonomyLevel::L4 );
    REQUIRE( AutonomyLevel::L4 < AutonomyLevel::L5 );
    REQUIRE( autonomyLevelOrdinal( AutonomyLevel::L5 ) == 5 );
    REQUIRE( autonomyLevelFromOrdinal( 0 ) == AutonomyLevel::L0 );
    REQUIRE( autonomyLevelFromOrdinal( 5 ) == AutonomyLevel::L5 );
}

TEST_CASE( "closed capability taxonomy maps to minimum levels", "[autonomy][policy]" )
{
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kReadOnlyQuery ) );
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kConceptHint ) );
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kErrorLocalization ) );
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kNextStepRecommendation ) );
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kPlanGeneration ) );
    REQUIRE( isKnownAssistanceCapability( assistance_capabilities::kAutonomousExecution ) );

    REQUIRE( minimumLevelForCapability( assistance_capabilities::kReadOnlyQuery ) == AutonomyLevel::L0 );
    REQUIRE( minimumLevelForCapability( assistance_capabilities::kConceptHint ) == AutonomyLevel::L1 );
    REQUIRE( minimumLevelForCapability( assistance_capabilities::kErrorLocalization ) == AutonomyLevel::L2 );
    REQUIRE( minimumLevelForCapability( assistance_capabilities::kNextStepRecommendation ) == AutonomyLevel::L3 );
    REQUIRE( minimumLevelForCapability( assistance_capabilities::kPlanGeneration ) == AutonomyLevel::L4 );
    REQUIRE( minimumLevelForCapability( assistance_capabilities::kAutonomousExecution ) == AutonomyLevel::L5 );
}

TEST_CASE( "unknown capability has no minimum level (fail-closed)", "[autonomy][policy]" )
{
    REQUIRE_FALSE( isKnownAssistanceCapability( "" ) );
    REQUIRE_FALSE( isKnownAssistanceCapability( "do_everything" ) );
    REQUIRE_FALSE( isKnownAssistanceCapability( "concept_hints" ) );
}

TEST_CASE( "valid policy parses with all declared fields", "[autonomy][policy]" )
{
    const Json::Value doc = parseOrThrow( R"({
      "schema": "sicnu.autonomy-policy/1",
      "level": "L3",
      "mode": "practice",
      "max_level": "L4",
      "capability_overrides": {
        "next_step_recommendation": { "decision": "deny", "reason_code": "AUTONOMY_OVERRIDE_DENIED" }
      },
      "source": "course"
    })" );

    const AutonomyPolicyParseResult result = parseAutonomyPolicy( doc );
    REQUIRE( result.ok );
    REQUIRE( result.errors.empty() );
    REQUIRE( result.policy.hasLevel );
    REQUIRE( result.policy.level == AutonomyLevel::L3 );
    REQUIRE( result.policy.mode == autonomy_modes::kPractice );
    REQUIRE( result.policy.hasMaxLevel );
    REQUIRE( result.policy.maxLevel == AutonomyLevel::L4 );
    REQUIRE( result.policy.source == policy_sources::kCourse );
    REQUIRE( result.policy.overrides.size() == 1 );
    REQUIRE( result.policy.overrides[0].first == assistance_capabilities::kNextStepRecommendation );
    REQUIRE( result.policy.overrides[0].second.decision == "deny" );
}

TEST_CASE( "policy with no declared fields parses as inert", "[autonomy][policy]" )
{
    const AutonomyPolicyParseResult result = parseAutonomyPolicy( parseOrThrow( R"({"schema":"sicnu.autonomy-policy/1"})" ) );
    REQUIRE( result.ok );
    REQUIRE_FALSE( result.policy.hasLevel );
    REQUIRE( result.policy.mode.empty() );
    REQUIRE_FALSE( result.policy.hasMaxLevel );
    REQUIRE( result.policy.overrides.empty() );
}

TEST_CASE( "policy rejects unsupported schema versions", "[autonomy][policy]" )
{
    const AutonomyPolicyParseResult missing = parseAutonomyPolicy( parseOrThrow( R"({"level":"L1"})" ) );
    REQUIRE_FALSE( missing.ok );
    REQUIRE_FALSE( missing.errors.empty() );

    const AutonomyPolicyParseResult future = parseAutonomyPolicy( parseOrThrow( R"({"schema":"sicnu.autonomy-policy/2","level":"L1"})" ) );
    REQUIRE_FALSE( future.ok );
    REQUIRE_FALSE( future.errors.empty() );
}

TEST_CASE( "policy rejects unknown level, mode, source, capability and decision", "[autonomy][policy]" )
{
    struct Case { const char *name; const char *json; };
    const Case cases[] = {
        { "level", R"({"schema":"sicnu.autonomy-policy/1","level":"L6"})" },
        { "level-spelling", R"({"schema":"sicnu.autonomy-policy/1","level":"l3"})" },
        { "mode", R"({"schema":"sicnu.autonomy-policy/1","mode":"sandbox"})" },
        { "max_level", R"({"schema":"sicnu.autonomy-policy/1","max_level":"L9"})" },
        { "source", R"({"schema":"sicnu.autonomy-policy/1","source":"marketing"})" },
        { "override-capability", R"({"schema":"sicnu.autonomy-policy/1","capability_overrides":{"do_everything":{"decision":"deny"}}})" },
        { "override-decision", R"({"schema":"sicnu.autonomy-policy/1","capability_overrides":{"concept_hint":{"decision":"maybe"}}})" },
        { "override-not-object", R"({"schema":"sicnu.autonomy-policy/1","capability_overrides":{"concept_hint":"deny"}})" },
        { "unknown-field", R"({"schema":"sicnu.autonomy-policy/1","levl":"L2"})" },
        { "not-an-object", R"(["sicnu.autonomy-policy/1"])" },
    };
    for ( const Case &c : cases )
    {
        const AutonomyPolicyParseResult result = parseAutonomyPolicy( parseOrThrow( c.json ) );
        INFO( c.name );
        REQUIRE_FALSE( result.ok );
        REQUIRE_FALSE( result.errors.empty() );
    }
}

TEST_CASE( "policy serialization round-trips deterministically", "[autonomy][policy]" )
{
    const Json::Value doc = parseOrThrow( R"({
      "schema": "sicnu.autonomy-policy/1",
      "level": "L2",
      "mode": "exam",
      "capability_overrides": {
        "concept_hint": { "decision": "allow", "reason_code": "AUTONOMY_ALLOWED" }
      },
      "source": "teacher"
    })" );
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicy( doc );
    REQUIRE( parsed.ok );

    const std::string first = parsed.policy.toJson().toStyledString();
    const AutonomyPolicyParseResult reparsed = parseAutonomyPolicy( parsed.policy.toJson() );
    REQUIRE( reparsed.ok );
    REQUIRE( reparsed.policy.toJson().toStyledString() == first );
    REQUIRE( reparsed.policy.level == AutonomyLevel::L2 );
    REQUIRE( reparsed.policy.mode == autonomy_modes::kExam );
    REQUIRE( reparsed.policy.source == policy_sources::kTeacher );
}

TEST_CASE( "policy text parse reports typed errors without throwing", "[autonomy][policy]" )
{
    const AutonomyPolicyParseResult broken = parseAutonomyPolicyJson( "{ not json" );
    REQUIRE_FALSE( broken.ok );
    REQUIRE_FALSE( broken.errors.empty() );
}

TEST_CASE( "override reason codes come from the closed vocabulary only",
           "[autonomy][policy]" )
{
    // A deny's reason_code rides verbatim into the decision and the audit
    // log: a caller-chosen string would break the closed nine-code
    // contract, so the parse refuses it (and "allowed" on a deny).
    const Json::Value attacker = parseOrThrow( R"({
      "schema": "sicnu.autonomy-policy/1",
      "level": "L0",
      "capability_overrides": {
        "next_step_recommendation":
            { "decision": "deny", "reason_code": "ATTACKER_CHOSEN_CODE" }
      }
    })" );
    const AutonomyPolicyParseResult refused = parseAutonomyPolicy( attacker );
    CHECK_FALSE( refused.ok );
    bool unknownReported = false;
    for ( const std::string &error : refused.errors )
        unknownReported |= error.find( "reason_code.unknown" ) != std::string::npos;
    CHECK( unknownReported );

    const Json::Value allowedOnDeny = parseOrThrow( R"({
      "schema": "sicnu.autonomy-policy/1",
      "level": "L0",
      "capability_overrides": {
        "next_step_recommendation":
            { "decision": "deny", "reason_code": "AUTONOMY_ALLOWED" }
      }
    })" );
    CHECK_FALSE( parseAutonomyPolicy( allowedOnDeny ).ok );

    const Json::Value known = parseOrThrow( R"({
      "schema": "sicnu.autonomy-policy/1",
      "level": "L0",
      "capability_overrides": {
        "next_step_recommendation":
            { "decision": "deny", "reason_code": "AUTONOMY_COURSE_CAP" }
      }
    })" );
    const AutonomyPolicyParseResult accepted = parseAutonomyPolicy( known );
    CHECK( accepted.ok );
    CHECK( accepted.policy.overrides.size() == 1 );
    CHECK( accepted.policy.overrides[0].second.reasonCode ==
           "AUTONOMY_COURSE_CAP" );
}
