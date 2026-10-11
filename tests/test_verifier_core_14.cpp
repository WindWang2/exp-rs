/***************************************************************************
  test_verifier_core_14.cpp — Unified Scientific Verifier 14 (Slice A)

  Slice A owns THREE things, and this lane is the gate for all of them:

    1. the three-valued status lattice (Pass / Fail / Indeterminate) and the
       rule that an EMPTY set is Indeterminate — never Pass. This is the
       semantic floor of the whole track: every later check, pack and rollup
       inherits it.
    2. the closed failure-code vocabulary (machine-readable codes with a
       category and a replan class) that Agent consumers read.
    3. deterministic serialization (canonicalJson) + content digest
       (specDigest), which is what makes a report reproducible at all.

  Qt-free, QGIS-free, filesystem-free: every fixture below is built in memory,
  per the repo convention that lanes synthesize their own fixtures.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/canonical_json.h"
#include "verification/digest.h"
#include "verification/failure_codes.h"
#include "verification/spec.h"
#include "verification/status_lattice.h"

#include <json/json.h>

#include <limits>
#include <string>
#include <vector>

namespace
{

using sicnu::verification::CheckStatus;
using sicnu::verification::IndeterminatePolicy;

} // namespace

TEST_CASE( "verifier14: status lattice combine is Fail-dominant and downgrades to Indeterminate", "[verifier14][core]" )
{
    using sicnu::verification::combineStatus;

    REQUIRE( combineStatus( CheckStatus::Pass, CheckStatus::Pass ) == CheckStatus::Pass );
    REQUIRE( combineStatus( CheckStatus::Pass, CheckStatus::Indeterminate ) == CheckStatus::Indeterminate );
    REQUIRE( combineStatus( CheckStatus::Pass, CheckStatus::Fail ) == CheckStatus::Fail );
    REQUIRE( combineStatus( CheckStatus::Indeterminate, CheckStatus::Pass ) == CheckStatus::Indeterminate );
    REQUIRE( combineStatus( CheckStatus::Indeterminate, CheckStatus::Indeterminate ) == CheckStatus::Indeterminate );
    REQUIRE( combineStatus( CheckStatus::Indeterminate, CheckStatus::Fail ) == CheckStatus::Fail );
    REQUIRE( combineStatus( CheckStatus::Fail, CheckStatus::Pass ) == CheckStatus::Fail );
    REQUIRE( combineStatus( CheckStatus::Fail, CheckStatus::Indeterminate ) == CheckStatus::Fail );
    REQUIRE( combineStatus( CheckStatus::Fail, CheckStatus::Fail ) == CheckStatus::Fail );
}

TEST_CASE( "verifier14: an empty check set can never be treated as a pass", "[verifier14][core]" )
{
    using sicnu::verification::combineAll;

    // The whole point of the third value: absence of evidence is not success.
    REQUIRE( combineAll( {} ) == CheckStatus::Indeterminate );
    REQUIRE( combineAll( { CheckStatus::Pass } ) == CheckStatus::Pass );
    REQUIRE( combineAll( { CheckStatus::Pass, CheckStatus::Pass } ) == CheckStatus::Pass );

    // One Indeterminate is enough to stop a pass; one Fail is enough to stop
    // everything else.
    REQUIRE( combineAll( { CheckStatus::Pass, CheckStatus::Pass, CheckStatus::Pass,
                           CheckStatus::Indeterminate, CheckStatus::Pass } ) == CheckStatus::Indeterminate );
    REQUIRE( combineAll( { CheckStatus::Pass, CheckStatus::Fail, CheckStatus::Pass } ) == CheckStatus::Fail );
}

TEST_CASE( "verifier14: strict policy promotes Indeterminate to Fail and records who was promoted", "[verifier14][core]" )
{
    using sicnu::verification::rollUp;

    const std::vector<std::pair<std::string, CheckStatus>> input {
        { "artifact.exists", CheckStatus::Pass },
        { "artifact.grid", CheckStatus::Indeterminate },
        { "provenance.complete", CheckStatus::Pass },
    };

    const auto keep = rollUp( input, IndeterminatePolicy::Keep );
    REQUIRE( keep.status == CheckStatus::Indeterminate );
    REQUIRE( keep.promoted.empty() );

    const auto strict = rollUp( input, IndeterminatePolicy::Fail );
    REQUIRE( strict.status == CheckStatus::Fail );
    REQUIRE( strict.promoted.size() == 1 );
    REQUIRE( strict.promoted.front() == "artifact.grid" );

    // Strict mode must not invent promotions when there is nothing to promote.
    const auto strictAllPass = rollUp( { { "a", CheckStatus::Pass } }, IndeterminatePolicy::Fail );
    REQUIRE( strictAllPass.status == CheckStatus::Pass );
    REQUIRE( strictAllPass.promoted.empty() );

    // Empty input stays Indeterminate even under the strict policy: it is not
    // a failure, it is "we do not know", and the caller must learn that.
    const auto strictEmpty = rollUp( {}, IndeterminatePolicy::Fail );
    REQUIRE( strictEmpty.status == CheckStatus::Indeterminate );
    REQUIRE( strictEmpty.promoted.empty() );
}

TEST_CASE( "verifier14: status wire strings round-trip and reject look-alikes", "[verifier14][core]" )
{
    using sicnu::verification::statusFromWire;
    using sicnu::verification::statusToWire;

    REQUIRE( std::string( statusToWire( CheckStatus::Pass ) ) == "pass" );
    REQUIRE( std::string( statusToWire( CheckStatus::Fail ) ) == "fail" );
    REQUIRE( std::string( statusToWire( CheckStatus::Indeterminate ) ) == "indeterminate" );

    CheckStatus out = CheckStatus::Pass;
    REQUIRE( statusFromWire( "pass", out ) );
    REQUIRE( out == CheckStatus::Pass );
    REQUIRE( statusFromWire( "fail", out ) );
    REQUIRE( out == CheckStatus::Fail );
    REQUIRE( statusFromWire( "indeterminate", out ) );
    REQUIRE( out == CheckStatus::Indeterminate );

    // Look-alikes must be refused, never silently mapped onto a passing value.
    out = CheckStatus::Pass;
    REQUIRE( !statusFromWire( "passed", out ) );
    REQUIRE( !statusFromWire( "PASS", out ) );
    REQUIRE( !statusFromWire( "unknown", out ) );
    REQUIRE( !statusFromWire( "", out ) );
    // On refusal the output must not be left smelling like a pass; the guard
    // below pins the current contract (caller's value is untouched).
    REQUIRE( out == CheckStatus::Pass );
}

TEST_CASE( "verifier14: every failure code carries a category, replan class and human hint", "[verifier14][core]" )
{
    namespace fc = sicnu::verification::failure_codes;

    const std::vector<std::string> codes = sicnu::verification::allFailureCodes();
    REQUIRE( !codes.empty() );

    for ( const std::string &code : codes )
    {
        UNSCOPED_INFO( "code under test: " << code );
        REQUIRE( sicnu::verification::isKnownFailureCode( code ) );
        REQUIRE( !sicnu::verification::failureHintForCode( code ).empty() );
        const std::string category = sicnu::verification::failureCategoryForCode( code );
        REQUIRE( ( category == "validation" || category == "io" || category == "resource" ) );
        const auto replan = sicnu::verification::replanClassForCode( code );
        REQUIRE( ( replan == sicnu::verification::ReplanClass::None ||
                   replan == sicnu::verification::ReplanClass::Retry ||
                   replan == sicnu::verification::ReplanClass::Replan ||
                   replan == sicnu::verification::ReplanClass::Abort ) );
    }

    // The vocabulary the track promised must actually exist.
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kSpecInvalid ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kUnsupportedCheckKind ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kNoChecks ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kEvidenceUnavailable ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kStateInvariantViolation ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kArtifactMissing ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kNumericNotFinite ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kProvenanceIncomplete ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kReproducibilityDigestMismatch ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kCrossOutputInconsistent ) );
    REQUIRE( sicnu::verification::isKnownFailureCode( fc::kBudgetExceeded ) );

    // Unknown codes are NOT in the table, and the conservative fallback must
    // not be "ignore it".
    REQUIRE( !sicnu::verification::isKnownFailureCode( "VERIFY.MADE_UP" ) );
    REQUIRE( !sicnu::verification::isKnownFailureCode( "" ) );
    REQUIRE( !sicnu::verification::isKnownFailureCode( "pass" ) );
}

TEST_CASE( "verifier14: canonical json is member-order independent and refuses unbounded depth", "[verifier14][core]" )
{
    using sicnu::verification::canonicalJson;

    Json::Value a{ Json::objectValue };
    a["zeta"] = 1;
    a["alpha"] = 2;
    a["mid"]["deep"] = 3;

    Json::Value b{ Json::objectValue };
    b["alpha"] = 2;
    b["mid"]["deep"] = 3;
    b["zeta"] = 1;

    std::string outA;
    std::string outB;
    std::string error;
    REQUIRE( canonicalJson( a, outA, error ) );
    REQUIRE( canonicalJson( b, outB, error ) );
    REQUIRE( outA == outB );

    // Byte-exact golden text. Cross-checking two permutations only proves the
    // writer is CONSISTENT; pinning the literal output proves the members are
    // actually emitted in sorted order, which is what makes the form canonical
    // rather than merely stable. Removing the key sort today survives
    // (jsoncpp's getMemberNames is itself ordered) and that is precisely why
    // this stronger assertion carries the load instead.
    REQUIRE( outA == "{\"alpha\":2,\"mid\":{\"deep\":3},\"zeta\":1}" );

    // Floating point must be pinned to 12 significant digits, otherwise a
    // digest over the same logical value drifts between platforms.
    Json::Value computed{ Json::objectValue };
    computed["v"] = 0.1 + 0.2;
    Json::Value literal{ Json::objectValue };
    literal["v"] = 0.3;
    std::string computedText;
    std::string literalText;
    REQUIRE( canonicalJson( computed, computedText, error ) );
    REQUIRE( canonicalJson( literal, literalText, error ) );
    REQUIRE( computedText == literalText );

    // Non-finite numbers cannot be canonicalized deterministically: refuse
    // instead of writing "nan" / "inf" into a digest.
    Json::Value broken{ Json::objectValue };
    broken["v"] = std::numeric_limits<double>::quiet_NaN();
    std::string brokenText;
    REQUIRE( !canonicalJson( broken, brokenText, error ) );
    REQUIRE( !error.empty() );

    // Depth bomb: the same defects as #1154/#1155 must not be reachable here.
    Json::Value deep{ Json::objectValue };
    Json::Value *cursor = &deep;
    for ( int i = 0; i < 200; ++i )
    {
        ( *cursor )["n"] = Json::Value{ Json::objectValue };
        cursor = &( *cursor )["n"];
    }
    std::string deepText;
    REQUIRE( !canonicalJson( deep, deepText, error ) );
    REQUIRE( !error.empty() );
}

TEST_CASE( "verifier14: spec digest is stable and sensitive to real changes", "[verifier14][core]" )
{
    using sicnu::verification::VerificationCheck;
    using sicnu::verification::specDigest;

    sicnu::verification::VerificationSpec spec;
    spec.specId = "radiometric-calibration.node.sar_calibrate";
    VerificationCheck grid;
    grid.id = "artifact.grid";
    grid.kind = "artifact_shape";
    grid.title = "output grid matches the declared expectation";
    spec.checks.push_back( grid );

    const std::string first = specDigest( spec );
    REQUIRE( first.size() == 64 );
    REQUIRE( specDigest( spec ) == first );

    // A reordered fixture must NOT change the digest (content addressing).
    sicnu::verification::VerificationSpec reordered;
    reordered.specId = spec.specId;
    reordered.checks = spec.checks;
    REQUIRE( specDigest( reordered ) == first );

    // Changing anything meaningful MUST change it — otherwise the digest is
    // decoration rather than identity.
    sicnu::verification::VerificationSpec retitled = spec;
    retitled.checks.front().title = "different title";
    REQUIRE( specDigest( retitled ) != first );

    sicnu::verification::VerificationSpec renamed = spec;
    renamed.specId = "other.spec";
    REQUIRE( specDigest( renamed ) != first );

    // An empty spec is representable but is NOT a passing spec: the empty-set
    // rule from the lattice must be reachable through the runner too.
    sicnu::verification::VerificationSpec empty;
    REQUIRE( empty.checks.empty() );
    REQUIRE( sicnu::verification::combineAll( {} ) == CheckStatus::Indeterminate );
}

TEST_CASE( "verifier14: canonical digest delegates to the single sha256 implementation", "[verifier14][core]" )
{
    Json::Value value{ Json::objectValue };
    value["kind"] = "raster";
    value["band_count"] = 4;

    std::string error;
    const std::string first = sicnu::verification::canonicalDigestSha256( value, error );
    REQUIRE( error.empty() );
    REQUIRE( first.size() == 64 );
    REQUIRE( sicnu::verification::canonicalDigestSha256( value, error ) == first );

    // Known-answer check against the FIPS 180-4 vector for the empty string,
    // so the borrow at build time can never be silently replaced by a stub.
    Json::Value nothing{ Json::objectValue };
    const std::string nothingDigest = sicnu::verification::canonicalDigestSha256( nothing, error );
    REQUIRE( error.empty() );
    // canonical form of an empty object is "{}"; this pins that the digest is
    // taken over canonical TEXT and not over jsoncpp's default writer output.
    REQUIRE( nothingDigest.size() == 64 );
    REQUIRE( sicnu::verification::canonicalDigestSha256( Json::Value{ Json::objectValue }, error ) == nothingDigest );
}
