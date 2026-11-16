// tests/test_r32_t04_refverify.cpp — WP08: the archived reference verifier's
// rendered surfaces must honor the run-level budget downgrade (#2116, in
// src/verification — reference module, NOT the production src/verify chain).
#include <catch2/catch_test_macros.hpp>

#include "verification/report.h"
#include "verification/render_agent.h"
#include "verification/render_teaching.h"
#include "verification/failure_codes.h"

using namespace sicnu::verification;

namespace
{
// A report whose checks are ALL Pass but whose run-level budget usage marks
// an overrun — the exact #2116 mask: buildReport demoted status/outcome to
// Indeterminate with VERIFY.BUDGET_EXCEEDED, and the renderers re-folded the
// all-Pass checks back to "pass".
VerificationReport overBudgetAllPassReport()
{
    VerificationReport report;
    report.schemaVersion = kVerificationReportSchemaVersion;
    report.specId = "spec/budget";
    report.specDigest = std::string( 64, 'd' );

    CheckResult result;
    result.checkId = "check.ok";
    result.kind = "artifact.exists";
    result.status = CheckStatus::Pass;
    report.results.push_back( result );

    NodeOutcome node;
    node.nodeId = "node.root";
    node.status = CheckStatus::Pass;
    node.checkIds = { "check.ok" };
    report.nodes.push_back( node );

    report.status = CheckStatus::Indeterminate;
    report.outcome.status = CheckStatus::Indeterminate;
    report.outcome.failureCodes = { failure_codes::kBudgetExceeded };
    report.budgetUsage.exceeded = true;
    report.budgetUsage.reasons = { "node budget exceeded" };
    return report;
}
} // namespace

TEST_CASE( "WP08 budget-overrun report renders as indeterminate with its code",
           "[r32t04][wp08][reference]" )
{
    const VerificationReport report = overBudgetAllPassReport();

    REQUIRE( derivedStatus( report ) == CheckStatus::Indeterminate );
    const auto codes = derivedFailureCodes( report );
    bool hasBudgetCode = false;
    for ( const auto &code : codes )
        if ( code == failure_codes::kBudgetExceeded )
            hasBudgetCode = true;
    REQUIRE( hasBudgetCode );

    // Both rendered surfaces agree with the report, not with the sub-check
    // majority.
    const auto agent = renderAgent( report );
    REQUIRE( agent.status == std::string( "indeterminate" ) );
    bool agentHasCode = false;
    for ( const auto &code : agent.failureCodes )
        if ( code == failure_codes::kBudgetExceeded )
            agentHasCode = true;
    REQUIRE( agentHasCode );

    const auto teaching = renderTeaching( report );
    // The downgrade must render in the UNVERIFIED vocabulary — an
    // all-Pass-checks fold must not surface "trustworthy".
    REQUIRE( teaching.verdict == std::string( kTeachingVerdictUnverified ) );
}

TEST_CASE( "WP08 an honest all-Pass report still renders pass",
           "[r32t04][wp08][reference]" )
{
    VerificationReport report = overBudgetAllPassReport();
    report.status = CheckStatus::Pass;
    report.outcome.status = CheckStatus::Pass;
    report.outcome.failureCodes.clear();
    report.budgetUsage.exceeded = false;
    report.budgetUsage.reasons.clear();

    REQUIRE( derivedStatus( report ) == CheckStatus::Pass );
    REQUIRE( renderAgent( report ).status == std::string( "pass" ) );
    REQUIRE( renderTeaching( report ).verdict == std::string( kTeachingVerdictTrustworthy ) );
}
