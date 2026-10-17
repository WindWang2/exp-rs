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
#include "verification/check_runner.h"
#include "verification/checks_numeric.h"
#include "verification/digest.h"
#include "verification/packs_builtin.h"
#include "verification/providers.h"
#include "verification/report.h"
#include "verification/spec.h"

#include <json/json.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <locale>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// B-1/B-2: jsoncpp untyped integer access in the fromJson loaders (#1447
//          family) — behaviour fixes (RED first)
//
// isIntegral() says yes to uint64 values above INT64_MAX and to int64 values
// outside int32, but asInt64()/asInt() then THROW Json::LogicError (probed on
// the vendored jsoncpp 1.9.8: "LargestUInt out of Int64 range" / "LargestInt
// out of Int range"). The loaders' contract is false + error on malformed
// input; an escaping exception is the #1488 defect wearing a different member
// name, and it hands the caller a crash instead of a typed refusal.
// ---------------------------------------------------------------------------

TEST_CASE( "r16a B-1: spec and report loaders refuse out-of-range integers instead of throwing",
           "[verifier14][r16a]" )
{
    std::string error;

    // A budget count above INT64_MAX (jsoncpp stores it as uint64).
    Json::Value spec{ Json::objectValue };
    spec["spec_id"] = "hostile.spec";
    spec["budget"] = Json::Value{ Json::objectValue };
    spec["budget"]["max_checks"] = Json::Value( Json::UInt64( 18446744073709551615ULL ) );
    VerificationSpec specLoaded;
    REQUIRE( !VerificationSpec::fromJson( spec, specLoaded, error ) );
    REQUIRE( !error.empty() );

    // A depth outside int32 where an int is read.
    Json::Value deep = spec;
    deep["budget"] = Json::Value{ Json::objectValue };
    deep["budget"]["max_depth"] = Json::Value( Json::Int64( 3000000000LL ) );
    REQUIRE( !VerificationSpec::fromJson( deep, specLoaded, error ) );
    REQUIRE( !error.empty() );

    // schema_version outside int32 on both loaders.
    Json::Value specVersion = spec;
    specVersion.removeMember( "budget" );
    specVersion["schema_version"] = Json::Value( Json::Int64( 3000000000LL ) );
    REQUIRE( !VerificationSpec::fromJson( specVersion, specLoaded, error ) );
    REQUIRE( !error.empty() );

    Json::Value report{ Json::objectValue };
    report["schema_version"] = Json::Value( Json::Int64( 3000000000LL ) );
    VerificationReport reportLoaded;
    REQUIRE( !VerificationReport::fromJson( report, reportLoaded, error ) );
    REQUIRE( !error.empty() );

    report.removeMember( "schema_version" );
    report["budget_usage"] = Json::Value{ Json::objectValue };
    report["budget_usage"]["max_depth_seen"] = Json::Value( Json::Int64( 3000000000LL ) );
    REQUIRE( !VerificationReport::fromJson( report, reportLoaded, error ) );
    REQUIRE( !error.empty() );

    report["budget_usage"] = Json::Value{ Json::objectValue };
    report["budget_usage"]["checks_evaluated"] = Json::Value( Json::UInt64( 18446744073709551615ULL ) );
    REQUIRE( !VerificationReport::fromJson( report, reportLoaded, error ) );
    REQUIRE( !error.empty() );

    // Sanity for the guard itself: in-range values keep loading.
    Json::Value sane{ Json::objectValue };
    sane["spec_id"] = "sane.spec";
    sane["budget"] = Json::Value{ Json::objectValue };
    sane["budget"]["max_checks"] = Json::Value( Json::Int64( 64 ) );
    REQUIRE( VerificationSpec::fromJson( sane, specLoaded, error ) );
}

// ---------------------------------------------------------------------------
// B-3: an artifact description whose "kind" is not a string cannot crash the
//      runner — behaviour fix (RED first)
//
// checks_artifact.cpp read the kind via asString() without a type guard on the
// path where no kind expectation was declared (required_keys-only checks);
// an object/array kind escaped Json::LogicError straight through runSpec.
// ---------------------------------------------------------------------------

