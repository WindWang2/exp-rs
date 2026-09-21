/***************************************************************************
  test_verifier_adversarial_14.cpp — Slice G: the fail-open battery

  Every other lane in this module tests that the verifier produces the RIGHT
  verdict. This file tests something narrower and more important: that the
  verifier cannot be made to say "Pass" when it has not established anything.

  The adversary is not a malicious user; it is a tired caller. The dangerous
  inputs are the ones that look like a successful verification to whatever code
  reads the verdict next:

    - a spec with no checks              ("no expectations" read as "nothing wrong")
    - a check kind this build never heard of
    - a provider that answers Missing or Refused
    - evidence that is a sample presented without its frame
    - a budget exhausted partway through
    - an empty report rendered for a human
    - a node that is Indeterminate under a task a summary calls "pass"

  Each case asserts one invariant from a different angle:

      INDETERMINATE IS NEVER ABSORBED INTO PASS.

  A single Pass escaping any of these is a defect in the module's core promise,
  and this file exists so that such a regression cannot pass unnoticed.
 ***************************************************************************/

#include "verification/check_runner.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/pack.h"
#include "verification/providers.h"
#include "verification/render_agent.h"
#include "verification/render_teaching.h"
#include "verification/report.h"
#include "verification/spec.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::verification;

namespace
{

bool contains( const std::vector<std::string> &haystack, const std::string &needle )
{
    for ( const std::string &entry : haystack )
    {
        if ( entry == needle )
        {
            return true;
        }
    }
    return false;
}

VerificationCheck makeCheck( const std::string &id, const std::string &kind )
{
    VerificationCheck check;
    check.id = id;
    check.kind = kind;   // wire string: the vocabulary is closed but versioned
    check.title = "adversarial " + id;
    check.subject = SubjectRef{ "task", "adv" };
    check.params = Json::Value( Json::objectValue );
    return check;
}

/// A provider bundle that answers every question the same way.
///
/// Most adversarial cases ask "what happens when nothing can be observed?", so
/// collapsing the five providers into one answer keeps the setup honest: the
/// test is about the verifier's response to absence, not about wiring.
class UniformProvider : public StateProvider,
                        public ArtifactProvider,
                        public MetricProvider,
                        public ProvenanceProvider,
                        public DigestProvider
{
  public:
    explicit UniformProvider( Availability answer ) : m_answer( answer ) {}

    Availability tryGet( const std::string &, StateSnapshot &, std::string & ) const override
    {
        return m_answer;
    }

    Availability describe( const std::string &, Json::Value &, std::string & ) const override
    {
        return m_answer;
    }

    Availability metric( const std::string &, MetricValue &, std::string & ) const override
    {
        return m_answer;
    }

    Availability completeness( const std::string &, std::vector<ProvenanceDimension> &,
                               std::string & ) const override
    {
        return m_answer;
    }

    Availability record( const std::string &, DigestRecord &, std::string & ) const override
    {
        return m_answer;
    }

  private:
    Availability m_answer;
};

/// Builds a wired input bundle pointing at @p provider. The provider must
/// outlive the inputs; callers keep it in a local shared_ptr.
VerificationInputs inputsWith( const UniformProvider &provider )
{
    VerificationInputs inputs;
    inputs.state = &provider;
    inputs.artifact = &provider;
    inputs.metric = &provider;
    inputs.provenance = &provider;
    inputs.digest = &provider;
    inputs.sourceId = "adversarial-fixture";
    return inputs;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Absence of expectations
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-1: a spec declaring zero checks is never a pass",
           "[verifier14][adversarial][failopen]" )
{
    // The most dangerous input in the module. A caller that builds a spec by
    // filtering ("keep the checks that apply to this product") can easily end
    // up with none, and an empty spec that rolls up to Pass is a rubber stamp:
    // it certifies a result nobody checked.
    VerificationSpec spec;
    spec.specId = "empty";
    spec.specVersion = "1";
    REQUIRE( spec.checks.empty() );

    const UniformProvider provider( Availability::Found );
    const VerificationReport report = runSpec( spec, inputsWith( provider ) );

    REQUIRE( report.results.empty() );
    REQUIRE( report.outcome.status == CheckStatus::Indeterminate );
    REQUIRE( report.outcome.status != CheckStatus::Pass );
    REQUIRE( contains( report.outcome.failureCodes, failure_codes::kNoChecks ) );
}

// ---------------------------------------------------------------------------
// 2. Vocabulary the build does not know
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-2: an unknown check kind is Indeterminate, never a silent skip",
           "[verifier14][adversarial][failopen]" )
{
    // Silently ignoring a check whose kind this build cannot evaluate is
    // indistinguishable from that check having passed -- until someone asks why
    // a result was certified while a declared expectation was never examined.
    VerificationSpec spec;
    spec.specId = "unknown-kind";
    spec.specVersion = "1";
    spec.checks.push_back( makeCheck( "c1", "a_kind_from_a_future_version" ) );

    const UniformProvider provider( Availability::Found );
    const VerificationReport report = runSpec( spec, inputsWith( provider ) );

    REQUIRE( report.results.size() == 1 );
    REQUIRE( report.results.front().status == CheckStatus::Indeterminate );
    REQUIRE( report.results.front().failureCode == failure_codes::kUnsupportedCheckKind );
    REQUIRE( report.outcome.status == CheckStatus::Indeterminate );
}

