/***************************************************************************
  tests/test_r18_e_verifier_rules.cpp — R18-E E04 evidence-rule lane.

  RED-first pins for the verification runner's composition seams:

    V2: a spec that declares the same check id twice is ambiguous. The
      roll-up must never let the first result for an id mask the second
      (a [x(Pass), x(Fail)] spec used to roll up as Pass while
      counters.by_status accused the fail). Duplicates degrade to
      Indeterminate VERIFY.SPEC_INVALID and the outcome names the defect;
      the roll-up itself combines ALL results for an id worst-wins so the
      defensive layer holds even for callers that bypass the runner.

    V2-pin: unique ids keep their exact verdicts (regression guard).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/check_runner.h"
#include "verification/failure_codes.h"
#include "verification/providers.h"
#include "verification/report.h"
#include "verification/spec.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace sicnu::verification;

namespace
{

/// Every seam answers Found with plausible facts, so the checks conclude
/// instead of staying unavailable (same shape as the r16a omniscient lane).
class OmniscientProvider : public StateProvider,
                           public ArtifactProvider,
                           public MetricProvider,
                           public ProvenanceProvider,
                           public DigestProvider
{
  public:
    Availability tryGet( const std::string &, StateSnapshot &out, std::string & ) const override
    {
        out.numericDomain = "linear";
        return Availability::Found;
    }
    Availability describe( const std::string &, Json::Value &facts, std::string & ) const override
    {
        facts = Json::Value{ Json::objectValue };
        facts["kind"] = "raster";
        facts["size"]["width"] = 512;
        facts["size"]["height"] = 512;
        facts["mean"] = 0.5;
        return Availability::Found;
    }
    Availability metric( const std::string &, MetricValue &out, std::string & ) const override
    {
        out.value = 0.25;
        out.unit = "1";
        return Availability::Found;
    }
    Availability completeness( const std::string &, std::vector<ProvenanceDimension> &out,
                               std::string & ) const override
    {
        out.push_back( ProvenanceDimension{ "operator", true, "rs:calibrate" } );
        return Availability::Found;
    }
    Availability record( const std::string &, DigestRecord &out, std::string & ) const override
    {
        out.algorithm = "sha256";
        out.digest = std::string( 64, '0' );
        return Availability::Found;
    }
};

VerificationInputs omniscientInputs( const OmniscientProvider &provider )
{
    VerificationInputs inputs;
    inputs.state = &provider;
    inputs.artifact = &provider;
    inputs.metric = &provider;
    inputs.provenance = &provider;
    inputs.digest = &provider;
    inputs.sourceId = "r18e-fixture";
    return inputs;
}

VerificationCheck numericCheck( const std::string &id, double lower, double upper )
{
    VerificationCheck check;
    check.id = id;
    check.kind = checkKindToWire( CheckKind::NumericRange );
    check.subject = SubjectRef{ "metric", "mean" };
    check.params["expect"]["lower"] = lower;
    check.params["expect"]["upper"] = upper;
    return check;
}

bool hasFailureCode( const VerificationReport &report, const std::string &code )
{
    return std::find( report.outcome.failureCodes.begin(), report.outcome.failureCodes.end(),
                      code )
           != report.outcome.failureCodes.end();
}

} // namespace

// ---------------------------------------------------------------------------
// V2 — duplicate check ids never roll up as pass
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V2: [x(Pass), x(Fail)] never rolls up as pass and names the defect",
           "[verifier][duplicate-ids][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    VerificationSpec spec;
    spec.specId = "r18e.duplicate.first-pass";
    spec.checks.push_back( numericCheck( "dup.mean", 0.0, 1.0 ) ); // evaluates Pass
    spec.checks.push_back( numericCheck( "dup.mean", 2.0, 3.0 ) ); // evaluates Fail

    const VerificationReport report = runSpecFlat( spec, inputs );

    // The masked-fail headline: with first-match roll-up this used to be Pass.
    CHECK( report.status != CheckStatus::Pass );
    // The outcome says WHY the spec could not be trusted.
    CHECK( hasFailureCode( report, failure_codes::kSpecInvalid ) );
    // An ambiguous spec evaluates NOTHING as a verdict: both duplicate
    // results are published as Indeterminate (visible, never vanished) and
    // the outcome names SPEC_INVALID — the audit trail says "refused",
    // which is the honest count for a spec that cannot be trusted.
    const Json::Value indetCount = report.counters["by_status"]["indeterminate"];
    REQUIRE( indetCount.isIntegral() );
    CHECK( indetCount.asInt() == 2 );
}

TEST_CASE( "r18e V2b: [x(Fail), x(Pass)] also refuses, worst-wins either way",
           "[verifier][duplicate-ids][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    VerificationSpec spec;
    spec.specId = "r18e.duplicate.first-fail";
    spec.checks.push_back( numericCheck( "dup.mean", 2.0, 3.0 ) ); // evaluates Fail
    spec.checks.push_back( numericCheck( "dup.mean", 0.0, 1.0 ) ); // evaluates Pass

    const VerificationReport report = runSpecFlat( spec, inputs );

    CHECK( report.status != CheckStatus::Pass );
    CHECK( hasFailureCode( report, failure_codes::kSpecInvalid ) );
}

TEST_CASE( "r18e V2c: the roll-up combines all results for an id worst-wins",
           "[verifier][duplicate-ids][r18_e]" )
{
    // Defensive layer: even without the runner's duplicate rejection, a
    // hand-assembled result set rolls a duplicated id up worst-wins.
    std::vector<CheckResult> results;
    CheckResult pass;
    pass.checkId = "dup.mean";
    pass.kind = "numeric_range";
    pass.status = CheckStatus::Pass;
    results.push_back( pass );
    CheckResult fail;
    fail.checkId = "dup.mean";
    fail.kind = "numeric_range";
    fail.status = CheckStatus::Fail;
    results.push_back( fail );

    const NodeOutcome node = rollUpNode( "node", { "dup.mean" }, results );
    CHECK( node.status == CheckStatus::Fail );
}

TEST_CASE( "r18e V2d: unique ids keep their exact verdicts",
           "[verifier][duplicate-ids][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    VerificationSpec spec;
    spec.specId = "r18e.unique";
    spec.checks.push_back( numericCheck( "mean.in_band", 0.0, 1.0 ) );
    spec.checks.push_back( numericCheck( "mean.out_of_band", 2.0, 3.0 ) );

    const VerificationReport report = runSpecFlat( spec, inputs );
    CHECK( report.status == CheckStatus::Fail );
    CHECK_FALSE( hasFailureCode( report, failure_codes::kSpecInvalid ) );
    CHECK( report.counters["by_status"]["pass"].asInt() == 1 );
    CHECK( report.counters["by_status"]["fail"].asInt() == 1 );
}

// ---------------------------------------------------------------------------
// V4 — derived contract checks can never read as Pass on unread vocabulary
// ---------------------------------------------------------------------------

#include "verification/pack.h"
#include "verification/packs_builtin.h"

TEST_CASE( "r18e V4: derived contract checks never evaluate to Pass on vacuous params",
           "[verifier][packs][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    const DerivedPack derived = scientificContractFor( "rs:threshold_raster" );
    REQUIRE_FALSE( derived.indeterminate );
    REQUIRE( derived.pack.checks.size() == 4 );

    // Route the pack's checks through the runner exactly like a composer.
    VerificationSpec spec;
    spec.specId = "r18e.contract";
    for ( const VerificationCheck &check : derived.pack.checks )
        spec.checks.push_back( check );

    const VerificationReport report = runSpecFlat( spec, inputs );
    REQUIRE( report.results.size() == 4 );
    for ( const CheckResult &result : report.results )
    {
        // Under the omniscient provider the two state-shaped checks used to
        // evaluate to Pass with NO expectation ever declared — a vacuous
        // pass. Every contract check must either conclude from a real
        // expectation or refuse visibly.
        if ( result.status == CheckStatus::Pass )
        {
            // A pass is only legitimate when the check actually read an
            // expectation from its params; the derived checks that carry
            // registry dimensions the evaluators cannot read must not land
            // here. The ids that may legitimately conclude will declare
            // their expectation in evidence.expected.
            bool declaredExpectation = result.evidence.expected.isObject()
                                       && !result.evidence.expected.empty();
            CHECK( declaredExpectation );
        }
        else
        {
            CHECK_FALSE( result.failureCode.empty() );
        }
    }
}

// ---------------------------------------------------------------------------
// V7 — a tautological relational operand pair never equals-passes
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V7: two defaulting metric operands comparing a metric to itself is refused",
           "[verifier][relational][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    VerificationSpec spec;
    spec.specId = "r18e.relational.tautology";
    VerificationCheck check;
    check.id = "relation.self";
    check.kind = checkKindToWire( CheckKind::RelationalConsistency );
    check.subject = SubjectRef{ "metric", "mean" };
    check.params["relation"] = "equals";
    Json::Value operands{ Json::arrayValue };
    Json::Value left{ Json::objectValue };
    left["source"] = "metric"; // no name — defaults to subject.id
    Json::Value right{ Json::objectValue };
    right["source"] = "metric"; // same default — the pair is the SAME metric
    operands.append( left );
    operands.append( right );
    check.params["operands"] = operands;
    check.params["expect"]["value"] = 0.0;
    check.params["expect"]["tolerance"] = 0.1;
    spec.checks.push_back( check );

    const VerificationReport report = runSpecFlat( spec, inputs );
    REQUIRE( report.results.size() == 1 );
    // Today: 0.25 == 0.25 → unconditional Pass. The spec is tautological —
    // it must be refused as SPEC_INVALID instead.
    CHECK( report.results[0].status != CheckStatus::Pass );
    CHECK( report.results[0].failureCode == failure_codes::kSpecInvalid );
}

// ---------------------------------------------------------------------------
// V9 — specDigest stays order-independent for checks tied on (id, kind, title)
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V9: tied checks with different params keep one digest under permutation",
           "[verifier][digest][r18_e]" )
{
    auto tiedCheck = []( int which ) {
        VerificationCheck check;
        check.id = "tied.mean";
        check.kind = checkKindToWire( CheckKind::NumericRange );
        check.title = "the tied expectation";
        check.subject = SubjectRef{ "metric", "mean" };
        check.params["expect"]["lower"] = which == 0 ? 0.0 : 1.0;
        check.params["expect"]["upper"] = which == 0 ? 0.5 : 1.5;
        return check;
    };

    VerificationSpec a;
    a.specId = "r18e.tied";
    a.checks.push_back( tiedCheck( 0 ) );
    a.checks.push_back( tiedCheck( 1 ) );

    VerificationSpec b;
    b.specId = "r18e.tied";
    b.checks.push_back( tiedCheck( 1 ) );
    b.checks.push_back( tiedCheck( 0 ) );

    CHECK( specDigest( a ) == specDigest( b ) );
}

// ---------------------------------------------------------------------------
// V5 — the teaching/agent view never disagrees with the engine's headline
// ---------------------------------------------------------------------------

#include "verification/render_teaching.h"

TEST_CASE( "r18e V5: an uncovered failing check keeps the rendered view non-pass",
           "[verifier][render][r18_e]" )
{
    OmniscientProvider provider;
    const VerificationInputs inputs = omniscientInputs( provider );

    // Two checks; the node map covers ONLY the passing one. buildReport
    // folds the uncovered Fail into report.status (report.cpp uncovered
    // fold) — the renderer used to recompute from nodes alone and report
    // "pass" while listing the failure code.
    VerificationSpec spec;
    spec.specId = "r18e.uncovered";
    spec.checks.push_back( numericCheck( "covered.mean", 0.0, 1.0 ) );  // Pass
    spec.checks.push_back( numericCheck( "orphan.mean", 2.0, 3.0 ) );   // Fail

    NodeCheckMap nodeChecks;
    nodeChecks["node"] = { "covered.mean" }; // orphan.mean belongs to no node

    const VerificationReport report = runSpec( spec, inputs, nodeChecks );
    REQUIRE( report.status == CheckStatus::Fail );

    CHECK( derivedStatus( report ) != CheckStatus::Pass );
    const TeachingView teaching = renderTeaching( report );
    CHECK( teaching.verdict != kTeachingVerdictTrustworthy );
}
