// tests/test_r28_t03_verification_matrix.cpp
//
// R28-T03 M2 (WP06): the verification aggregation fail-closed matrix.
//
// VerificationReport::aggregate is the boundary where per-artifact verdicts
// become the one verdict the session trusts for delivery. The matrix:
//
//   empty artifacts                 → INDETERMINATE (nothing verified)
//   all PASS                        → PASS
//   PASS + PASS_WITH_WARNINGS       → PASS_WITH_WARNINGS
//   any FAIL (mixed with anything)  → FAIL (FAIL dominates everything)
//   ANY unknown/empty per-artifact verdict → NOT PASS: INDETERMINATE
//     (an unknown verdict is "not verified" — only an explicit verdict may
//     raise the aggregate; garbage must never ride a PASS upgrade)
#include <catch2/catch_test_macros.hpp>

#include "agent_loop/session_seams.h"

using namespace sicnu::agent_loop;

namespace
{

ArtifactVerificationReport artifact( const std::string &verdict )
{
    ArtifactVerificationReport report;
    report.verdict = verdict;
    report.path = "/matrix/" + verdict + ".tif";
    return report;
}

} // namespace

TEST_CASE( "the verification aggregation matrix is fail-closed",
           "[r28_t03][verification_matrix]" )
{
    SECTION( "empty artifacts stay INDETERMINATE" )
    {
        CHECK( VerificationReport::aggregate( {} ) == kVerificationIndeterminate );
    }

    SECTION( "explicit verdicts aggregate as documented" )
    {
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ) } ) == "PASS" );
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ), artifact( "PASS" ) } ) ==
               "PASS" );
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ),
                                                artifact( "PASS_WITH_WARNINGS" ) } ) ==
               "PASS_WITH_WARNINGS" );
        CHECK( VerificationReport::aggregate( { artifact( "PASS_WITH_WARNINGS" ) } ) ==
               "PASS_WITH_WARNINGS" );
    }

    SECTION( "any FAIL dominates the aggregate" )
    {
        CHECK( VerificationReport::aggregate( { artifact( "FAIL" ) } ) == "FAIL" );
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ), artifact( "FAIL" ) } ) ==
               "FAIL" );
        CHECK( VerificationReport::aggregate( { artifact( "PASS_WITH_WARNINGS" ),
                                                artifact( "FAIL" ) } ) == "FAIL" );
        // FAIL wins even over unknown verdicts — FAIL is the strongest signal.
        CHECK( VerificationReport::aggregate( { artifact( "INDETERMINATE" ),
                                                artifact( "FAIL" ) } ) == "FAIL" );
    }

    SECTION( "unknown per-artifact verdicts never aggregate to PASS" )
    {
        CHECK( VerificationReport::aggregate( { artifact( "INDETERMINATE" ) } ) ==
               kVerificationIndeterminate );
        CHECK( VerificationReport::aggregate( { artifact( "" ) } ) ==
               kVerificationIndeterminate );
        CHECK( VerificationReport::aggregate( { artifact( "pass" ) } ) ==
               kVerificationIndeterminate ); // case is part of the vocabulary
        CHECK( VerificationReport::aggregate( { artifact( "unknown" ) } ) ==
               kVerificationIndeterminate );
        // A single unknown among explicit passes poisons the aggregate — it
        // can never ride along a PASS upgrade.
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ),
                                                artifact( "INDETERMINATE" ) } ) ==
               kVerificationIndeterminate );
        CHECK( VerificationReport::aggregate( { artifact( "PASS" ),
                                                artifact( "PASS_WITH_WARNINGS" ),
                                                artifact( "" ) } ) ==
               kVerificationIndeterminate );
    }

    SECTION( "the default report verdict is the indeterminate sentinel" )
    {
        const VerificationReport report;
        CHECK( report.verdict() == kVerificationIndeterminate );
        CHECK( report.artifacts.empty() );
    }
}