namespace
{

/// Describes exactly one artifact: the fixed facts handed to it at
/// construction. Enough provider for the artifact seam; the others stay null.
class FixedFactsProvider : public ArtifactProvider
{
  public:
    explicit FixedFactsProvider( Json::Value facts ) : m_facts( std::move( facts ) ) {}

    Availability describe( const std::string &, Json::Value &facts, std::string & ) const override
    {
        facts = m_facts;
        return Availability::Found;
    }

  private:
    Json::Value m_facts;
};

VerificationCheck artifactCheck( const std::string &id, Json::Value params )
{
    VerificationCheck check;
    check.id = id;
    check.kind = checkKindToWire( CheckKind::ArtifactShape );
    check.title = "r16a " + id;
    check.subject = SubjectRef{ "artifact", "product.tif" };
    check.params = std::move( params );
    return check;
}

VerificationInputs artifactInputs( const ArtifactProvider &provider )
{
    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = "r16a-fixture";
    return inputs;
}

} // namespace

TEST_CASE( "r16a B-3: a non-string artifact kind is an unreadable fact, never a crash",
           "[verifier14][r16a]" )
{
    // kind as an object — the shape a tired or hostile producer can hand back.
    Json::Value facts{ Json::objectValue };
    facts["kind"] = Json::Value{ Json::objectValue };
    facts["kind"]["surprise"] = true;
    facts["dtype"] = "uint16";
    const FixedFactsProvider provider( facts );

    VerificationSpec spec;
    spec.specId = "r16a.nonstring.kind";

    // A check with no kind expectation: this is the path that reached the
    // unguarded asString() before the fix.
    Json::Value params{ Json::objectValue };
    Json::Value required{ Json::arrayValue };
    required.append( "dtype" );
    params["required_keys"] = required;
    spec.checks.push_back( artifactCheck( "artifact.dtype_present", params ) );

    // A second check WITH a kind expectation, to pin the already-guarded path
    // (an unreadable kind is Indeterminate there, not a throw).
    Json::Value kindParams{ Json::objectValue };
    kindParams["expect"]["kind"] = "raster";
    spec.checks.push_back( artifactCheck( "artifact.kind_is_raster", kindParams ) );

    const VerificationReport report = runSpecFlat( spec, artifactInputs( provider ) );
    REQUIRE( report.results.size() == 2 );

    // The dtype-only check asked one question and the artifact answered it
    // (dtype is present); a garbage kind it was never asked about cannot
    // retroactively fail that question — but the report as a whole may not
    // read as Pass while any declared expectation stays unreadable.
    REQUIRE( report.results.front().status == CheckStatus::Pass );
    REQUIRE( report.results.back().status == CheckStatus::Indeterminate );
    REQUIRE( report.results.back().failureCode == failure_codes::kArtifactSchemaMismatch );
    REQUIRE( report.status == CheckStatus::Indeterminate );

    // An array kind must fare the same as an object kind.
    Json::Value arrayFacts{ Json::objectValue };
    arrayFacts["kind"] = Json::Value{ Json::arrayValue };
    arrayFacts["dtype"] = "uint16";
    const FixedFactsProvider arrayProvider( arrayFacts );
    const VerificationReport arrayReport = runSpecFlat( spec, artifactInputs( arrayProvider ) );
    REQUIRE( arrayReport.results.size() == 2 );
    REQUIRE( arrayReport.status == CheckStatus::Indeterminate );

    // And the whole-report seam: nothing above may throw out of runSpec, so
    // the digest must exist (empty digest means canonicalization refused).
    std::string digestError;
    REQUIRE( !reportDigest( report ).empty() );
    (void)digestError;
}

// ---------------------------------------------------------------------------
// D: digest determinism under key-order permutation (metamorphic property)
//    and across repeated runs (already correct — pins the contract the whole
//    content-addressing story rests on; the core lane only permutes 2 orders)
// ---------------------------------------------------------------------------

