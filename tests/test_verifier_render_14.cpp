/***************************************************************************
  test_verifier_render_14.cpp — the two rendered surfaces of a report (RS14-14)

  Why this lane exists, in one sentence: the teaching and agent views are what
  a student and an Agent actually read, so a renderer that says "correct" when
  the truth is "we could not determine" is the most damaging defect available
  to this track — it converts absence of evidence into permission to ship.

  Everything the lane asserts is therefore about the DIRECTION of error:

    1. two-level roll-up: Fail anywhere dominates; one Indeterminate node keeps
       the task Indeterminate and names itself as a blocker;
    2. nine Pass nodes must not dilute one Indeterminate node;
    3. all three teaching verdicts are reachable, each with reasons, and the
       `unverified` one never borrows the vocabulary of `trustworthy`
       (keyword guard over the whole serialized view);
    4. no teaching check is allowed a blank expected/observed/whyItMatters/
       howToFix cell;
    5. the agent view's failure codes are EXACTLY the codes on non-Pass
       results — no invented codes, no dropped codes;
    6. teaching verdict and agent status are synonyms, tested as a matrix;
    7. rendering is deterministic and survives a report round-trip;
    8. an EMPTY report is unverified/indeterminate. This is the fail-open path.

  Fixtures are built in memory (no runner, no providers, no filesystem), so
  this lane is independent of the in-flight check families and packs.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/canonical_json.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/render_agent.h"
#include "verification/render_teaching.h"
#include "verification/report.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace
{

using sicnu::verification::AgentView;
using sicnu::verification::CheckResult;
using sicnu::verification::CheckStatus;
using sicnu::verification::EvidenceCoverage;
using sicnu::verification::NodeOutcome;
using sicnu::verification::TeachingView;
using sicnu::verification::VerificationReport;

// ---------------------------------------------------------------------------
// The exact vocabulary the `unverified` verdict must never contain.
//
// These are NOT generic bad words: each one is a term a careless renderer
// reaches for when it softens "we could not determine" into "probably fine".
// The list is fixed by what THIS renderer can emit, and it is asserted over the
// whole serialized view, so a new phrase anywhere in the surface trips it.
//
//   correct      the trustworthy verdict's own claim about the science
//   trustworthy  the trustworthy verdict's own name
//   trusted      the trustworthy headline's own verb ("this result can be trusted")
//   passed       the pass verdict in its past-tense form
//   valid        "still valid", the classic softening of an unknown
//   success      the pass verdict in its outcome form
//   accept       "safe to accept", the action a student would then take
//
// "verified" is deliberately NOT on the list: it is a substring of
// "unverified", which is the verdict spelling itself.
const char *const kForbiddenPassWording[] = { "correct", "trustworthy", "trusted", "passed", "valid",
                                              "success", "accept" };

std::string canonicalText( const Json::Value &value )
{
    std::string text;
    std::string error;
    const bool ok = sicnu::verification::canonicalJson( value, text, error );
    REQUIRE( ok );
    return text;
}

std::string lowercased( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return text;
}

void requireNoPassWording( const std::string &text )
{
    const std::string haystack = lowercased( text );
    for ( const char *word : kForbiddenPassWording )
    {
        INFO( "forbidden pass wording: " << word );
        REQUIRE( haystack.find( word ) == std::string::npos );
    }
}

// ---------------------------------------------------------------------------
// fixtures
// ---------------------------------------------------------------------------

CheckResult makeCheck( const std::string &id, const std::string &kind, const std::string &title,
                       CheckStatus status, const std::string &code, const std::string &message,
                       const Json::Value &observed, const Json::Value &expected,
                       EvidenceCoverage coverage = EvidenceCoverage::Full )
{
    CheckResult result;
    result.checkId = id;
    result.kind = kind;
    result.title = title;
    result.status = status;
    result.failureCode = code;
    result.message = message;
    result.evidence.kind = kind;
    result.evidence.coverage = coverage;
    result.evidence.observed = observed;
    result.evidence.expected = expected;
    result.evidence.sourceId = "lane-fixture";
    return result;
}

struct NodeInput
{
    std::string id;
    std::vector<std::string> checkIds;
};

/// Assembles a well-formed report: node outcomes come from the real level-1
/// roll-up and the task outcome from the real level-2 roll-up, so this lane
/// tests the RENDERER and not the roll-up.
VerificationReport assemble( const std::vector<CheckResult> &results,
                             const std::vector<NodeInput> &nodeInputs )
{
    VerificationReport report;
    report.specId = "lab01.reflectance";
    report.results = results;
    for ( const NodeInput &input : nodeInputs )
    {
        report.nodes.push_back(
            sicnu::verification::rollUpNode( input.id, input.checkIds, results ) );
    }
    report.outcome = sicnu::verification::rollUpTask( report.nodes );
    report.status = report.outcome.status;
    report.budgetUsage.checksEvaluated = results.size();
    report.budgetUsage.nodes = report.nodes.size();
    return report;
}

Json::Value numberObject( const char *key, double value )
{
    Json::Value object{ Json::objectValue };
    object[key] = value;
    return object;
}

/// A clean, fully-passing report: two nodes, two checks.
VerificationReport allPassReport()
{
    const std::vector<CheckResult> results {
        makeCheck( "grid.shape", "artifact_shape", "Output keeps the declared grid",
                   CheckStatus::Pass, "", "If the grid drifts, comparing pixels against a "
                                          "reference stops meaning anything.",
                   numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ),
        makeCheck( "range.reflectance", "numeric_range", "Reflectance stays inside the declared range",
                   CheckStatus::Pass, "", "Reflectance outside 0..1 cannot be a surface property.",
                   numberObject( "max", 0.8731 ), numberObject( "max", 1.0 ) ),
    };
    return assemble( results, { { "radiometric.apply", { "grid.shape" } },
                                { "reflectance.build", { "range.reflectance" } } } );
}

/// One node violates a declared expectation.
VerificationReport failingReport()
{
    const std::vector<CheckResult> results {
        makeCheck( "grid.shape", "artifact_shape", "Output keeps the declared grid",
                   CheckStatus::Pass, "", "If the grid drifts, comparing pixels against a "
                                          "reference stops meaning anything.",
                   numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ),
        makeCheck( "range.reflectance", "numeric_range", "Reflectance stays inside the declared range",
                   CheckStatus::Fail, sicnu::verification::failure_codes::kNumericOutOfRange,
                   "The brightest pixel sits outside the range a surface can have.",
                   numberObject( "max", 1.834 ), numberObject( "max", 1.0 ) ),
        makeCheck( "provenance.source", "provenance_completeness", "Provenance names the calibration source",
                   CheckStatus::Pass, "", "Without a source the reflectance cannot be cited.",
                   numberObject( "fields", 4.0 ), numberObject( "fields", 4.0 ) ),
    };
    return assemble( results, { { "radiometric.apply", { "grid.shape" } },
                                { "reflectance.build", { "range.reflectance" } },
                                { "provenance.write", { "provenance.source" } } } );
}

/// One node could not be judged: its evidence was never obtained.
VerificationReport indeterminateReport()
{
    const std::vector<CheckResult> results {
        makeCheck( "grid.shape", "artifact_shape", "Output keeps the declared grid",
                   CheckStatus::Pass, "", "If the grid drifts, comparing pixels against a "
                                          "reference stops meaning anything.",
                   numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ),
        makeCheck( "range.reflectance", "numeric_range", "Reflectance stays inside the declared range",
                   CheckStatus::Indeterminate,
                   sicnu::verification::failure_codes::kEvidenceUnavailable,
                   "The reflectance statistics could not be read, so nothing is claimed about them.",
                   Json::Value{ Json::objectValue }, numberObject( "max", 1.0 ),
                   EvidenceCoverage::Unavailable ),
    };
    return assemble( results, { { "radiometric.apply", { "grid.shape" } },
                                { "reflectance.build", { "range.reflectance" } } } );
}

/// Nine nodes that are fine, one that is unknown.
VerificationReport ninePassOneUnknownReport()
{
    std::vector<CheckResult> results;
    std::vector<NodeInput> nodeInputs;
    for ( int index = 0; index < 9; ++index )
    {
        const std::string id = "grid.node" + std::to_string( index );
        results.push_back( makeCheck( id, "artifact_shape", "Output keeps the declared grid",
                                      CheckStatus::Pass, "", "Every tile must keep the grid the "
                                                             "reference was built on.",
                                      numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ) );
        nodeInputs.push_back( { "node" + std::to_string( index ), { id } } );
    }
    results.push_back( makeCheck( "evidence.node9", "numeric_range",
                                  "Reflectance stays inside the declared range",
                                  CheckStatus::Indeterminate,
                                  sicnu::verification::failure_codes::kEvidenceRefused,
                                  "The provider holding the reflectance facts refused to answer.",
                                  Json::Value{ Json::objectValue }, numberObject( "max", 1.0 ),
                                  EvidenceCoverage::Unavailable ) );
    nodeInputs.push_back( { "node9", { "evidence.node9" } } );
    return assemble( results, nodeInputs );
}

} // namespace

TEST_CASE( "verifier14: a failing node fails the whole task and names itself as the blocker",
           "[verifier14][render]" )
{
    const VerificationReport report = failingReport();
    const AgentView agent = sicnu::verification::renderAgent( report );

    REQUIRE( sicnu::verification::derivedStatus( report ) == CheckStatus::Fail );
    REQUIRE( agent.status == "fail" );
    REQUIRE( agent.blockingNodes == std::vector<std::string>{ "reflectance.build" } );
    REQUIRE( agent.firstBlockingNode == "reflectance.build" );
    REQUIRE( agent.failureCodes ==
             std::vector<std::string>{ sicnu::verification::failure_codes::kNumericOutOfRange } );
    REQUIRE( agent.replanClasses == std::vector<std::string>{ "replan" } );
    REQUIRE( agent.suggestedActions.size() == agent.failureCodes.size() );

    // The renderer must agree with the real roll-up: this lane tests rendering,
    // not arithmetic, so any disagreement is a renderer that invented a verdict.
    REQUIRE( report.outcome.status == CheckStatus::Fail );
    REQUIRE( sicnu::verification::rollUpTask( report.nodes ).blockingNodes == agent.blockingNodes );

    const TeachingView teaching = sicnu::verification::renderTeaching( report );
    REQUIRE( teaching.verdict == sicnu::verification::kTeachingVerdictNotTrustworthy );
    REQUIRE_FALSE( teaching.reasons.empty() );
    REQUIRE_FALSE( teaching.headline.empty() );
}

TEST_CASE( "verifier14: an indeterminate node keeps the task indeterminate and is listed as blocking",
           "[verifier14][render]" )
{
    const VerificationReport report = indeterminateReport();
    const AgentView agent = sicnu::verification::renderAgent( report );

    REQUIRE( sicnu::verification::derivedStatus( report ) == CheckStatus::Indeterminate );
    REQUIRE( agent.status == "indeterminate" );
    REQUIRE( agent.blockingNodes == std::vector<std::string>{ "reflectance.build" } );
    REQUIRE( agent.firstBlockingNode == "reflectance.build" );
    REQUIRE( agent.failureCodes ==
             std::vector<std::string>{ sicnu::verification::failure_codes::kEvidenceUnavailable } );
    REQUIRE( agent.replanClasses == std::vector<std::string>{ "retry" } );

    const TeachingView teaching = sicnu::verification::renderTeaching( report );
    REQUIRE( teaching.verdict == sicnu::verification::kTeachingVerdictUnverified );
    REQUIRE_FALSE( teaching.reasons.empty() );
    REQUIRE_FALSE( teaching.headline.empty() );
}

TEST_CASE( "verifier14: nine passing nodes do not dilute one indeterminate node",
           "[verifier14][render]" )
{
    const VerificationReport report = ninePassOneUnknownReport();
    REQUIRE( report.nodes.size() == 10u );

    const AgentView agent = sicnu::verification::renderAgent( report );
    REQUIRE( agent.status == "indeterminate" );
    REQUIRE( sicnu::verification::derivedStatus( report ) == CheckStatus::Indeterminate );

    // Majority rule is exactly the bug: 9 good nodes must not absorb the 10th.
    REQUIRE( agent.blockingNodes == std::vector<std::string>{ "node9" } );
    REQUIRE( agent.failureCodes ==
             std::vector<std::string>{ sicnu::verification::failure_codes::kEvidenceRefused } );

    const TeachingView teaching = sicnu::verification::renderTeaching( report );
    REQUIRE( teaching.verdict == sicnu::verification::kTeachingVerdictUnverified );
    REQUIRE( teaching.verdict != sicnu::verification::kTeachingVerdictTrustworthy );
    REQUIRE( teaching.verdict != sicnu::verification::kTeachingVerdictNotTrustworthy );
}

TEST_CASE( "verifier14: all three teaching verdicts are reachable and each carries reasons",
           "[verifier14][render]" )
{
    const VerificationReport passReport = allPassReport();
    const VerificationReport failReport = failingReport();
    const VerificationReport unknownReport = ninePassOneUnknownReport();

    const TeachingView trusted = sicnu::verification::renderTeaching( passReport );
    const TeachingView untrusted = sicnu::verification::renderTeaching( failReport );
    const TeachingView unverified = sicnu::verification::renderTeaching( unknownReport );

    REQUIRE( trusted.verdict == sicnu::verification::kTeachingVerdictTrustworthy );
    REQUIRE( untrusted.verdict == sicnu::verification::kTeachingVerdictNotTrustworthy );
    REQUIRE( unverified.verdict == sicnu::verification::kTeachingVerdictUnverified );

    for ( const TeachingView *view : { &trusted, &untrusted, &unverified } )
    {
        INFO( "verdict: " << view->verdict );
        REQUIRE_FALSE( view->reasons.empty() );
        REQUIRE_FALSE( view->headline.empty() );
        REQUIRE( view->schema == sicnu::verification::kTeachingSchema );
        for ( const std::string &reason : view->reasons )
        {
            REQUIRE_FALSE( reason.empty() );
        }
    }

    // A verdict that shares its wording with another verdict is not three
    // answers, it is two answers with a synonym.
    REQUIRE( trusted.headline != untrusted.headline );
    REQUIRE( trusted.headline != unverified.headline );
    REQUIRE( untrusted.headline != unverified.headline );
}

TEST_CASE( "verifier14: an unverified verdict never borrows the wording of a trustworthy one",
           "[verifier14][render]" )
{
    const std::vector<VerificationReport> unverifiedReports {
        VerificationReport{},                     // nothing at all was verified
        indeterminateReport(),                    // one node's evidence is missing
        ninePassOneUnknownReport(),               // nine fine nodes, one unknown
    };

    for ( const VerificationReport &report : unverifiedReports )
    {
        const TeachingView view = sicnu::verification::renderTeaching( report );
        INFO( "headline: " << view.headline );
        REQUIRE( view.verdict == sicnu::verification::kTeachingVerdictUnverified );

        // The guard that matters: over the WHOLE serialized view, not just the
        // headline. A softening phrase in any field reaches the student.
        requireNoPassWording( canonicalText( view.toJson() ) );
        requireNoPassWording( view.headline );
        requireNoPassWording( view.verdict );
        for ( const std::string &reason : view.reasons )
        {
            INFO( "reason: " << reason );
            requireNoPassWording( reason );
        }
        for ( const sicnu::verification::TeachingCheckView &check : view.checks )
        {
            requireNoPassWording( check.expected );
            requireNoPassWording( check.observed );
            requireNoPassWording( check.whyItMatters );
            requireNoPassWording( check.howToFix );
        }
    }

    // And the same words ARE expected in the trustworthy view — otherwise the
    // guard would be vacuous, passing because the renderer never says anything.
    const TeachingView trusted = sicnu::verification::renderTeaching( allPassReport() );
    REQUIRE( trusted.verdict == sicnu::verification::kTeachingVerdictTrustworthy );
    REQUIRE( lowercased( canonicalText( trusted.toJson() ) ).find( "trustworthy" ) !=
             std::string::npos );
}

TEST_CASE( "verifier14: every teaching check populates expected, observed, whyItMatters and howToFix",
           "[verifier14][render]" )
{
    // The last check is deliberately bare: no message, no code, no evidence, no
    // hints. A renderer that only fills what it was handed leaves four blank
    // cells, and a blank "how do I fix this" is not a neutral cell — it moves
    // the work back onto the student.
    const std::vector<CheckResult> results {
        makeCheck( "grid.shape", "artifact_shape", "Output keeps the declared grid",
                   CheckStatus::Pass, "", "Every tile must keep the grid the reference was built on.",
                   numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ),
        makeCheck( "range.reflectance", "numeric_range", "Reflectance stays inside the declared range",
                   CheckStatus::Fail, sicnu::verification::failure_codes::kNumericOutOfRange,
                   "Reflectance above 1.0 is not a surface property.",
                   numberObject( "max", 1.834 ), numberObject( "max", 1.0 ) ),
        makeCheck( "provenance.source", "provenance_completeness", "", CheckStatus::Indeterminate,
                   sicnu::verification::failure_codes::kEvidenceUnavailable, "",
                   Json::Value{ Json::objectValue }, Json::Value{ Json::objectValue },
                   EvidenceCoverage::Unavailable ),
        makeCheck( "bare.check", "state_invariant", "", CheckStatus::Indeterminate, "", "",
                   Json::Value{ Json::objectValue }, Json::Value{ Json::objectValue },
                   EvidenceCoverage::Unavailable ),
    };
    const VerificationReport report = assemble(
        results, { { "node.a", { "grid.shape" } },
                   { "node.b", { "range.reflectance" } },
                   { "node.c", { "provenance.source", "bare.check" } } } );

    const TeachingView view = sicnu::verification::renderTeaching( report );
    REQUIRE( view.checks.size() == results.size() );

    for ( const sicnu::verification::TeachingCheckView &check : view.checks )
    {
        INFO( "check: " << check.checkId );
        REQUIRE_FALSE( check.expected.empty() );
        REQUIRE_FALSE( check.observed.empty() );
        REQUIRE_FALSE( check.whyItMatters.empty() );
        REQUIRE_FALSE( check.howToFix.empty() );
        REQUIRE_FALSE( check.title.empty() );   // falls back to the id, never blank
        REQUIRE_FALSE( check.checkId.empty() );
    }
}

TEST_CASE( "verifier14: agent failure codes are exactly the codes on non-pass results",
           "[verifier14][render]" )
{
    // Four distinct scenarios, each with its own code set; the agent view must
    // reproduce each set exactly — no code invented, none dropped.
    const std::vector<CheckResult> mixed {
        makeCheck( "artifact.exists", "artifact_shape", "Artifact exists", CheckStatus::Fail,
                   sicnu::verification::failure_codes::kArtifactMissing, "The output is absent.",
                   Json::Value{ Json::objectValue }, Json::Value{ Json::objectValue } ),
        makeCheck( "artifact.grid", "artifact_shape", "Artifact keeps the grid", CheckStatus::Fail,
                   sicnu::verification::failure_codes::kArtifactGridMismatch, "Grid differs.",
                   Json::Value{ Json::objectValue }, Json::Value{ Json::objectValue } ),
        makeCheck( "artifact.grid.again", "artifact_shape", "Artifact keeps the grid (repeat)",
                   CheckStatus::Fail, sicnu::verification::failure_codes::kArtifactGridMismatch,
                   "Grid differs again.", Json::Value{ Json::objectValue },
                   Json::Value{ Json::objectValue } ),
        makeCheck( "spec.shape", "state_invariant", "Spec is readable", CheckStatus::Indeterminate,
                   sicnu::verification::failure_codes::kSpecInvalid, "Spec could not be read.",
                   Json::Value{ Json::objectValue }, Json::Value{ Json::objectValue } ),
        makeCheck( "grid.shape", "artifact_shape", "Output keeps the declared grid",
                   CheckStatus::Pass, "", "Grid is what the reference expects.",
                   numberObject( "rows", 512.0 ), numberObject( "rows", 512.0 ) ),
    };
    const VerificationReport report = assemble( mixed, { { "node.a", { "artifact.exists" } },
                                                         { "node.b", { "artifact.grid",
                                                                       "artifact.grid.again" } },
                                                         { "node.c", { "spec.shape" } },
                                                         { "node.d", { "grid.shape" } } } );

    // The ground truth, read straight off the report.
    std::vector<std::string> expectedCodes;
    for ( const CheckResult &result : report.results )
    {
        if ( result.status == CheckStatus::Pass || result.failureCode.empty() )
        {
            continue;
        }
        expectedCodes.push_back( result.failureCode );
    }
    std::sort( expectedCodes.begin(), expectedCodes.end() );
    expectedCodes.erase( std::unique( expectedCodes.begin(), expectedCodes.end() ),
                         expectedCodes.end() );
    REQUIRE( expectedCodes.size() == 3u );

    const AgentView agent = sicnu::verification::renderAgent( report );
    REQUIRE( agent.failureCodes == expectedCodes );

    // Every code is one the closed table knows; every replan class is derived
    // from those codes and nothing else.
    for ( const std::string &code : agent.failureCodes )
    {
        REQUIRE( sicnu::verification::isKnownFailureCode( code ) );
    }
    std::vector<std::string> expectedClasses;
    for ( const std::string &code : expectedCodes )
    {
        expectedClasses.push_back(
            sicnu::verification::replanClassToWire( sicnu::verification::replanClassForCode( code ) ) );
    }
    std::sort( expectedClasses.begin(), expectedClasses.end() );
    expectedClasses.erase( std::unique( expectedClasses.begin(), expectedClasses.end() ),
                           expectedClasses.end() );
    REQUIRE( agent.replanClasses == expectedClasses );
    REQUIRE( agent.suggestedActions.size() == agent.failureCodes.size() );
    for ( const std::string &action : agent.suggestedActions )
    {
        REQUIRE_FALSE( action.empty() );
    }

    // A clean report carries no codes at all — inventing "none" here would be
    // the same dishonesty in the opposite direction.
    const AgentView clean = sicnu::verification::renderAgent( allPassReport() );
    REQUIRE( clean.status == "pass" );
    REQUIRE( clean.failureCodes.empty() );
    REQUIRE( clean.replanClasses.empty() );
    REQUIRE( clean.suggestedActions.empty() );
    REQUIRE( clean.blockingNodes.empty() );
    REQUIRE( clean.firstBlockingNode.empty() );
}

TEST_CASE( "verifier14: teaching verdict and agent status are synonyms across all three states",
           "[verifier14][render]" )
{
    struct Expectation
    {
        const char *name;
        VerificationReport report;
        const char *verdict;
        const char *status;
    };

    const std::vector<Expectation> matrix {
        { "all expectations met", allPassReport(),
          sicnu::verification::kTeachingVerdictTrustworthy, "pass" },
        { "an expectation violated", failingReport(),
          sicnu::verification::kTeachingVerdictNotTrustworthy, "fail" },
        { "evidence never obtained", indeterminateReport(),
          sicnu::verification::kTeachingVerdictUnverified, "indeterminate" },
    };

    for ( const Expectation &expectation : matrix )
    {
        INFO( "state: " << expectation.name );
        const TeachingView teaching = sicnu::verification::renderTeaching( expectation.report );
        const AgentView agent = sicnu::verification::renderAgent( expectation.report );
        REQUIRE( teaching.verdict == expectation.verdict );
        REQUIRE( agent.status == expectation.status );
    }

    // The mapping is a bijection, not merely three consistent pairs: three
    // distinct verdicts over three distinct statuses, and no fourth spelling.
    REQUIRE( matrix[0].verdict != matrix[1].verdict );
    REQUIRE( matrix[1].verdict != matrix[2].verdict );
    REQUIRE( matrix[0].verdict != matrix[2].verdict );
    REQUIRE( matrix[0].status != matrix[1].status );
    REQUIRE( matrix[1].status != matrix[2].status );
    REQUIRE( matrix[0].status != matrix[2].status );
}

TEST_CASE( "verifier14: both views render deterministically and survive a report round-trip",
           "[verifier14][render]" )
{
    const VerificationReport report = ninePassOneUnknownReport();

    const std::string teachingFirst = canonicalText( sicnu::verification::renderTeaching( report ).toJson() );
    const std::string teachingSecond = canonicalText( sicnu::verification::renderTeaching( report ).toJson() );
    const std::string agentFirst = canonicalText( sicnu::verification::renderAgent( report ).toJson() );
    const std::string agentSecond = canonicalText( sicnu::verification::renderAgent( report ).toJson() );

    REQUIRE( teachingFirst == teachingSecond );
    REQUIRE( agentFirst == agentSecond );

    // The report carries no clock and no environment by design, so a round-trip
    // through JSON must render identically.
    VerificationReport reloaded;
    std::string error;
    REQUIRE( VerificationReport::fromJson( report.toJson(), reloaded, error ) );
    REQUIRE( canonicalText( sicnu::verification::renderTeaching( reloaded ).toJson() ) == teachingFirst );
    REQUIRE( canonicalText( sicnu::verification::renderAgent( reloaded ).toJson() ) == agentFirst );

    const Json::Value teachingJson = sicnu::verification::renderTeaching( report ).toJson();
    const Json::Value agentJson = sicnu::verification::renderAgent( report ).toJson();
    REQUIRE( teachingJson.isObject() );
    REQUIRE( teachingJson["schema"].asString() == sicnu::verification::kTeachingSchema );
    REQUIRE( teachingJson.isMember( "verdict" ) );
    REQUIRE( teachingJson.isMember( "headline" ) );
    REQUIRE( teachingJson["reasons"].isArray() );
    REQUIRE( teachingJson["checks"].isArray() );
    REQUIRE( agentJson.isObject() );
    REQUIRE( agentJson["schema"].asString() == sicnu::verification::kAgentSchema );
    REQUIRE( agentJson.isMember( "status" ) );
    REQUIRE( agentJson["failure_codes"].isArray() );
    REQUIRE( agentJson["replan_classes"].isArray() );
    REQUIRE( agentJson["suggested_actions"].isArray() );
    REQUIRE( agentJson["blocking_nodes"].isArray() );
    REQUIRE( agentJson["first_blocking_node"].isString() );
}

TEST_CASE( "verifier14: an empty report renders as unverified and indeterminate, never trustworthy",
           "[verifier14][render]" )
{
    const VerificationReport empty;
    REQUIRE( empty.results.empty() );
    REQUIRE( empty.nodes.empty() );

    const TeachingView teaching = sicnu::verification::renderTeaching( empty );
    const AgentView agent = sicnu::verification::renderAgent( empty );

    REQUIRE( teaching.verdict == sicnu::verification::kTeachingVerdictUnverified );
    REQUIRE( teaching.verdict != sicnu::verification::kTeachingVerdictTrustworthy );
    REQUIRE( agent.status == "indeterminate" );
    REQUIRE( agent.status != "pass" );
    REQUIRE_FALSE( teaching.reasons.empty() );
    REQUIRE_FALSE( teaching.headline.empty() );

    // No evidence means no codes, no classes, and — crucially — no invented
    // blocker to point at.
    REQUIRE( agent.failureCodes.empty() );
    REQUIRE( agent.replanClasses.empty() );
    REQUIRE( agent.suggestedActions.empty() );
    REQUIRE( agent.blockingNodes.empty() );
    REQUIRE( agent.firstBlockingNode.empty() );
    REQUIRE( teaching.checks.empty() );

    requireNoPassWording( canonicalText( teaching.toJson() ) );
}

TEST_CASE( "verifier14: a summary that claims pass over an indeterminate node is not believed",
           "[verifier14][render]" )
{
    // The adversarial shape this whole slice is calibrated for: the roll-up was
    // mangled (or hand-written) and the summary field says pass while a node is
    // still unknown. A renderer that trusts the summary ships it.
    VerificationReport report = ninePassOneUnknownReport();
    REQUIRE( report.nodes.back().status == CheckStatus::Indeterminate );
    report.status = CheckStatus::Pass;
    report.outcome.status = CheckStatus::Pass;

    REQUIRE( sicnu::verification::derivedStatus( report ) == CheckStatus::Indeterminate );
    REQUIRE( sicnu::verification::renderAgent( report ).status == "indeterminate" );
    const TeachingView teaching = sicnu::verification::renderTeaching( report );
    REQUIRE( teaching.verdict == sicnu::verification::kTeachingVerdictUnverified );
    requireNoPassWording( canonicalText( teaching.toJson() ) );
}
