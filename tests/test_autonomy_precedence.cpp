// tests/test_autonomy_precedence.cpp
//
// RS14-12 teaching autonomy ladder — Slice D: precedence and mode ceilings.
//
// resolveEffectivePolicy is the ONE merge rule for the four policy sources
// (course < labspec < teacher < session). Properties under test:
//   * per-field resolution — the highest-precedence source that DECLARES a
//     value wins (level, mode, per-capability override);
//   * max_level ceilings take the tightest declared value — no source can
//     loosen another source's cap;
//   * unknown sources grant nothing;
//   * mode ceilings are closed and fail-closed (unknown mode ⇒ L0).
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

#include <string>
#include <vector>

using namespace sicnu::agent::autonomy;

namespace {

AutonomyPolicyLayer layer( const std::string &source, const std::string &json )
{
    const AutonomyPolicyParseResult parsed = parseAutonomyPolicyJson( json );
    REQUIRE( parsed.ok );
    return AutonomyPolicyLayer{ source, parsed.policy };
}

} // namespace

TEST_CASE( "level resolves from the highest-precedence declaring source", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","level":"L1"})" ),
        layer( policy_sources::kLabspec, R"({"schema":"sicnu.autonomy-policy/1","level":"L3"})" ),
        layer( policy_sources::kTeacher, R"({"schema":"sicnu.autonomy-policy/1","level":"L4"})" ),
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1","level":"L2"})" ),
    } );
    REQUIRE( effective.hasLevel );
    REQUIRE( effective.level == AutonomyLevel::L2 );
}

TEST_CASE( "a source that declares nothing does not erase higher declarations", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","level":"L3","mode":"practice"})" ),
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1"})" ),
    } );
    REQUIRE( effective.hasLevel );
    REQUIRE( effective.level == AutonomyLevel::L3 );
    REQUIRE( effective.mode == autonomy_modes::kPractice );
}

TEST_CASE( "mode resolves from the highest-precedence declaring source", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","mode":"exam"})" ),
        layer( policy_sources::kTeacher, R"({"schema":"sicnu.autonomy-policy/1","mode":"instructor"})" ),
    } );
    REQUIRE( effective.mode == autonomy_modes::kInstructor );
}

TEST_CASE( "the tightest max_level cap wins regardless of precedence", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1","level":"L5","max_level":"L5"})" ),
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","max_level":"L1"})" ),
    } );
    REQUIRE( effective.hasMaxLevel );
    REQUIRE( effective.maxLevel == AutonomyLevel::L1 );

    const AutonomyPolicy noLoosening = resolveEffectivePolicy( {
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","max_level":"L2"})" ),
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1","max_level":"L5"})" ),
    } );
    REQUIRE( noLoosening.maxLevel == AutonomyLevel::L2 );
}

TEST_CASE( "per-capability overrides resolve per capability", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( policy_sources::kCourse,
               R"({"schema":"sicnu.autonomy-policy/1","capability_overrides":{
                    "concept_hint":{"decision":"deny"},
                    "next_step_recommendation":{"decision":"allow"}}})" ),
        layer( policy_sources::kTeacher,
               R"({"schema":"sicnu.autonomy-policy/1","capability_overrides":{
                    "concept_hint":{"decision":"allow","reason_code":"AUTONOMY_ALLOWED"}}})" ),
    } );
    // concept_hint: teacher (higher precedence) replaces the course denial.
    const auto conceptEntry = std::find_if(
        effective.overrides.begin(), effective.overrides.end(),
        []( const std::pair<std::string, AutonomyCapabilityOverride> &entry ) {
            return entry.first == assistance_capabilities::kConceptHint;
        } );
    REQUIRE( conceptEntry != effective.overrides.end() );
    REQUIRE( conceptEntry->second.decision == "allow" );
    REQUIRE( conceptEntry->second.reasonCode == autonomy_reason_codes::kAllowed );

    // next_step_recommendation: only the course declared it — inherited.
    const auto nextStep = std::find_if(
        effective.overrides.begin(), effective.overrides.end(),
        []( const std::pair<std::string, AutonomyCapabilityOverride> &entry ) {
            return entry.first == assistance_capabilities::kNextStepRecommendation;
        } );
    REQUIRE( nextStep != effective.overrides.end() );
    REQUIRE( nextStep->second.decision == "allow" );
}

TEST_CASE( "unknown sources are ignored and cannot grant anything", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {
        layer( "marketing", R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"agent"})" ),
    } );
    REQUIRE_FALSE( effective.hasLevel );
    REQUIRE( effective.mode.empty() );
    REQUIRE_FALSE( effective.hasMaxLevel );
    REQUIRE( effective.overrides.empty() );
}

TEST_CASE( "no layers resolve to an inert (fail-closed) policy", "[autonomy][precedence]" )
{
    const AutonomyPolicy effective = resolveEffectivePolicy( {} );
    REQUIRE_FALSE( effective.hasLevel );
    REQUIRE( effective.mode.empty() );
    REQUIRE_FALSE( effective.hasMaxLevel );
    REQUIRE( effective.overrides.empty() );
}

TEST_CASE( "mode ceilings are closed and fail-closed", "[autonomy][precedence]" )
{
    REQUIRE( modeCeiling( autonomy_modes::kExam ) == AutonomyLevel::L2 );
    REQUIRE( modeCeiling( autonomy_modes::kPractice ) == AutonomyLevel::L4 );
    REQUIRE( modeCeiling( autonomy_modes::kInstructor ) == AutonomyLevel::L5 );
    REQUIRE( modeCeiling( autonomy_modes::kAgent ) == AutonomyLevel::L5 );
    REQUIRE( modeCeiling( "" ) == AutonomyLevel::L0 );
    REQUIRE( modeCeiling( "sandbox" ) == AutonomyLevel::L0 );
}

TEST_CASE( "precedence resolution is deterministic across input orders", "[autonomy][precedence]" )
{
    const std::vector<AutonomyPolicyLayer> forward = {
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","level":"L1"})" ),
        layer( policy_sources::kLabspec, R"({"schema":"sicnu.autonomy-policy/1","level":"L2"})" ),
        layer( policy_sources::kTeacher, R"({"schema":"sicnu.autonomy-policy/1","level":"L3"})" ),
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1","level":"L4"})" ),
    };
    const std::vector<AutonomyPolicyLayer> shuffled = {
        layer( policy_sources::kTeacher, R"({"schema":"sicnu.autonomy-policy/1","level":"L3"})" ),
        layer( policy_sources::kSession, R"({"schema":"sicnu.autonomy-policy/1","level":"L4"})" ),
        layer( policy_sources::kCourse, R"({"schema":"sicnu.autonomy-policy/1","level":"L1"})" ),
        layer( policy_sources::kLabspec, R"({"schema":"sicnu.autonomy-policy/1","level":"L2"})" ),
    };
    REQUIRE( resolveEffectivePolicy( forward ).toJson().toStyledString() ==
             resolveEffectivePolicy( shuffled ).toJson().toStyledString() );
}