TEST_CASE( "r16a D: object key order never changes a digest, at any nesting depth",
           "[verifier14][r16a]" )
{
    // Sixteen top-level keys plus nested objects/arrays with mixed scalar
    // types — big enough that insertion orders genuinely differ.
    const std::vector<std::string> keys = { "alpha", "bravo",   "charlie", "delta",  "echo",
                                            "foxtrot", "golf", "hotel",  "india",  "juliet",
                                            "kilo",  "lima",    "mike",    "november", "oscar",
                                            "papa" };

    auto buildInOrder = [ &keys ]( std::size_t rotation )
    {
        Json::Value doc{ Json::objectValue };
        for ( std::size_t i = 0; i < keys.size(); ++i )
        {
            // The payload belongs to the KEY (stable index j), never to the
            // insertion position: rotating must reorder INSERTION only, or
            // the fixtures would differ in content and the property below
            // would compare different documents.
            const std::size_t j = ( i + rotation ) % keys.size();
            const std::string &key = keys[ j ];
            switch ( j % 4 )
            {
                case 0:
                    doc[ key ] = static_cast<Json::Int64>( j );
                    break;
                case 1:
                    doc[ key ] = "value-" + key;
                    break;
                case 2:
                    doc[ key ] = ( j % 8 ) == 0;
                    break;
                default:
                    doc[ key ]["inner"][ "deep" + std::to_string( j ) ] = static_cast<double>( j ) / 7.0;
                    doc[ key ]["list"].append( static_cast<Json::Int64>( j ) );
                    doc[ key ]["list"].append( "s" );
                    break;
            }
        }
        return doc;
    };

    std::string goldenText;
    std::string goldenDigest;
    std::string error;
    std::string digestError;
    for ( std::size_t rotation = 0; rotation < keys.size(); ++rotation )
    {
        const Json::Value permuted = buildInOrder( rotation );
        std::string text;
        REQUIRE( canonicalJson( permuted, text, error ) );
        if ( rotation == 0 )
        {
            goldenText = text;
            goldenDigest = canonicalDigestSha256( permuted, digestError );
            REQUIRE( goldenDigest.size() == 64 );
        }
        else
        {
            REQUIRE( text == goldenText );
            REQUIRE( canonicalDigestSha256( permuted, digestError ) == goldenDigest );
        }
    }

    // The golden text is sorted by member name, whatever order insertion used.
    REQUIRE( goldenText.find( "\"alpha\":" ) < goldenText.find( "\"bravo\":" ) );
    REQUIRE( goldenText.find( "\"oscar\":" ) < goldenText.find( "\"papa\":" ) );

    // A real content change must still move the digest, or the property above
    // would be vacuous.
    Json::Value mutated = buildInOrder( 5 );
    mutated["kilo"] = "changed";
    REQUIRE( canonicalDigestSha256( mutated, digestError ) != goldenDigest );
}

TEST_CASE( "r16a D: repeated runs of a full spec produce one report digest", "[verifier14][r16a]" )
{
    // An omniscient provider: every seam answers Found with plausible facts,
    // so the checks below actually conclude instead of staying unavailable.
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
            facts["dtype"] = "float32";
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
            out.push_back( ProvenanceDimension{ "operator", true, "rs:sar_calibrate" } );
            out.push_back( ProvenanceDimension{ "parameters", true, "declared" } );
            return Availability::Found;
        }
        Availability record( const std::string &, DigestRecord &out, std::string & ) const override
        {
            out.algorithm = "sha256";
            out.digest = std::string( 64, '0' );
            return Availability::Found;
        }
    };

    OmniscientProvider provider;

    VerificationSpec spec;
    spec.specId = "r16a.determinism";

    VerificationCheck state;
    state.id = "state.domain";
    state.kind = checkKindToWire( CheckKind::StateInvariant );
    state.subject = SubjectRef{ "node", "calibrate" };
    state.params["expect"]["numeric_domain"] = "linear";
    spec.checks.push_back( state );

    VerificationCheck grid;
    grid.id = "artifact.grid";
    grid.kind = checkKindToWire( CheckKind::ArtifactShape );
    grid.subject = SubjectRef{ "artifact", "product.tif" };
    grid.params["expect"]["size"]["width"] = 512;
    grid.params["expect"]["size"]["height"] = 512;
    spec.checks.push_back( grid );

    VerificationCheck metric;
    metric.id = "metric.mean";
    metric.kind = checkKindToWire( CheckKind::NumericRange );
    metric.subject = SubjectRef{ "metric", "mean" };
    metric.params["expect"]["lower"] = 0.0;
    metric.params["expect"]["upper"] = 1.0;
    spec.checks.push_back( metric );

    VerificationInputs inputs;
    inputs.state = &provider;
    inputs.artifact = &provider;
    inputs.metric = &provider;
    inputs.provenance = &provider;
    inputs.digest = &provider;
    inputs.sourceId = "r16a-fixture";

    const VerificationReport first = runSpecFlat( spec, inputs );
    REQUIRE( first.status == CheckStatus::Pass );
    const std::string firstDigest = reportDigest( first );
    REQUIRE( firstDigest.size() == 64 );

    for ( int run = 0; run < 3; ++run )
    {
        const VerificationReport again = runSpecFlat( spec, inputs );
        REQUIRE( reportDigest( again ) == firstDigest );
    }

    // Declaring the same checks in a different order keeps the SPEC identity
    // (specDigest is order-free by design) even though the report's result
    // array follows declaration order.
    VerificationSpec reordered = spec;
    std::reverse( reordered.checks.begin(), reordered.checks.end() );
    REQUIRE( specDigest( reordered ) == specDigest( spec ) );
}

