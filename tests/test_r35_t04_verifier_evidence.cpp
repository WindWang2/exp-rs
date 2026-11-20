// test_r35_t04_verifier_evidence.cpp — R35-T04 Phase B verifier regressions:
// exact integer equality/ordering in the state evaluator (WP13 — doubles
// fold every integer above 2^53, so 9007199254740992 used to equal
// 9007199254740993) and spec-budget admission at the composition
// accumulation point (WP14 — an over-cap composition is refused before the
// remaining parts are copied and sorted).
#include <catch2/catch_test_macros.hpp>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_pack.h"
#include "verify/verify_types.h"

#include <json/json.h>

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

/// One state.invariant check evaluated against a single key/op/value.
VerificationStatus stateVerdict( const Json::Value &observed, const std::string &op,
                                 const Json::Value &pinned )
{
    FakeStateView state;
    state.values["k"] = observed;
    VerificationCheckSpec check;
    check.checkId = "c";
    check.kind = "state.invariant";
    Json::Value expectation( Json::objectValue );
    expectation["key"] = "k";
    expectation["op"] = op;
    expectation["value"] = pinned;
    Json::Value expectations( Json::arrayValue );
    expectations.append( expectation );
    check.params["expectations"] = expectations;

    VerificationSpec spec;
    spec.specId = "spec_r35t04";
    spec.scope = "task";
    spec.checks = { check };
    VerificationContext context;
    context.stateView = &state;
    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.checks.size() == 1 );
    return report.checks.front().status;
}

VerificationSpec oneCheckSpec( const std::string &id )
{
    VerificationSpec spec;
    spec.specId = id;
    spec.scope = "task";
    VerificationCheckSpec check;
    check.checkId = id + "_check";
    check.kind = "state.invariant";
    Json::Value expectation( Json::objectValue );
    expectation["key"] = "k";
    expectation["op"] = "present";
    Json::Value expectations( Json::arrayValue );
    expectations.append( expectation );
    check.params["expectations"] = expectations;
    spec.checks = { check };
    return spec;
}
} // namespace

// ── WP13: exact integer comparison in state eq/ne ──────────────────────────

TEST_CASE( "state eq does not fold integers above 2^53", "[verify][r35t04]" )
{
    constexpr std::uint64_t kFoldHi = 9007199254740993ull; // 2^53 + 1
    constexpr std::uint64_t kFoldLo = 9007199254740992ull; // 2^53 (exact in double)
    // Different integers that share a double image: NEVER equal.
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( kFoldHi ) ), "eq",
                           Json::Value( Json::UInt64( kFoldLo ) ) )
             == VerificationStatus::Fail );
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( kFoldLo ) ), "eq",
                           Json::Value( Json::UInt64( kFoldHi ) ) )
             == VerificationStatus::Fail );
    // …and their inequality is detected in BOTH directions.
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( kFoldHi ) ), "ne",
                           Json::Value( Json::UInt64( kFoldLo ) ) )
             == VerificationStatus::Pass );
    // Same integer: equal (both directions).
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( kFoldHi ) ), "eq",
                           Json::Value( Json::UInt64( kFoldHi ) ) )
             == VerificationStatus::Pass );
}

TEST_CASE( "state eq keeps the #1191 numeric-by-value ruling", "[verify][r35t04]" )
{
    // int 0 and real 0.0 are the same fact.
    REQUIRE( stateVerdict( Json::Value( 0 ), "eq", Json::Value( 0.0 ) )
             == VerificationStatus::Pass );
    REQUIRE( stateVerdict( Json::Value( 0.0 ), "eq", Json::Value( 0 ) )
             == VerificationStatus::Pass );
    // Whole reals within double precision still cross the int/real seam.
    REQUIRE( stateVerdict( Json::Value( Json::Int64( 42 ) ), "eq", Json::Value( 42.0 ) )
             == VerificationStatus::Pass );
    // An integer the double domain cannot carry is NOT equal to the double it
    // folds onto (9007199254740993 folds onto the double 2^53).
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( 9007199254740993ull ) ), "eq",
                           Json::Value( 9007199254740992.0 ) )
             == VerificationStatus::Fail );
    // Exact powers of two above 2^53 cross the seam honestly.
    REQUIRE( stateVerdict( Json::Value( Json::UInt64( 1ull << 54 ) ), "eq",
                           Json::Value( 18014398509481984.0 ) )
             == VerificationStatus::Pass );
}