// ---------------------------------------------------------------------------
// 3. Providers that cannot answer
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-3: every provider refusing still cannot produce a pass",
           "[verifier14][adversarial][failopen]" )
{
    VerificationSpec spec;
    spec.specId = "all-refused";
    spec.specVersion = "1";
    spec.checks.push_back( makeCheck( "state", checkKindToWire( CheckKind::StateInvariant ) ) );
    spec.checks.push_back( makeCheck( "artifact", checkKindToWire( CheckKind::ArtifactShape ) ) );
    spec.checks.push_back( makeCheck( "numeric", checkKindToWire( CheckKind::NumericRange ) ) );

    const UniformProvider provider( Availability::Refused );
    const VerificationReport report = runSpec( spec, inputsWith( provider ) );

    REQUIRE( report.results.size() == 3 );
    for ( const CheckResult &result : report.results )
    {
        INFO( "check: " << result.checkId << " code: " << result.failureCode );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE_FALSE( result.failureCode.empty() );
    }
    REQUIRE( report.outcome.status == CheckStatus::Indeterminate );
}

TEST_CASE( "adversarial G-4: no provider at all is Indeterminate, not void-as-success",
           "[verifier14][adversarial][failopen]" )
{
    // A caller who forgets to wire a provider hands us an empty
    // VerificationInputs. Unset members must read as Missing, never as "the fact
    // held" -- an unwired verifier that passes is worse than one that crashes.
    VerificationSpec spec;
    spec.specId = "no-providers";
    spec.specVersion = "1";
    spec.checks.push_back( makeCheck( "state", checkKindToWire( CheckKind::StateInvariant ) ) );

    const VerificationReport report = runSpec( spec, VerificationInputs{} );

    REQUIRE( report.results.size() == 1 );
    REQUIRE( report.results.front().status == CheckStatus::Indeterminate );
    REQUIRE( report.outcome.status == CheckStatus::Indeterminate );
}

TEST_CASE( "adversarial G-5: a provider answering Missing is not an empty observation",
           "[verifier14][adversarial][failopen]" )
{
    // Missing and "present but empty" are the same shape and must not be the
    // same verdict. A state snapshot with no tokens is not evidence that no
    // invariant was violated; it is evidence of nothing.
    VerificationSpec spec;
    spec.specId = "all-missing";
    spec.specVersion = "1";
    spec.checks.push_back( makeCheck( "state", checkKindToWire( CheckKind::StateInvariant ) ) );

    const UniformProvider provider( Availability::Missing );
    const VerificationReport report = runSpec( spec, inputsWith( provider ) );

    REQUIRE( report.results.size() == 1 );
    REQUIRE( report.results.front().status == CheckStatus::Indeterminate );
    REQUIRE( report.outcome.status != CheckStatus::Pass );
}

// ---------------------------------------------------------------------------
// 4. Evidence that overstates itself
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-6: a sample without its frame cannot support a verdict",
           "[verifier14][adversarial][failopen]" )
{
    // "1.2% NoData" over an unstated population is not a measurement, it is a
    // number. Accepting it would let a check that examined three pixels certify
    // a scene of 10^8.
    VerificationEvidence sampled;
    sampled.coverage = EvidenceCoverage::Sampled;
    sampled.sourceId = "adv";
    sampled.details = Json::Value( Json::objectValue );   // deliberately no frame

    const std::vector<std::string> problems = evidenceCompleteness( sampled );
    REQUIRE_FALSE( problems.empty() );

    VerificationEvidence framed = sampled;
    framed.details["sample_size"] = 1000;
    framed.details["population"] = 100000000;
    REQUIRE( evidenceCompleteness( framed ).empty() );
}

TEST_CASE( "adversarial G-7: unavailable evidence that still declares an expectation is refused",
           "[verifier14][adversarial][failopen]" )
{
    // Internal contradiction: the record says both "I could not observe
    // anything" and "here is what I expected to observe". One of those is a
    // lie, and a verdict built on it inherits the lie.
    VerificationEvidence evidence;
    evidence.coverage = EvidenceCoverage::Unavailable;
    evidence.sourceId = "adv";
    evidence.expected["value"] = 42;

    REQUIRE_FALSE( evidenceCompleteness( evidence ).empty() );
}

// ---------------------------------------------------------------------------
// 5. Resource exhaustion
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-8: exhausting the check budget is never a truncated pass",
           "[verifier14][adversarial][failopen]" )
{
    // Truncation is the subtle fail-open: run the first N checks, drop the
    // rest, report on what ran. The dropped checks are exactly the ones that
    // might have failed.
    VerificationSpec spec;
    spec.specId = "over-budget";
    spec.specVersion = "1";
    spec.budget.maxChecks = 2;

    for ( int i = 0; i < 5; ++i )
    {
        spec.checks.push_back(
            makeCheck( "c" + std::to_string( i ), checkKindToWire( CheckKind::StateInvariant ) ) );
    }

    const UniformProvider provider( Availability::Found );
    const VerificationReport report = runSpec( spec, inputsWith( provider ) );

    // Either the run refuses up front, or the unevaluated checks are reported
    // as Indeterminate. What must NOT happen is a Pass covering only the first
    // two checks while three were silently dropped.
    REQUIRE( report.outcome.status != CheckStatus::Pass );
}