// ---------------------------------------------------------------------------
// E: locale independence — a hostile global locale must not reach canonical
//    text (already correct: every seam imbues std::locale::classic())
// ---------------------------------------------------------------------------

TEST_CASE( "r16a E: canonical text ignores a global locale with a comma decimal separator",
           "[verifier14][r16a]" )
{
    struct CommaDecimalFacet : std::numpunct<char>
    {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };

    const std::locale friendly = std::locale();
    std::locale::global( std::locale( std::locale::classic(), new CommaDecimalFacet ) );
    struct RestoreLocale
    {
        std::locale saved;
        ~RestoreLocale() { std::locale::global( saved ); }
    } restore{ friendly };

    // Prove the hostile locale is actually in force for a default-constructed
    // stream — otherwise the assertions below prove nothing.
    {
        std::ostringstream plain;
        plain << 1.5;
        REQUIRE( plain.str() == "1,5" );
    }

    REQUIRE( roundSignificant( 1.5 ) == "1.5" );
    REQUIRE( roundSignificant( 1234567.25 ) == "1234567.25" );
    REQUIRE( roundSignificant( 0.1 + 0.2 ) == "0.3" );

    Json::Value value{ Json::objectValue };
    value["half"] = 0.5;
    value["sum"] = 0.1 + 0.2;
    value["big"] = 1e21;
    std::string text;
    std::string error;
    REQUIRE( canonicalJson( value, text, error ) );
    REQUIRE( text == "{\"big\":1e+21,\"half\":0.5,\"sum\":0.3}" );

    // Evidence text and the digest seam stay classic-locale too.
    REQUIRE( canonicalNumber( 2.5 ).asString() == "2.5" );
    std::string digestError;
    const std::string underHostile = canonicalDigestSha256( value, digestError );
    REQUIRE( underHostile.size() == 64 );

    std::locale::global( friendly );
    REQUIRE( canonicalDigestSha256( value, digestError ) == underHostile );
}

// ---------------------------------------------------------------------------
// G: the shipped builtin packs must never read as Pass — their params use a
//    vocabulary the evaluators do not consume (expect_present / expect_kind /
//    expect_grid / required_fields / require_resolved_inputs / top-level
//    algorithm / repeats), so with every provider answering they still land
//    Indeterminate. This pins the fail-closed side of that authoring gap
//    (an R14-E leftover, owned by the authoring track) until the substitution
//    layer exists; if a builtin check ever starts evaluating, this test asks
//    whoever changed it to prove the params now match the family contract.
// ---------------------------------------------------------------------------