TEST_CASE( "state eq handles the int64/uint64 domain edges exactly",
           "[verify][r35t04]" )
{
    const Json::Value uintMax( Json::UInt64( std::numeric_limits<std::uint64_t>::max() ) );
    const Json::Value intMin( Json::Int64( std::numeric_limits<std::int64_t>::min() ) );
    const Json::Value intMax( Json::Int64( std::numeric_limits<std::int64_t>::max() ) );
    REQUIRE( stateVerdict( uintMax, "eq",
                           Json::Value( Json::UInt64(
                               std::numeric_limits<std::uint64_t>::max() ) ) )
             == VerificationStatus::Pass );
    REQUIRE( stateVerdict( uintMax, "eq", Json::Value( 0 ) ) == VerificationStatus::Fail );
    REQUIRE( stateVerdict( intMin, "eq",
                           Json::Value( Json::Int64(
                               std::numeric_limits<std::int64_t>::min() ) ) )
             == VerificationStatus::Pass );
    // UINT64_MAX vs INT64_MAX: adjacent-ish, never equal, no fold.
    REQUIRE( stateVerdict( uintMax, "ne", intMax ) == VerificationStatus::Pass );
    // -1 (int) vs UINT64_MAX (uint): different values, not a sign-fold match.
    REQUIRE( stateVerdict( Json::Value( Json::Int64( -1 ) ), "eq", uintMax )
             == VerificationStatus::Fail );
}

TEST_CASE( "state eq recurses into nested evidence without folding",
           "[verify][r35t04]" )
{
    Json::Value observed( Json::arrayValue );
    observed.append( Json::Value( Json::UInt64( 9007199254740993ull ) ) );
    observed.append( Json::Value( 1 ) );
    Json::Value pinned( Json::arrayValue );
    pinned.append( Json::Value( Json::UInt64( 9007199254740992ull ) ) );
    pinned.append( Json::Value( 1 ) );
    REQUIRE( stateVerdict( observed, "eq", pinned ) == VerificationStatus::Fail );
    // Identical nested arrays (with big integers intact) stay equal.
    REQUIRE( stateVerdict( observed, "eq", observed ) == VerificationStatus::Pass );
}

// ── WP13: exact integer ordering in state gt/ge/lt/le ─────────────────────

TEST_CASE( "state ordering does not fold integers above 2^53", "[verify][r35t04]" )
{
    constexpr std::uint64_t kFoldHi = 9007199254740993ull;
    constexpr std::uint64_t kFoldLo = 9007199254740992ull;
    // Brace-init: parenthesized Json::Value( Json::UInt64( x ) ) at block
    // scope is a vexing parse (a function DECLARATION) and the resulting
    // function pointer silently converts to a jsoncpp BOOL.
    const Json::Value hi{ Json::UInt64( kFoldHi ) };
    const Json::Value lo{ Json::UInt64( kFoldLo ) };
    REQUIRE( stateVerdict( hi, "gt", lo ) == VerificationStatus::Pass );
    REQUIRE( stateVerdict( hi, "ge", lo ) == VerificationStatus::Pass );
    REQUIRE( stateVerdict( lo, "lt", hi ) == VerificationStatus::Pass );
    REQUIRE( stateVerdict( lo, "le", hi ) == VerificationStatus::Pass );
    REQUIRE( stateVerdict( lo, "ge", hi ) == VerificationStatus::Fail );
    REQUIRE( stateVerdict( hi, "lt", lo ) == VerificationStatus::Fail );
    // UINT64_MAX really is the top of the order.
    const Json::Value uintMax( Json::UInt64( std::numeric_limits<std::uint64_t>::max() ) );
    REQUIRE( stateVerdict( uintMax, "gt", hi ) == VerificationStatus::Pass );
    REQUIRE( stateVerdict( hi, "gt", uintMax ) == VerificationStatus::Fail );
    // Mixed int/real ordering still works through the double space.
    REQUIRE( stateVerdict( Json::Value( 2 ), "gt", Json::Value( 1.5 ) )
             == VerificationStatus::Pass );
}

