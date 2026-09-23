// test_preflight_engine.cpp — RS14-02 slice B: PreflightEngine contract.
//
// The engine is rule-agnostic: these tests use scripted stub rules so the
// registry/evaluation/ack/budget contracts are pinned independently of the
// builtin rule set. Contract (PR #1207 slice-B design, verified missing on
// master e4904cd3c):
//   duplicate rule id rejected; evaluation in sorted-id order independent
//   of registration order; rules_revision = shortDigest of sorted "id@rev";
//   deterministic budgets with explicit typed truncation; ack applies ONLY
//   to require_ack and can never flip a block.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rule.h"
#include "preflight/sha256.h"

#include <map>
#include <utility>
#include <vector>

using namespace sicnu::preflight;

namespace {

/// Scripted rule: emits a canned finding (or none) so engine behavior is
/// observable without any builtin rule.
class StubRule : public IPreflightRule
{
  public:
    StubRule( std::string id, int revision, std::vector<PreflightFinding> findings,
              std::string outcome = "", std::string detail = "" )
        : id_( std::move( id ) ), revision_( revision ),
          findings_( std::move( findings ) ), outcome_( std::move( outcome ) ),
          detail_( std::move( detail ) )
    {
    }

    std::string id() const override { return id_; }
    int revision() const override { return revision_; }

    RuleResult evaluate( const PreflightRequest &, const RuleFacts & ) const override
    {
        RuleResult r;
        r.findings = findings_;
        r.outcome = outcome_;
        r.detail = detail_;
        return r;
    }

  private:
    std::string id_;
    int revision_;
    std::vector<PreflightFinding> findings_;
    std::string outcome_;
    std::string detail_;
};

PreflightFinding makeFinding( const std::string &code, PreflightSeverity severity,
                              const std::string &subject = "primary" )
{
    PreflightFinding f;
    f.code = code;
    f.severity = severity;
    f.subject = subject;
    f.domain = "test";
    f.basis = "observed";
    return f;
}

PreflightRequest baseRequest( std::vector<std::pair<std::string, std::string>> inputs = {
                                  { "primary", "asset-a" } } )
{
    PreflightRequest req;
    req.operatorId = "rs:demo";
    req.mode = "teaching";
    req.humanOperatorId = "student-7";
    req.inputs = std::move( inputs );
    return req;
}

struct SimpleProvider : IAssetFactsProvider
{
    mutable int calls = 0;
    SlotFactsResult slotFacts( const std::string & ) const override
    {
        ++calls;
        SlotFactsResult r;
        r.status = FactStatus::Unknown;
        r.detail = "stub";
        return r;
    }
};

} // namespace

TEST_CASE( "engine registry rejects duplicate rule ids", "[preflight][engine]" )
{
    PreflightEngine engine;
    auto first = std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} );
    auto duplicate = std::make_unique<StubRule>( "preflight.a", 2, std::vector<PreflightFinding>{} );

    REQUIRE( engine.registerRule( std::move( first ) ) == RegistrationResult::Ok );
    REQUIRE( engine.registerRule( std::move( duplicate ) ) == RegistrationResult::DuplicateId );
    REQUIRE( engine.ruleCount() == 1 );
    REQUIRE( engine.registrationError().find( "preflight.a" ) != std::string::npos );
}

TEST_CASE( "engine registry keeps rules sorted by id", "[preflight][engine]" )
{
    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.c", 1, std::vector<PreflightFinding>{} ) );
    engine.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );
    engine.registerRule( std::make_unique<StubRule>( "preflight.b", 1, std::vector<PreflightFinding>{} ) );

    const auto ids = engine.ruleIds();
    REQUIRE( ids.size() == 3 );
    REQUIRE( ids[0] == "preflight.a" );
    REQUIRE( ids[1] == "preflight.b" );
    REQUIRE( ids[2] == "preflight.c" );
}

TEST_CASE( "engine evaluation order follows sorted ids, not registration order",
           "[preflight][engine]" )
{
    PreflightEngine engine;
    engine.registerRule(
        std::make_unique<StubRule>( "preflight.zeta", 1, std::vector<PreflightFinding>{} ) );
    engine.registerRule(
        std::make_unique<StubRule>( "preflight.alpha", 1, std::vector<PreflightFinding>{} ) );

    const PreflightReport report = engine.evaluate( baseRequest(), SimpleProvider{}, MemoryCapabilityProvider{} );
    REQUIRE( report.evaluated.size() == 2 );
    REQUIRE( report.evaluated[0].ruleId == "preflight.alpha" );
    REQUIRE( report.evaluated[1].ruleId == "preflight.zeta" );
}

