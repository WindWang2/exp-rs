// test_r33_t04_verify_roundtrip.cpp — R33-T04 / WP07 (#2115):
//
//   The engine's state.invariant / relational.consistency evaluators record
//   the spec's expectation/relations ARRAY as evidence 'expected' (and
//   cross.output.consistency an ARRAY 'observed'), but
//   VerificationEvidence::fromCanonicalJson only accepted objects — every
//   such report failed its own serialize→parse round-trip. These cases pin
//   the closed unified structure (object-or-array for observed/expected)
//   with a REAL evaluate() → canonical JSON → parse → re-canonicalize →
//   digest chain:
//
//   * state fail/pass and relational pass evidence round-trip bit-exactly
//     (source/observed/expected/sampling, status, code, message, counts);
//   * the re-serialized canonical text is byte-stable and digest-stable;
//   * scalars/nulls for observed/expected stay rejected (closed structure).
#include <catch2/catch_test_macros.hpp>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_types.h"

#include <json/json.h>

#include <map>
#include <optional>
#include <string>

using namespace sicnu::verify;

namespace
{

class FakeStateView : public IStateView
{
  public:
    std::map<std::string, Json::Value> values;
    std::optional<Json::Value> state( const std::string &key ) override
    {
        const auto found = values.find( key );
        if ( found == values.end() )
            return std::nullopt;
        return found->second;
    }
};

class FakeMetricView : public IMetricView
{
  public:
    std::map<std::string, double> values;
    std::optional<double> metric( const std::string &name ) override
    {
        const auto found = values.find( name );
        if ( found == values.end() )
            return std::nullopt;
        return found->second;
    }
};

Json::Value stateExpectations( bool withFailure )
{
    Json::Value expectations( Json::arrayValue );
    Json::Value ok;
    ok["key"] = "stage";
    ok["op"] = "present";
    expectations.append( ok );
    if ( withFailure )
    {
        Json::Value bad;
        bad["key"] = "ghost";
        bad["op"] = "present";
        expectations.append( bad );
    }
    return expectations;
}
} // namespace

TEST_CASE( "state and relational evidence round-trips through the canonical report",
           "[verify][r33t04]" )
{
    FakeStateView state;
    state.values["stage"] = "complete";
    FakeMetricView metrics;
    metrics.values["iou"] = 0.875;

    VerificationSpec spec;
    spec.specId = "spec_r33t04_roundtrip";
    spec.scope = "task";

    VerificationCheckSpec failingState;
    failingState.checkId = "state_fail";
    failingState.kind = "state.invariant";
    failingState.params["expectations"] = stateExpectations( /*withFailure=*/true );

    VerificationCheckSpec passingState;
    passingState.checkId = "state_pass";
    passingState.kind = "state.invariant";
    passingState.params["expectations"] = stateExpectations( /*withFailure=*/false );

    VerificationCheckSpec relational;
    relational.checkId = "rel_pass";
    relational.kind = "relational.consistency";
    Json::Value relation( Json::objectValue );
    relation["op"] = "eq";
    relation["left"] = "iou";
    relation["right"] = 0.875;
    Json::Value relations( Json::arrayValue );
    relations.append( relation );
    relational.params["relations"] = relations;

    spec.checks = { failingState, passingState, relational };

    VerificationContext context;
    context.stateView = &state;
    context.metricView = &metrics;

    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.overall == VerificationStatus::Fail ); // failingState folds the report
    REQUIRE( report.checks.size() == 3 );
    REQUIRE( report.checks[0].status == VerificationStatus::Fail );
    REQUIRE( report.checks[0].code == kCodeStateViolated );
    REQUIRE( report.checks[1].status == VerificationStatus::Pass );
    REQUIRE( report.checks[2].status == VerificationStatus::Pass );
    REQUIRE( report.checks[0].evidence );
    // The array-evidence shapes #2115 is about.
    REQUIRE( report.checks[0].evidence->expected.isArray() );
    REQUIRE( report.checks[2].evidence->expected.isArray() );

    const Json::Value canonical = report.toCanonicalJson();
    const std::string digest = report.digest();
    REQUIRE( !digest.empty() );

    // Serialize → parse with digest re-verification: previously this failed
    // with "'observed'/'expected' must be objects" for the failing state
    // check and never reached the digest check at all.
    VerificationReport parsed;
    std::string error;
    REQUIRE( VerificationReport::fromCanonicalJson( canonical, parsed, error, digest ) );
    REQUIRE( error.empty() );

    REQUIRE( parsed.overall == report.overall );
    REQUIRE( parsed.checks.size() == report.checks.size() );
    for ( std::size_t i = 0; i < report.checks.size(); ++i )
    {
        REQUIRE( parsed.checks[i].checkId == report.checks[i].checkId );
        REQUIRE( parsed.checks[i].kind == report.checks[i].kind );
        REQUIRE( parsed.checks[i].status == report.checks[i].status );
        REQUIRE( parsed.checks[i].code == report.checks[i].code );
        REQUIRE( parsed.checks[i].message == report.checks[i].message );
        REQUIRE( ( parsed.checks[i].evidence.has_value() == report.checks[i].evidence.has_value() ) );
        if ( report.checks[i].evidence )
        {
            REQUIRE( parsed.checks[i].evidence->source == report.checks[i].evidence->source );
            REQUIRE( parsed.checks[i].evidence->observed == report.checks[i].evidence->observed );
            REQUIRE( parsed.checks[i].evidence->expected == report.checks[i].evidence->expected );
            REQUIRE( parsed.checks[i].evidence->hasSampling == report.checks[i].evidence->hasSampling );
            REQUIRE( parsed.checks[i].evidence->sampledPoints
                     == report.checks[i].evidence->sampledPoints );
        }
    }

    // Re-canonicalize: byte- and digest-stable (the parsed report is the
    // same evidence, not a lossy approximation).
    REQUIRE( parsed.toCanonicalJson() == canonical );
    REQUIRE( parsed.digest() == digest );
}

TEST_CASE( "evidence observed/expected stay closed: scalars and nulls are rejected",
           "[verify][r33t04]" )
{
    Json::Value evidence( Json::objectValue );
    evidence["source"] = "state";
    evidence["observed"] = Json::Value( Json::objectValue );
    evidence["expected"] = Json::Value( Json::arrayValue );

    VerificationEvidence parsed;
    std::string error;

    SECTION( "arrays and objects together parse" )
    {
        REQUIRE( parsed.fromCanonicalJson( evidence, error ) );
        CHECK( parsed.expected.isArray() );
    }

    SECTION( "a scalar observed is rejected" )
    {
        evidence["observed"] = 3;
        CHECK_FALSE( parsed.fromCanonicalJson( evidence, error ) );
        CHECK( !error.empty() );
    }

    SECTION( "a null expected is rejected" )
    {
        evidence["expected"] = Json::Value();
        CHECK_FALSE( parsed.fromCanonicalJson( evidence, error ) );
        CHECK( !error.empty() );
    }

    SECTION( "a string expected is rejected" )
    {
        evidence["expected"] = "expectations";
        CHECK_FALSE( parsed.fromCanonicalJson( evidence, error ) );
        CHECK( !error.empty() );
    }
}