// ── WP14: pack composition budget at the accumulation point ───────────────

TEST_CASE( "composePacks refuses an over-budget composition with a named source",
           "[verify][r35t04][pack]" )
{
    // One spec per part: crossing the budget at part kMax+1 must refuse
    // there, naming the spec and the pack that tipped it.
    std::vector<VerifierPack> parts;
    for ( std::size_t i = 0; i <= kMaxSpecsPerPack; ++i )
    {
        VerifierPack part;
        part.schemaVersion = 1;
        part.packId = "part_" + std::to_string( i );
        part.specs.push_back( oneCheckSpec( "spec_" + std::to_string( i ) ) );
        parts.push_back( part );
    }
    VerifierPack out;
    std::string error;
    REQUIRE_FALSE( composePacks( "composed", parts, out, error ) );
    REQUIRE( error.find( "too many specs" ) != std::string::npos );
    REQUIRE( error.find( "part_" + std::to_string( kMaxSpecsPerPack ) ) != std::string::npos );
    REQUIRE( error.find( "spec_" + std::to_string( kMaxSpecsPerPack ) ) != std::string::npos );
    // Nothing half-composed leaks out on refusal.
    REQUIRE( out.specs.empty() );
}

TEST_CASE( "composePacks keeps legal composition, stable order and conflict naming",
           "[verify][r35t04][pack]" )
{
    std::vector<VerifierPack> parts;
    {
        VerifierPack a;
        a.schemaVersion = 1;
        a.packId = "a";
        a.specs.push_back( oneCheckSpec( "z_spec" ) );
        a.specs.push_back( oneCheckSpec( "a_spec" ) );
        parts.push_back( a );
    }
    {
        VerifierPack b;
        b.schemaVersion = 1;
        b.packId = "b";
        b.specs.push_back( oneCheckSpec( "m_spec" ) );
        parts.push_back( b );
    }
    VerifierPack out;
    std::string error;
    REQUIRE( composePacks( "composed", parts, out, error ) );
    REQUIRE( error.empty() );
    REQUIRE( out.specs.size() == 3 );
    REQUIRE( out.specs[0].specId == "a_spec" ); // stable sort by specId
    REQUIRE( out.specs[1].specId == "m_spec" );
    REQUIRE( out.specs[2].specId == "z_spec" );
    const std::string digest = packDigest( out );
    // Re-composing the same parts yields the identical digest (stability).
    VerifierPack again;
    std::string againError;
    REQUIRE( composePacks( "composed", parts, again, againError ) );
    REQUIRE( packDigest( again ) == digest );

    // Duplicate specId across parts still names BOTH contributors.
    {
        VerifierPack a;
        a.schemaVersion = 1;
        a.packId = "left";
        a.specs.push_back( oneCheckSpec( "dup" ) );
        VerifierPack b;
        b.schemaVersion = 1;
        b.packId = "right";
        b.specs.push_back( oneCheckSpec( "dup" ) );
        VerifierPack conflictOut;
        std::string conflictError;
        REQUIRE_FALSE( composePacks( "c", { a, b }, conflictOut, conflictError ) );
        REQUIRE( conflictError.find( "'left'" ) != std::string::npos );
        REQUIRE( conflictError.find( "'right'" ) != std::string::npos );
    }
}
