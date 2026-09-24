// tests/test_verifier_render.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice F: the deterministic
// teaching/agent/text renders and the harness / LabEvidence shape
// projections.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "verify/projections.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_render.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

VerificationCheckResult result( const std::string &id, const std::string &kind, VerificationStatus status,
                                const std::string &code = "", const std::string &message = "" )
{
    VerificationCheckResult check;
    check.checkId = id;
    check.kind = kind;
    check.status = status;
    check.code = code;
    check.message = message;
    return check;
}

VerificationReport reportWith( std::vector<VerificationCheckResult> checks )
{
    return buildReport( "spec.render", "node", std::string( 64, 'a' ), std::move( checks ) );
}

} // namespace

TEST_CASE( "renderText is a deterministic one-line summary", "[verify][render][F]" )
{
    const VerificationReport report = reportWith( {
        result( "a", "artifact.exists", VerificationStatus::Pass ),
        result( "b", "metric.range", VerificationStatus::Fail, kCodeMetricOutOfRange, "too high" ),
    } );
    REQUIRE( renderText( report ) ==
             "verify(spec.render): Fail (fail=1 indeterminate=0 pass=1)" );

    const VerificationReport empty =
        buildReport( "spec.none", "task", std::string( 64, 'b' ), {} );
    REQUIRE( renderText( empty ) ==
             "verify(spec.none): Indeterminate (fail=0 indeterminate=0 pass=0)" );
}

TEST_CASE( "renderTeaching lists checks in declaration order with status words",
           "[verify][render][F]" )
{
    const VerificationReport report = reportWith( {
        result( "exists", "artifact.exists", VerificationStatus::Pass ),
        result( "range", "metric.range", VerificationStatus::Indeterminate, kCodeMetricMissing,
                "metric not recorded" ),
    } );
    const std::string text = renderTeaching( report );
    REQUIRE( text.find( "Verification spec.render [node]" ) != std::string::npos );
    REQUIRE( text.find( "[Pass] exists" ) != std::string::npos );
    REQUIRE( text.find( "[Indeterminate] range" ) != std::string::npos );
    REQUIRE( text.find( "metric not recorded" ) != std::string::npos );
    REQUIRE( text.find( "1 passed, 0 failed, 1 could not be judged" ) != std::string::npos );
    // Deterministic: same report -> same bytes.
    REQUIRE( text == renderTeaching( report ) );
    // Control characters from ids must not survive into the render.
    const VerificationReport hostile = reportWith( {
        result( std::string( "bad\nid" ), "metric.range", VerificationStatus::Pass ),
    } );
    REQUIRE( renderTeaching( hostile ).find( '\n' + std::string( "id" ) ) == std::string::npos );
}

TEST_CASE( "renderAgent exposes the blocking list and suggested actions",
           "[verify][render][F]" )
{
    const VerificationReport report = reportWith( {
        result( "ok", "artifact.exists", VerificationStatus::Pass ),
        result( "range", "metric.range", VerificationStatus::Fail, kCodeMetricOutOfRange, "above max" ),
        result( "digest", "reproducibility.digest", VerificationStatus::Indeterminate,
                kCodeProviderMissing, "no probe" ),
    } );
    const Json::Value agent = renderAgent( report );

    REQUIRE( agent["overall"].asString() == "fail" );
    REQUIRE( agent["counts"]["pass"].asUInt64() == 1 );
    REQUIRE( agent["counts"]["fail"].asUInt64() == 1 );
    REQUIRE( agent["counts"]["indeterminate"].asUInt64() == 1 );
    REQUIRE( agent["blocking"].size() == 2 );
    REQUIRE( agent["blocking"][0]["checkId"].asString() == "range" );
    REQUIRE( agent["blocking"][0]["suggestedAction"].asString() == "repair_output_then_reverify" );
    REQUIRE( agent["blocking"][1]["suggestedAction"].asString() == "attach_provider_then_reverify" );
    // Overall-level suggestion = first blocking check's action.
    REQUIRE( agent["suggestedAction"].asString() == "repair_output_then_reverify" );

    const Json::Value clean = renderAgent( reportWith( {
        result( "ok", "artifact.exists", VerificationStatus::Pass ),
    } ) );
    REQUIRE( clean["blocking"].empty() );
    REQUIRE( clean["suggestedAction"].asString() == "none" );
}

