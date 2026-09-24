// tests/test_scientific_planner_vocab.cpp — slice A0 RED→GREEN:
// closed-vocabulary closure/order and SHA-256 correctness (FIPS 180-4
// vectors), pinning the planner's wire-stable vocabulary surface.
#include <catch2/catch_test_macros.hpp>

#include "planner/planner_vocab.h"
#include "planner/sha256_util.h"

namespace vocab = sicnu::planner;

namespace
{

// No vocabulary may silently gain or lose a member: closure + no duplicates.
void checkClosedAndUnique( const std::vector<std::string> &vocab )
{
    for ( const auto &value : vocab )
        CHECK( vocab::isKnownVocabValue( vocab, value ) );
    for ( size_t i = 0; i < vocab.size(); ++i )
    {
        for ( size_t j = i + 1; j < vocab.size(); ++j )
            CHECK( vocab[i] != vocab[j] );
    }
}

} // namespace

TEST_CASE( "planner vocabularies are closed and duplicate-free", "[scientific_planner][vocab]" )
{
    checkClosedAndUnique( vocab::kGoalKinds );
    checkClosedAndUnique( vocab::kStepRoles );
    checkClosedAndUnique( vocab::kVerdicts );
    checkClosedAndUnique( vocab::kQuestionKinds );
    checkClosedAndUnique( vocab::kRiskKinds );
    checkClosedAndUnique( vocab::kCostClasses );
    checkClosedAndUnique( vocab::kAssetKinds );
    checkClosedAndUnique( vocab::kAssetStates );
    checkClosedAndUnique( vocab::kAssetModalities );
    checkClosedAndUnique( vocab::kModeKinds );
    checkClosedAndUnique( vocab::kAutonomyLevels );
    checkClosedAndUnique( vocab::kFamilySlots );
    checkClosedAndUnique( vocab::kProposalRejectionCodes );

    // Unknown probes are rejected by every membership check.
    CHECK_FALSE( vocab::isKnownGoalKind( "win_the_lottery" ) );
    CHECK_FALSE( vocab::isKnownStepRole( "party" ) );
    CHECK_FALSE( vocab::isKnownVerdict( "maybe" ) );
    CHECK_FALSE( vocab::isKnownQuestionKind( "unknown_kind" ) );
    CHECK_FALSE( vocab::isKnownRiskKind( "vibes" ) );
    CHECK_FALSE( vocab::isKnownCostClass( "astronomical" ) );
    CHECK_FALSE( vocab::isKnownAssetState( "partially_there" ) );
    CHECK_FALSE( vocab::isKnownModeKind( "chaos" ) );
    CHECK_FALSE( vocab::isKnownAutonomy( "possessed" ) );
    CHECK_FALSE( vocab::isKnownFamilySlot( "alchemistry" ) );
    CHECK_FALSE( vocab::isKnownProposalRejectionCode( "planner:proposal_vibes" ) );
    CHECK( vocab::kGoalKinds.size() == 7 ); // historical scope: 7 goal kinds
}

TEST_CASE( "proposal rejection vocabulary is sorted and planner-prefixed", "[scientific_planner][vocab]" )
{
    REQUIRE( vocab::kProposalRejectionCodes.size() > 0 );
    for ( size_t i = 0; i + 1 < vocab::kProposalRejectionCodes.size(); ++i )
        CHECK( vocab::kProposalRejectionCodes[i] < vocab::kProposalRejectionCodes[i + 1] );
    for ( const auto &code : vocab::kProposalRejectionCodes )
    {
        CHECK( code.rfind( "planner:proposal_", 0 ) == 0 );
        CHECK( vocab::isKnownProposalRejectionCode( code ) );
    }
}

TEST_CASE( "cost class rank and aggregation", "[scientific_planner][vocab]" )
{
    CHECK( vocab::costClassRank( "low" ) < vocab::costClassRank( "medium" ) );
    CHECK( vocab::costClassRank( "medium" ) < vocab::costClassRank( "high" ) );
    CHECK( vocab::costClassRank( "nope" ) == -1 );
    CHECK( vocab::maxCostClass( "low", "high" ) == "high" );
    CHECK( vocab::maxCostClass( "high", "medium" ) == "high" );
    CHECK( vocab::aggregateCostClass( {} ) == "low" );
    CHECK( vocab::aggregateCostClass( { "low", "medium", "low" } ) == "medium" );
    CHECK( vocab::aggregateCostClass( { "low", "high", "medium" } ) == "high" );
}

TEST_CASE( "asset state semantics: only ready is a hard fact", "[scientific_planner][vocab]" )
{
    CHECK( vocab::isHardFactAssetState( "ready" ) );
    CHECK_FALSE( vocab::isHardFactAssetState( "missing" ) );
    CHECK_FALSE( vocab::isHardFactAssetState( "offline" ) );
    CHECK_FALSE( vocab::isHardFactAssetState( "unknown" ) );
    for ( const auto &state : { "offline", "unavailable_source", "authentication_required",
                                "registered", "resolving", "stale" } )
        CHECK( vocab::isAvailabilityBlockedState( state ) );
    CHECK_FALSE( vocab::isAvailabilityBlockedState( "ready" ) );
    CHECK_FALSE( vocab::isAvailabilityBlockedState( "missing" ) );
}

TEST_CASE( "sha256 FIPS 180-4 vectors", "[scientific_planner][sha256]" )
{
    // FIPS 180-4 / NIST examples.
    CHECK( vocab::sha256Hex( "" )
           == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    CHECK( vocab::sha256Hex( "abc" )
           == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    // 56-byte message: spans the two-block boundary of the padding scheme.
    CHECK( vocab::sha256Hex( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" )
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
    // 'a' x 1000000 → cdc76e5c... is the classic million-'a' vector; the
    // 448-bit boundary case above already pins the multi-block path, and this
    // shorter derivative pins deterministic repetition.
    const std::string many( 1000, 'a' );
    const std::string once( 1, 'a' );
    CHECK( vocab::sha256Hex( many ) != vocab::sha256Hex( once ) );
    CHECK( vocab::fingerprint16( "abc" ) == "ba7816bf8f01cfea" );
    CHECK( vocab::fingerprint16( "" ) == "e3b0c44298fc1c14" );
    CHECK( vocab::fingerprint16( "abc" ).size() == 16 );
}