TEST_CASE( "rules_revision is the digest of the sorted id@revision set",
           "[preflight][engine]" )
{
    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.b", 2, std::vector<PreflightFinding>{} ) );
    engine.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );

    const PreflightReport report = engine.evaluate( baseRequest(), SimpleProvider{}, MemoryCapabilityProvider{} );
    const std::string expected = shortDigest( "preflight.a@1\npreflight.b@2" );
    REQUIRE( report.rulesRevision == expected );

    // A different rule set yields a different revision.
    PreflightEngine other;
    other.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );
    const PreflightReport otherReport = other.evaluate( baseRequest(), SimpleProvider{}, MemoryCapabilityProvider{} );
    REQUIRE( otherReport.rulesRevision != report.rulesRevision );
}

TEST_CASE( "request digest is stable and sensitive to the evaluated request",
           "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );

    const PreflightReport one = engine.evaluate( baseRequest(), facts, capability );
    const PreflightReport again = engine.evaluate( baseRequest(), facts, capability );
    REQUIRE( one.requestDigest == again.requestDigest );
    REQUIRE( one.requestDigest.size() == 16 );

    PreflightRequest changed = baseRequest();
    changed.inputs = { { "primary", "asset-b" } };
    const PreflightReport different = engine.evaluate( changed, facts, capability );
    REQUIRE( different.requestDigest != one.requestDigest );
}