TEST_CASE( "suggestedAction is total over the closed vocabulary and conservative beyond",
           "[verify][render][F]" )
{
    // Every declared code maps to a non-empty, code-class-consistent action.
    for ( const std::string &code :
          { std::string( kCodeInvalidSpec ),       std::string( kCodeStateViolated ),
            std::string( kCodeArtifactMissing ),   std::string( kCodeArtifactTooSmall ),
            std::string( kCodeTypeMismatch ),      std::string( kCodeGridMismatch ),
            std::string( kCodeSchemaMismatch ),    std::string( kCodeMetricOutOfRange ),
            std::string( kCodeRelationViolated ),  std::string( kCodeProvenanceIncomplete ),
            std::string( kCodeDigestMismatch ),    std::string( kCodeCrossOutputInconsistent ),
            std::string( kCodeProviderMissing ),   std::string( kCodeArtifactUnreadable ),
            std::string( kCodeMetricMissing ),     std::string( kCodeMetricNotFinite ),
            std::string( kCodeProvenanceMissing ), std::string( kCodeDigestUnavailable ),
            std::string( kCodeEmptyInput ) } )
    {
        const std::string action = suggestedAction( code );
        REQUIRE_FALSE( action.empty() );
        if ( isIndeterminateCode( code ) )
            REQUIRE( action.find( "reverify" ) != std::string::npos );
    }
    // A violated contract is never advised into a mere retry.
    REQUIRE( suggestedAction( kCodeArtifactMissing ) == "produce_missing_output_then_reverify" );
    // Unknown and empty codes escalate — never a routine repair suggestion.
    REQUIRE( suggestedAction( "" ) == "escalate_unverifiable" );
    REQUIRE( suggestedAction( "verify:e_from_the_future" ) == "escalate_unverifiable" );
}

TEST_CASE( "harness projection stays fail-closed for indeterminate reports",
           "[verify][render][F]" )
{
    const VerificationReport unknown =
        buildReport( "spec.x", "node", std::string( 64, 'a' ),
                     { result( "c1", "metric.range", VerificationStatus::Indeterminate,
                               kCodeMetricMissing, "missing" ) } );
    const Json::Value projected = harnessVerificationFromReport( unknown );

    // The harness tri-state has no "unknown": the projection refuses to fold
    // it into a success class.
    REQUIRE( projected["verdict"].asString() == "FAIL" );
    REQUIRE( projected["checks"][0]["passed"].asBool() == false );
    REQUIRE( projected["checks"][0]["severity"].asString() == "error" );
    REQUIRE( projected["checks"][0]["code"].asString() == kCodeMetricMissing );

    const VerificationReport failing =
        buildReport( "spec.y", "node", std::string( 64, 'b' ),
                     { result( "c1", "metric.range", VerificationStatus::Fail,
                               kCodeMetricOutOfRange, "out" ) } );
    REQUIRE( harnessVerificationFromReport( failing )["verdict"].asString() == "FAIL" );

    const VerificationReport passing =
        buildReport( "spec.z", "node", std::string( 64, 'c' ),
                     { result( "c1", "metric.range", VerificationStatus::Pass ) } );
    const Json::Value passProjected = harnessVerificationFromReport( passing );
    REQUIRE( passProjected["verdict"].asString() == "PASS" );
    REQUIRE( passProjected["checks"][0]["severity"].asString() == "info" );
    REQUIRE( passProjected["checks"][0]["code"].asString().empty() );
}

TEST_CASE( "lab evidence projection mirrors the teaching shape", "[verify][render][F]" )
{
    VerificationCheckResult failing = result( "ndvi-range", "metric.range", VerificationStatus::Fail,
                                              kCodeMetricOutOfRange, "above max" );
    VerificationEvidence evidence;
    evidence.source = "metric:ndvi_mean";
    evidence.observed["value"] = 1.4;
    evidence.expected["max"] = 1.0;
    failing.evidence = evidence;

    const VerificationReport report = buildReport( "spec.lab", "node", std::string( 64, 'a' ),
                                                   { failing, result( "exists", "artifact.exists",
                                                                      VerificationStatus::Pass ) } );
    const Json::Value projected = labEvidenceFromReport( report );
    REQUIRE( projected["evidence"].size() == 2 );

    const Json::Value &first = projected["evidence"][0];
    REQUIRE( first["assertion_id"].asString() == "ndvi-range" );
    REQUIRE( first["kind"].asString() == "metric.range" );
    REQUIRE( first["passed"].asBool() == false );
    REQUIRE( first["observed"]["value"].asDouble() == 1.4 );
    REQUIRE( first["expected"]["max"].asDouble() == 1.0 );

    // No evidence carried: empty objects, never missing keys.
    const Json::Value &second = projected["evidence"][1];
    REQUIRE( second["passed"].asBool() == true );
    REQUIRE( second["observed"].isObject() );
    REQUIRE( second["observed"].empty() );
    REQUIRE( second["expected"].isObject() );
}
