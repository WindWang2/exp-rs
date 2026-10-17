/***************************************************************************
  test_r16_a_verifier.cpp — R16-A-T01 hardening lane for src/verification

  This lane exists to close the #1488/#1447 follow-ups the seven _14 lanes do
  not reach, and to pin the determinism contracts the module's digests rest on:

    A. canonical-json boundary behaviour (#1488 aftermath): the five Canonical
       Limits budgets actually trip at their stated edges, integer extremes
       round-trip exactly, and IEEE-equal doubles serialize identically —
       including negative zero, where "-0" vs "0" would fork digests for two
       runs that computed the same value.
    B. jsoncpp untyped-access escapes (#1447 same type): every fromJson seam
       answers false + error for out-of-range integers instead of letting
       Json::LogicError escape, and an artifact description whose "kind" is an
       object cannot crash the runner.
    C/D. digest determinism: key-order permutations and repeated runs produce
       byte-identical digests (metamorphic property).
    E. locale independence: a global locale with a comma decimal separator
       cannot leak into canonical text.
    F. a near-budget document (tens of thousands of nodes) canonicalizes.
    G. the shipped builtin packs can never read as Pass while their params use
       a vocabulary the evaluators do not consume.

  Every assertion that encodes NEW behaviour (the fixes above) was written
  first and shown failing; see .planning/r16-a-t01/TEST_MATRIX.md for the RED
  evidence. Assertions marked "already correct" pin existing behaviour that no
  other lane reaches, so a regression cannot pass unnoticed.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/canonical_json.h"
#include "verification/checks_numeric.h"
#include "verification/digest.h"

#include <json/json.h>

#include <limits>
#include <string>

using namespace sicnu::verification;

namespace
{

/// A vertical chain of @p depth objects: {"n":{"n":{...{}}}}. The root object
/// is depth 1, so the deepest write() call happens at exactly @p depth.
Json::Value objectChain( int depth )
{
    Json::Value root{ Json::objectValue };
    Json::Value *cursor = &root;
    for ( int i = 1; i < depth; ++i )
    {
        ( *cursor )["n"] = Json::Value{ Json::objectValue };
        cursor = &( *cursor )["n"];
    }
    return root;
}

/// An object with @p members integer members ("k00000".."kNNNNN").
Json::Value flatObject( int members )
{
    Json::Value root{ Json::objectValue };
    for ( int i = 0; i < members; ++i )
    {
        root[ "k" + std::to_string( i ) ] = i;
    }
    return root;
}

} // namespace

// ---------------------------------------------------------------------------
// A-1: IEEE negative zero — behaviour fix (RED first)
// ---------------------------------------------------------------------------

TEST_CASE( "r16a A-1: negative zero serializes like positive zero, so equal runs get one digest",
           "[verifier14][r16a]" )
{
    // -0.0 == 0.0 under IEEE comparison, so a run that computes one and a run
    // that computes the other are runs over the same value. If their canonical
    // text differs ("-0" vs "0"), their digests differ and content addressing
    // reports a change where there is none — the exact defect class the
    // canonical writer exists to remove (0.1+0.2 vs 0.3).
    REQUIRE( roundSignificant( 0.0 ) == "0" );
    REQUIRE( roundSignificant( -0.0 ) == "0" );

    Json::Value minus{ Json::objectValue };
    minus["v"] = -0.0;
    Json::Value plus{ Json::objectValue };
    plus["v"] = 0.0;

    std::string minusText;
    std::string plusText;
    std::string error;
    REQUIRE( canonicalJson( minus, minusText, error ) );
    REQUIRE( canonicalJson( plus, plusText, error ) );
    REQUIRE( minusText == plusText );
    REQUIRE( minusText == "{\"v\":0}" );

    // The digest seam, which is what actually gets compared across runs.
    std::string digestError;
    const std::string minusDigest = canonicalDigestSha256( minus, digestError );
    const std::string plusDigest = canonicalDigestSha256( plus, digestError );
    REQUIRE( !minusDigest.empty() );
    REQUIRE( minusDigest == plusDigest );

    // Evidence text goes through the same helper; one quantity, one spelling.
    REQUIRE( canonicalNumber( -0.0 ).asString() == "0" );
    REQUIRE( canonicalNumber( 0.0 ).asString() == "0" );

    // A computed negative-zero delta must not fork the digest either.
    Json::Value deltaA{ Json::objectValue };
    deltaA["delta"] = -0.0;
    Json::Value deltaB{ Json::objectValue };
    deltaB["delta"] = 0.0;
    REQUIRE( canonicalDigestSha256( deltaA, digestError ) ==
             canonicalDigestSha256( deltaB, digestError ) );
}

// ---------------------------------------------------------------------------
// A-2: the five CanonicalLimits budgets trip at their stated edges
//      (already correct — no other lane reaches these edges)
// ---------------------------------------------------------------------------

TEST_CASE( "r16a A-2: the canonical budgets trip exactly where they are declared",
           "[verifier14][r16a]" )
{
    std::string out;
    std::string error;

    // Depth: a 32-level chain is legal, the 33rd level is refused.
    REQUIRE( canonicalJson( objectChain( 32 ), out, error ) );
    REQUIRE( !canonicalJson( objectChain( 33 ), out, error ) );
    REQUIRE( error.find( "depth" ) != std::string::npos );

    // Per-string: 4096 chars fit, 4097 do not.
    Json::Value exactString{ std::string( 4096, 'a' ) };
    REQUIRE( canonicalJson( exactString, out, error ) );
    Json::Value overString{ std::string( 4097, 'a' ) };
    REQUIRE( !canonicalJson( overString, out, error ) );
    REQUIRE( error.find( "string" ) != std::string::npos );

    // Members per object level: 10000 fit, 10001 do not.
    REQUIRE( canonicalJson( flatObject( 10000 ), out, error ) );
    REQUIRE( !canonicalJson( flatObject( 10001 ), out, error ) );
    REQUIRE( error.find( "member" ) != std::string::npos );

    // Total nodes: a document past 50000 nodes is refused, not truncated.
    Json::Value wide{ Json::arrayValue };
    for ( int block = 0; block < 6; ++block )
    {
        Json::Value inner{ Json::arrayValue };
        for ( int i = 0; i < 10000; ++i )
        {
            inner.append( i );
        }
        wide.append( inner );
    }
    REQUIRE( !canonicalJson( wide, out, error ) );
    REQUIRE( error.find( "node" ) != std::string::npos );

    // Output bytes: ~4.4 MiB of legal short strings is refused even though
    // every other budget is respected.
    Json::Value heavy{ Json::objectValue };
    for ( int i = 0; i < 1100; ++i )
    {
        heavy[ "s" + std::to_string( i ) ] = std::string( 4000, 'b' );
    }
    REQUIRE( !canonicalJson( heavy, out, error ) );
    REQUIRE( error.find( "output" ) != std::string::npos );

    // Refusal must leave the caller's buffer untouched (pinned by the core
    // lane for failure generally; re-pinned here per budget).
    std::string preserved = "PRESERVED";
    REQUIRE( !canonicalJson( overString, preserved, error ) );
    REQUIRE( preserved == "PRESERVED" );
}

TEST_CASE( "r16a A-3: integer extremes serialize exactly and doubles pin to 12 digits",
           "[verifier14][r16a]" )
{
    std::string out;
    std::string error;

    // int64 both ends: std::to_string round-trips, no double detour that would
    // lose the low bits (already correct — pins the #1509 branch order).
    Json::Value intMin( std::numeric_limits<Json::Int64>::min() );
    REQUIRE( canonicalJson( intMin, out, error ) );
    REQUIRE( out == "-9223372036854775808" );
    Json::Value intMax( std::numeric_limits<Json::Int64>::max() );
    REQUIRE( canonicalJson( intMax, out, error ) );
    REQUIRE( out == "9223372036854775807" );

    // uint64 above INT64_MAX stays exact — the original #1488 crash, pinned.
    Json::Value uintAbove( Json::UInt64( 9223372036854775808ULL ) );
    REQUIRE( canonicalJson( uintAbove, out, error ) );
    REQUIRE( out == "9223372036854775808" );
    Json::Value uintMax( Json::UInt64( 18446744073709551615ULL ) );
    REQUIRE( canonicalJson( uintMax, out, error ) );
    REQUIRE( out == "18446744073709551615" );

    // The 2^53 double-precision boundary: an integer-valued double inside the
    // int64 range keeps every digit — jsoncpp's isInt64() admits it, so a
    // quantity spelled once as an integer and once as a whole-number double
    // yields ONE canonical spelling. That is the strongest form of the
    // "one quantity, one spelling" contract, so pin it at the boundary where
    // double precision itself runs out.
    Json::Value twoPow53Int( Json::Int64( 9007199254740992LL ) );
    REQUIRE( canonicalJson( twoPow53Int, out, error ) );
    REQUIRE( out == "9007199254740992" );
    Json::Value twoPow53Double( 9007199254740992.0 );
    REQUIRE( canonicalJson( twoPow53Double, out, error ) );
    REQUIRE( out == "9007199254740992" );

    // The same exactness holds one uint64 step above INT64_MAX...
    Json::Value hugeWhole( 1e19 );
    REQUIRE( canonicalJson( hugeWhole, out, error ) );
    REQUIRE( out == "10000000000000000000" );

    // ...while a double beyond every integer range falls back to the pinned
    // 12-significant-digit text, deterministically.
    Json::Value huge( 1e300 );
    REQUIRE( canonicalJson( huge, out, error ) );
    REQUIRE( out == "1e+300" );
    Json::Value fractional( 3.141592653589793 );
    REQUIRE( canonicalJson( fractional, out, error ) );
    REQUIRE( out == "3.14159265359" );

    // Both infinities are refused exactly like NaN (only NaN is covered by the
    // core lane; the symmetric cases were unpinned).
    Json::Value positiveInf( std::numeric_limits<double>::infinity() );
    REQUIRE( !canonicalJson( positiveInf, out, error ) );
    Json::Value negativeInf( -std::numeric_limits<double>::infinity() );
    REQUIRE( !canonicalJson( negativeInf, out, error ) );
    REQUIRE( !error.empty() );
}