TEST_CASE( "verdict matrix: ok / requires_ack / blocked", "[preflight][engine]" )
{
    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.clean", 1, std::vector<PreflightFinding>{} ) );
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    REQUIRE( engine.evaluate( baseRequest(), facts, capability ).verdict == "ok" );

    PreflightEngine withAck;
    withAck.registerRule( std::make_unique<StubRule>(
        "preflight.risky", 1, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_RISK", PreflightSeverity::RequireAck ) } ) );
    REQUIRE( withAck.evaluate( baseRequest(), facts, capability ).verdict == "requires_ack" );

    PreflightEngine withBlock;
    withBlock.registerRule( std::make_unique<StubRule>(
        "preflight.wrong", 1, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_BLOCK", PreflightSeverity::Block ) } ) );
    REQUIRE( withBlock.evaluate( baseRequest(), facts, capability ).verdict == "blocked" );
}

TEST_CASE( "ack clears only require_ack findings and never flips a block",
           "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>(
        "preflight.risky", 1, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_RISK", PreflightSeverity::RequireAck ) } ) );

    PreflightRequest acked = baseRequest();
    acked.acknowledgements = { "SPF_DEMO_RISK" };
    const PreflightReport report = engine.evaluate( acked, facts, capability );
    REQUIRE( report.verdict == "ok" );
    REQUIRE( report.findings.size() == 1 );
    REQUIRE( report.findings[0].acknowledged == true );

    // The same code presented as a block can NOT be acknowledged away.
    PreflightEngine blockEngine;
    blockEngine.registerRule( std::make_unique<StubRule>(
        "preflight.wrong", 1, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_RISK", PreflightSeverity::Block ) } ) );
    const PreflightReport blocked = blockEngine.evaluate( acked, facts, capability );
    REQUIRE( blocked.verdict == "blocked" );
    REQUIRE( blocked.findings[0].acknowledged == false );
}

TEST_CASE( "engine recomputes acknowledgement from the request (single decision point)",
           "[preflight][engine]" )
{
    // A rule that lies about acknowledgement must not be able to smuggle an
    // acked block (or an unacked require_ack) past the engine.
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightFinding liar = makeFinding( "SPF_DEMO_RISK", PreflightSeverity::RequireAck );
    liar.acknowledged = true;
    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>(
        "preflight.liar", 1, std::vector<PreflightFinding>{ liar } ) );

    const PreflightReport unacked = engine.evaluate( baseRequest(), facts, capability );
    REQUIRE( unacked.findings[0].acknowledged == false );
    REQUIRE( unacked.verdict == "requires_ack" );

    PreflightRequest acked = baseRequest();
    acked.acknowledgements = { "SPF_DEMO_RISK" };
    const PreflightReport ok = engine.evaluate( acked, facts, capability );
    REQUIRE( ok.findings[0].acknowledged == true );
    REQUIRE( ok.verdict == "ok" );
}

TEST_CASE( "findings overflow is truncated deterministically and reported",
           "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    for ( int i = 0; i < 5; ++i )
    {
        std::string id = "preflight.r" + std::to_string( i );
        std::string code = "SPF_DEMO_" + std::to_string( i );
        engine.registerRule(
            std::make_unique<StubRule>( id, 1, std::vector<PreflightFinding>{ makeFinding( code, PreflightSeverity::Warn ) } ) );
    }

    PreflightRequest capped = baseRequest();
    capped.budgets.maxFindings = 3;
    const PreflightReport report = engine.evaluate( capped, facts, capability );

    REQUIRE( report.findings.size() == 3 );
    // The explicit truncation marker is present and typed.
    bool hasMarker = false;
    for ( const auto &f : report.findings )
        if ( f.code == "SPF_BUDGET_EXCEEDED" )
        {
            hasMarker = true;
            REQUIRE( f.severity == PreflightSeverity::RequireAck );
            REQUIRE( f.evidence["dropped_findings"].asInt() == 3 );
            REQUIRE( f.evidence["cap"].asInt() == 3 );
        }
    REQUIRE( hasMarker );
    // Truncation is loud: an over-budget report can never be "ok".
    REQUIRE( report.verdict == "requires_ack" );

    // Deterministic subset: same request, same surviving findings.
    const PreflightReport replay = engine.evaluate( capped, facts, capability );
    REQUIRE( canonicalReportJson( report ) == canonicalReportJson( replay ) );
}

TEST_CASE( "input overflow beyond max_inputs is skipped and reported",
           "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );

    PreflightRequest many = baseRequest();
    for ( int i = 0; i < 5; ++i )
        many.inputs.push_back( { "extra" + std::to_string( i ), "asset-" + std::to_string( i ) } );
    REQUIRE( many.inputs.size() == 6 );
    many.budgets.maxInputs = 2;

    const PreflightReport report = engine.evaluate( many, facts, capability );
    REQUIRE( facts.calls == 2 ); // bounded consultation
    bool hasMarker = false;
    for ( const auto &f : report.findings )
        if ( f.code == "SPF_BUDGET_EXCEEDED" )
        {
            hasMarker = true;
            REQUIRE( f.evidence["dropped_inputs"].asInt() == 4 );
        }
    REQUIRE( hasMarker );
    REQUIRE( report.verdict == "requires_ack" );
}

TEST_CASE( "registry full is a typed registration failure", "[preflight][engine]" )
{
    PreflightEngine engine;
    PreflightBudgets caps;
    caps.maxRules = 1;
    engine.setBudgets( caps );

    REQUIRE( engine.registerRule(
                 std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) ) ==
             RegistrationResult::Ok );
    REQUIRE( engine.registerRule( std::make_unique<StubRule>(
                 "preflight.b", 1, std::vector<PreflightFinding>{} ) ) == RegistrationResult::RegistryFull );
    REQUIRE( engine.ruleCount() == 1 );
}

TEST_CASE( "invalid requests fail closed with a typed finding", "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>( "preflight.a", 1, std::vector<PreflightFinding>{} ) );

    PreflightRequest noInputs = baseRequest();
    noInputs.inputs.clear();
    const PreflightReport empty = engine.evaluate( noInputs, facts, capability );
    REQUIRE( empty.verdict == "blocked" );
    bool invalid = false;
    for ( const auto &f : empty.findings )
        if ( f.code == "SPF_REQUEST_INVALID" )
            invalid = true;
    REQUIRE( invalid );

    PreflightRequest badMode = baseRequest();
    badMode.mode = "yolo";
    const PreflightReport mode = engine.evaluate( badMode, facts, capability );
    REQUIRE( mode.verdict == "blocked" );
}

TEST_CASE( "evaluation is byte-deterministic and round-trips", "[preflight][engine]" )
{
    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>(
        "preflight.b", 1, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_B", PreflightSeverity::Warn ) } ) );
    engine.registerRule( std::make_unique<StubRule>(
        "preflight.a", 3, std::vector<PreflightFinding>{ makeFinding( "SPF_DEMO_A", PreflightSeverity::RequireAck ) } ) );

    SimpleProvider facts;
    MemoryCapabilityProvider capability;
    const PreflightReport first = engine.evaluate( baseRequest(), facts, capability );
    const PreflightReport second = engine.evaluate( baseRequest(), facts, capability );

    REQUIRE( canonicalReportJson( first ) == canonicalReportJson( second ) );
    REQUIRE( reportDigest( first ) == reportDigest( second ) );

    const auto parsed = PreflightReport::fromJson( first.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->evaluated.size() == 2 );
    REQUIRE( parsed->findings.size() == 2 );
}

TEST_CASE( "rule traces expose insufficient_facts instead of silent passes",
           "[preflight][engine]" )
{
    SimpleProvider facts;
    MemoryCapabilityProvider capability;

    PreflightEngine engine;
    engine.registerRule( std::make_unique<StubRule>(
        "preflight.shy", 1, std::vector<PreflightFinding>{}, "insufficient_facts",
        "missing facts: radiometric_state" ) );

    const PreflightReport report = engine.evaluate( baseRequest(), facts, capability );
    REQUIRE( report.evaluated.size() == 1 );
    REQUIRE( report.evaluated[0].outcome == "insufficient_facts" );
    REQUIRE( report.evaluated[0].detail == "missing facts: radiometric_state" );
    // Trace-only insufficiency stays honest in the verdict path: nothing
    // was found, so the report is ok — but the trace says so per rule.
    REQUIRE( report.verdict == "ok" );
}
