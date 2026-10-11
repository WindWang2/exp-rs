/***************************************************************************
  tests/test_scientific_state_nul_tokens_r8.cpp

  R8 (issue #1465) regressions for the scientific_state enum token parsers:
  a JSON string can carry an embedded NUL ("\u0000"), and the 3-arg
  std::equal matcher walked the C-string literal with no length bound —
  "known\u0000" was ACCEPTED as "known" (reading the terminator), and
  "known\u0000x" read past the literal's end (UB, ASan-detectable). The
  parsers must compare lengths first and refuse any NUL-carrying token.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_types.h"

#include <string>

using namespace sicnu::state;

TEST_CASE( "enum token parsers refuse tokens with embedded NULs",
           "[scientific_state][nul][r8]" )
{
    // "known" + NUL exactly: size = strlen("known") + 1. The old 3-arg
    // std::equal matched all 5 literal bytes and then compared the NUL
    // against the literal's terminator — in bounds, but an ACCEPT of an
    // invalid token.
    ClaimKind claim = ClaimKind::Unknown;
    CHECK_FALSE( claimKindFromString( std::string( "known\0", 6 ), claim ) );

    // "known" + NUL + suffix: the old matcher kept advancing past the
    // literal's end — out-of-bounds read on the literal.
    CHECK_FALSE( claimKindFromString( std::string( "known\0x", 7 ), claim ) );
    CHECK_FALSE( claimKindFromString( std::string( "known\0\0\0\0", 9 ), claim ) );

    AssetKind asset = AssetKind::Unknown;
    CHECK_FALSE( assetKindFromString( std::string( "raster\0", 7 ), asset ) );
    CHECK_FALSE( assetKindFromString( std::string( "raster\0junk", 12 ), asset ) );

    AssetLifecycle lifecycle = AssetLifecycle::Unknown;
    CHECK_FALSE( assetLifecycleFromString( std::string( "ready\0", 6 ), lifecycle ) );
    CHECK_FALSE( assetLifecycleFromString( std::string( "ready\0x", 7 ), lifecycle ) );

    Modality modality = Modality::Unknown;
    CHECK_FALSE( modalityFromString( std::string( "optical\0", 8 ), modality ) );
    CHECK_FALSE( modalityFromString( std::string( "optical\0x", 9 ), modality ) );
}

TEST_CASE( "enum token parsers keep their case-insensitive round-trips",
           "[scientific_state][nul][r8]" )
{
    // The length guard must not narrow the accepted vocabulary: same-case,
    // mixed-case and every legal token still parse.
    ClaimKind claim = ClaimKind::Unknown;
    REQUIRE( claimKindFromString( "known", claim ) );
    CHECK( claim == ClaimKind::Known );
    REQUIRE( claimKindFromString( "KNOWN", claim ) );
    CHECK( claim == ClaimKind::Known );
    REQUIRE( claimKindFromString( "conflicted", claim ) );
    CHECK( claim == ClaimKind::Conflicted );
    CHECK_FALSE( claimKindFromString( "proven", claim ) );

    AssetKind asset = AssetKind::Unknown;
    REQUIRE( assetKindFromString( "Raster", asset ) );
    CHECK( asset == AssetKind::Raster );
    REQUIRE( assetKindFromString( "virtual_raster", asset ) );
    CHECK( asset == AssetKind::VirtualRaster );

    AssetLifecycle lifecycle = AssetLifecycle::Unknown;
    REQUIRE( assetLifecycleFromString( "unavailable_source", lifecycle ) );
    CHECK( lifecycle == AssetLifecycle::UnavailableSource );
    REQUIRE( assetLifecycleFromString( "AUTHENTICATION_REQUIRED", lifecycle ) );
    CHECK( lifecycle == AssetLifecycle::AuthenticationRequired );

    Modality modality = Modality::Unknown;
    REQUIRE( modalityFromString( "hyperspectral", modality ) );
    CHECK( modality == Modality::Hyperspectral );
    CHECK_FALSE( modalityFromString( "lidar", modality ) );
}
