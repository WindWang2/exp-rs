// tests/test_autonomy_holder.cpp
//
// RS14-12 teaching autonomy ladder — Slice E: the policy holder.
//
// The holder installs the course layer once and resolves effective policies
// with the one merge rule. Properties under test:
//   * the research default keeps pre-autonomy research flows behavior-
//     compatible (L5 / agent mode);
//   * a malformed course policy is refused with typed problems and the
//     previous policy is kept (never a half-applied policy);
//   * effectivePolicy composes course + extra layers by precedence.
// Pure value-object suite: no Qt, no QGIS, no network.

#include <catch2/catch_test_macros.hpp>

#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

#include <string>
#include <vector>

using namespace sicnu::agent::autonomy;

TEST_CASE( "the default course policy is the research default", "[autonomy][holder]" )
{
    const AutonomyPolicy defaultPolicy = AutonomyPolicyHolder::researchDefaultPolicy();
    REQUIRE( defaultPolicy.hasLevel );
    REQUIRE( defaultPolicy.level == AutonomyLevel::L5 );
    REQUIRE( defaultPolicy.mode == autonomy_modes::kAgent );
}

TEST_CASE( "installing a malformed course policy is refused and keeps the previous one",
           "[autonomy][holder]" )
{
    AutonomyPolicyHolder &holder = AutonomyPolicyHolder::instance();
    holder.installCoursePolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"exam"})" );
    const AutonomyPolicy before = holder.coursePolicy();
    REQUIRE( before.level == AutonomyLevel::L2 );

    REQUIRE_FALSE( holder.installCoursePolicyJson(
        R"({"schema":"sicnu.autonomy-policy/2","level":"L5"})" ) );
    REQUIRE_FALSE( holder.problems().empty() );
    REQUIRE( holder.coursePolicy().level == AutonomyLevel::L2 );

    REQUIRE( holder.installCoursePolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L1","mode":"practice"})" ) );
    REQUIRE( holder.coursePolicy().level == AutonomyLevel::L1 );
    REQUIRE( holder.problems().empty() );
}

TEST_CASE( "effectivePolicy composes the course layer with session layers", "[autonomy][holder]" )
{
    AutonomyPolicyHolder &holder = AutonomyPolicyHolder::instance();
    holder.installCoursePolicyJson(
        R"({"schema":"sicnu.autonomy-policy/1","level":"L1","mode":"exam","max_level":"L2"})" );

    const AutonomyPolicy effective = holder.effectivePolicy( {
        AutonomyPolicyLayer{ policy_sources::kSession,
                             parseAutonomyPolicyJson(
                                 R"({"schema":"sicnu.autonomy-policy/1","level":"L3"})" )
                                 .policy },
    } );
    REQUIRE( effective.hasLevel );
    REQUIRE( effective.level == AutonomyLevel::L3 );
    REQUIRE( effective.mode == autonomy_modes::kExam );
    REQUIRE( effective.hasMaxLevel );
    REQUIRE( effective.maxLevel == AutonomyLevel::L2 );

    // Restore the research default for other suites in this process.
    holder.installCoursePolicy( AutonomyPolicyHolder::researchDefaultPolicy() );
}