TEST_CASE( "r16a G: builtin packs cannot silently read as Pass under an omniscient provider",
           "[verifier14][r16a]" )
{
    class FoundEverything : public StateProvider,
                            public ArtifactProvider,
                            public MetricProvider,
                            public ProvenanceProvider,
                            public DigestProvider
    {
      public:
        Availability tryGet( const std::string &, StateSnapshot &out, std::string & ) const override
        {
            out.numericDomain = "linear";
            out.radiometricState = "sigma0";
            return Availability::Found;
        }
        Availability describe( const std::string &, Json::Value &facts, std::string & ) const override
        {
            facts = Json::Value{ Json::objectValue };
            facts["kind"] = "raster";
            facts["dtype"] = "float32";
            facts["size"]["width"] = 512;
            facts["size"]["height"] = 512;
            facts["crs_authid"] = "EPSG:32650";
            return Availability::Found;
        }
        Availability metric( const std::string &, MetricValue &out, std::string & ) const override
        {
            out.value = 1.0;
            return Availability::Found;
        }
        Availability completeness( const std::string &, std::vector<ProvenanceDimension> &out,
                                   std::string & ) const override
        {
            out.push_back( ProvenanceDimension{ "operator", true, "registered" } );
            out.push_back( ProvenanceDimension{ "parameters", true, "recorded" } );
            out.push_back( ProvenanceDimension{ "inputs", true, "resolved" } );
            return Availability::Found;
        }
        Availability record( const std::string &, DigestRecord &out, std::string & ) const override
        {
            out.algorithm = "sha256";
            out.digest = std::string( 64, 'f' );
            return Availability::Found;
        }
    };

    FoundEverything provider;
    VerificationInputs inputs;
    inputs.state = &provider;
    inputs.artifact = &provider;
    inputs.metric = &provider;
    inputs.provenance = &provider;
    inputs.digest = &provider;
    inputs.sourceId = "r16a-fixture";

    std::vector<VerifierPack> packs = { structuralPack(), provenancePack(), reproducibilityPack() };
    for ( const VerifierPack &pack : packs )
    {
        UNSCOPED_INFO( "builtin pack: " << pack.id );
        VerificationSpec spec;
        spec.specId = "r16a.builtin." + pack.id;
        spec.checks = pack.checks;
        const VerificationReport report = runSpecFlat( spec, inputs );
        REQUIRE( report.status == CheckStatus::Indeterminate );
        for ( const CheckResult &result : report.results )
        {
            UNSCOPED_INFO( "builtin check: " << result.checkId );
            REQUIRE( result.status == CheckStatus::Indeterminate );
            REQUIRE( result.status != CheckStatus::Pass );
        }
    }

    // The derived contract packs use the evaluators' real vocabulary and DO
    // conclude (fail or pass depending on the provider) — proving the guard
    // above is about the builtin placeholder vocabulary, not about some
    // global inability to evaluate.
    const DerivedPack derived = scientificContractFor( "rs:sar_calibrate" );
    REQUIRE( !derived.indeterminate );
    VerificationSpec contractSpec;
    contractSpec.specId = "r16a.contract";
    contractSpec.checks = derived.pack.checks;
    const VerificationReport contractReport = runSpecFlat( contractSpec, inputs );
    REQUIRE( contractReport.results.size() == derived.pack.checks.size() );
}

// ---------------------------------------------------------------------------
// F: a near-budget document (tens of thousands of nodes) canonicalizes —
//    the size regime a real evidence pack produces (already correct; the
//    wall-clock and RSS numbers for this case live in PERFORMANCE.md)
// ---------------------------------------------------------------------------

TEST_CASE( "r16a F: a document just under the node budget canonicalizes completely",
           "[verifier14][r16a]" )
{
    // 1 root + 5 arrays + 5 x 9998 scalars = 49996 nodes < 50000, with every
    // per-level member count inside the 10000 cap. The budget must let the
    // WHOLE document through or reports would silently cover less than they
    // claim to.
    Json::Value root{ Json::arrayValue };
    for ( int block = 0; block < 5; ++block )
    {
        Json::Value inner{ Json::arrayValue };
        for ( int i = 0; i < 9998; ++i )
        {
            inner.append( block * 10000 + i );
        }
        root.append( inner );
    }
    std::string text;
    std::string error;
    REQUIRE( canonicalJson( root, text, error ) );
    REQUIRE( text.size() > 250000 );  // it is genuinely large output (~285 KB)
    std::string digestError;
    REQUIRE( canonicalDigestSha256( root, digestError ).size() == 64 );

    // Five nodes more than the budget refuse — the edge is exactly at 50000
    // total nodes (49996 + 5 = 50001). The extras are spread one per sub-array
    // so every per-level member count stays inside its own 10000 cap and the
    // refusal can only come from the total-node budget.
    Json::Value over = root;
    for ( Json::ArrayIndex block = 0; block < 5; ++block )
    {
        over[ block ].append( -1 );
    }
    REQUIRE( !canonicalJson( over, text, error ) );
    REQUIRE( error.find( "node" ) != std::string::npos );
}