// ---------------------------------------------------------------------------
// 6. Human-facing surfaces
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-9: an empty report never renders as trustworthy",
           "[verifier14][adversarial][failopen]" )
{
    VerificationReport empty;   // no results, no nodes

    const TeachingView teaching = renderTeaching( empty );
    REQUIRE( teaching.verdict == kTeachingVerdictUnverified );
    REQUIRE( teaching.verdict != kTeachingVerdictTrustworthy );
    REQUIRE_FALSE( teaching.reasons.empty() );   // a verdict with no reason is an order

    const AgentView agent = renderAgent( empty );
    REQUIRE( agent.status == statusToWire( CheckStatus::Indeterminate ) );
    REQUIRE( agent.status != statusToWire( CheckStatus::Pass ) );
}

TEST_CASE( "adversarial G-10: a summary claiming pass over an Indeterminate node is not believed",
           "[verifier14][adversarial][failopen]" )
{
    // Level 2 must win over a flat, optimistic reading. This is the case where
    // a caller (or a careless serialiser) hands over a report whose headline
    // says Pass while a node is still unknown.
    VerificationReport report;

    NodeOutcome unknownNode;
    unknownNode.nodeId = "n1";
    unknownNode.status = CheckStatus::Indeterminate;
    report.nodes.push_back( unknownNode );

    // The lie: the flat field claims success.
    report.status = CheckStatus::Pass;

    const TeachingView teaching = renderTeaching( report );
    REQUIRE( teaching.verdict == kTeachingVerdictUnverified );
    REQUIRE( teaching.verdict != kTeachingVerdictTrustworthy );

    const AgentView agent = renderAgent( report );
    REQUIRE( agent.status == statusToWire( CheckStatus::Indeterminate ) );
}

// ---------------------------------------------------------------------------
// 7. Composition
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-11: composing packs that contribute nothing is no-checks, not a pass",
           "[verifier14][adversarial][failopen]" )
{
    // Three packs each declaring nothing is still nothing. A composition layer
    // returning an empty-but-ok result hands the caller a spec that will roll
    // up to Pass over zero checks.
    std::vector<VerifierPack> packs;
    for ( int i = 0; i < 3; ++i )
    {
        VerifierPack pack;
        pack.id = "empty" + std::to_string( i );
        packs.push_back( pack );
    }

    for ( const PackConflictPolicy policy :
          { PackConflictPolicy::Reject, PackConflictPolicy::Override } )
    {
        const ComposeOutcome outcome = composePacks( packs, policy );
        REQUIRE_FALSE( outcome.ok );
        REQUIRE( outcome.failureCode == failure_codes::kNoChecks );
        REQUIRE( outcome.checks.empty() );
    }
}

// ---------------------------------------------------------------------------
// 8. The lattice itself — exhaustive over the three-valued domain
// ---------------------------------------------------------------------------

TEST_CASE( "adversarial G-12: no combination of Indeterminate with anything yields Pass",
           "[verifier14][adversarial][failopen]" )
{
    const CheckStatus all[] = { CheckStatus::Pass, CheckStatus::Fail, CheckStatus::Indeterminate };

    for ( const CheckStatus left : all )
    {
        for ( const CheckStatus right : all )
        {
            const CheckStatus combined = combineStatus( left, right );
            if ( left == CheckStatus::Indeterminate || right == CheckStatus::Indeterminate )
            {
                INFO( "combining " << statusToWire( left ) << " and " << statusToWire( right ) );
                REQUIRE( combined != CheckStatus::Pass );
            }
        }
    }

    // No evidence at all is the empty-set case, and it is the one a "helpful"
    // implementation gets wrong: an empty fold seeded with Pass is a rubber
    // stamp that never even had to read a check.
    REQUIRE( combineAll( {} ) == CheckStatus::Indeterminate );
}

TEST_CASE( "adversarial G-13: a node set that is entirely unrun never rolls up to Pass",
           "[verifier14][adversarial][failopen]" )
{
    // A node whose checks never ran has no postcondition verdict, so the task
    // has no basis for success. This guards the level-1 -> level-2 seam, which
    // is where an optimistic implementation would be most tempted to default.
    const NodeOutcome node =
        rollUpNode( "n1", { "never-ran" }, std::vector<CheckResult>{}, IndeterminatePolicy::Keep );

    REQUIRE( node.status == CheckStatus::Indeterminate );
    REQUIRE( node.status != CheckStatus::Pass );

    const TaskOutcome task = rollUpTask( { node } );
    REQUIRE( task.status == CheckStatus::Indeterminate );
    REQUIRE( task.status != CheckStatus::Pass );
    REQUIRE( contains( task.blockingNodes, "n1" ) );
}
