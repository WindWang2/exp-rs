// tests/test_r33_t05_redaction_delivery.cpp — R33-T05 WP11/WP13 gates.
//
// Trace redaction: the injected-fault slot keeps the scoring marker (the
// fault-kind STRING) verbatim, but an object/array fault subtree — or a
// secret-shaped / over-long fault string — still goes through redaction
// and the byte budget. A hostile "fault" must not smuggle credentials or
// unbounded bytes into an exportable trace.
//
// Delivery claims: a supplied verifier report with an unknown schema is
// present-but-unusable (never "absent" with a high-confidence claim), and
// externally supplied claims are labeled as external rather than reading
// as platform-verified.
#include <catch2/catch_test_macros.hpp>

#include "agent_ops/delivery_assembler.h"
#include "agent_ops/secret_redactor.h"

#include <json/json.h>

#include <string>

using namespace sicnu::agent_ops;

namespace
{

Json::Value parse( const std::string &text )
{
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr< Json::CharReader > reader( builder.newCharReader() );
    REQUIRE( reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) );
    return doc;
}

std::string stringify( const Json::Value &doc )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    return Json::writeString( builder, doc );
}

sicnu::agent_loop::SessionResult deliveredResult()
{
    sicnu::agent_loop::SessionResult result;
    result.sessionId = "sess-r33t05";
    result.summary.outcome = "delivered";
    result.summary.verificationVerdict = "PASS";
    result.terminalState = "delivered";
    return result;
}

} // namespace

TEST_CASE( "a synthetic secret inside a fault subtree is redacted, the scoring marker "
           "is not",
           "[agent_ops][r33t05][redaction]" )
{
    constexpr std::size_t kMaxChars = 4096;

    SECTION( "the fault-kind string marker survives verbatim" )
    {
        Json::Value doc = parse( R"({"fault":"transient_failure"})" );
        const Json::Value redacted = redactAndBound( doc, kMaxChars );
        REQUIRE( redacted[ "fault" ].isString() );
        REQUIRE( redacted[ "fault" ].asString() == "transient_failure" );
    }

    SECTION( "a fault OBJECT cannot smuggle secret slots" )
    {
        const Json::Value doc = parse(
            R"({"fault":{"kind":"transient_failure","api_key":"sk-supersecretvalue123",)"
            R"("nested":{"authorization":"Bearer abcdefghijklmnop"},"id":"f-1",)"
            R"("type":"tool","code":503}})" );
        const std::string text = stringify( redactAndBound( doc, kMaxChars ) );
        REQUIRE( text.find( "sk-supersecretvalue123" ) == std::string::npos );
        REQUIRE( text.find( "abcdefghijklmnop" ) == std::string::npos );
        REQUIRE( text.find( "[REDACTED]" ) != std::string::npos );
        // Identity and numeric evidence survive for scoring/diagnostics.
        const Json::Value back = parse( text );
        REQUIRE( back[ "fault" ][ "kind" ].asString() == "transient_failure" );
        REQUIRE( back[ "fault" ][ "id" ].asString() == "f-1" );
        REQUIRE( back[ "fault" ][ "type" ].asString() == "tool" );
        REQUIRE( back[ "fault" ][ "code" ].asInt() == 503 );
    }

    SECTION( "a fault object's over-long description is budgeted" )
    {
        Json::Value doc( Json::objectValue );
        doc[ "fault" ][ "kind" ] = "transient_failure";
        doc[ "fault" ][ "description" ] = std::string( 100000, 'd' );
        bool truncated = false;
        const Json::Value redacted = redactAndBound( doc, kMaxChars, &truncated );
        REQUIRE( truncated );
        REQUIRE( redacted[ "fault" ][ "description" ].isObject() );
        REQUIRE( redacted[ "fault" ][ "description" ][ "truncated" ].asBool() );
        REQUIRE( redacted[ "fault" ][ "kind" ].asString() == "transient_failure" );
    }

    SECTION( "a secret-shaped fault STRING is dropped, not preserved" )
    {
        Json::Value doc( Json::objectValue );
        doc[ "fault" ] = "bearer leaked-livetoken-abcdef";
        const std::string text = stringify( redactAndBound( doc, kMaxChars ) );
        REQUIRE( text.find( "leaked-livetoken" ) == std::string::npos );
    }

    SECTION( "an ordinary scientific digest outside fault stays untouched" )
    {
        Json::Value doc = parse(
            R"({"findings_digest":"0123456789abcdef0123456789abcdef"})" );
        const Json::Value redacted = redactAndBound( doc, kMaxChars );
        REQUIRE( redacted[ "findings_digest" ].asString()
                 == "0123456789abcdef0123456789abcdef" );
    }
}

TEST_CASE( "an unknown-schema verifier report is present-but-unusable, never absent",
           "[agent_ops][r33t05][delivery]" )
{
    DeliveryAssembler assembler;

    SECTION( "a future/unknown schema loses the high-confidence claim" )
    {
        DeliveryExtras extras;
        extras.verificationReport = parse(
            R"({"schema":"sicnu.verification.report/2","overall":"pass"})" );
        auto delivery = assembler.assemble( deliveredResult(), extras );
        REQUIRE( delivery.claims.size() == 1 );
        REQUIRE( delivery.claims[ 0 ][ "confidence" ].asDouble() == 0.0 );
        REQUIRE( delivery.claims[ 0 ][ "evidence_missing" ][ 0 ].asString()
                 == "passing_verifier_verdict" );
        REQUIRE( delivery.verifier[ "unified" ][ "verdict" ].asString() == "FAIL" );
        REQUIRE( delivery.verifier[ "unified" ][ "reason" ].asString()
                 == "unsupported_report_schema" );
    }

    SECTION( "the default empty report still means absent (legacy gate)" )
    {
        auto delivery = assembler.assemble( deliveredResult(), {} );
        REQUIRE( delivery.claims[ 0 ][ "confidence" ].asDouble() == 0.9 );
        REQUIRE_FALSE( delivery.verifier.isMember( "unified" ) );
    }

    SECTION( "a null report still means absent" )
    {
        DeliveryExtras extras;
        extras.verificationReport = Json::Value();
        auto delivery = assembler.assemble( deliveredResult(), extras );
        REQUIRE( delivery.claims[ 0 ][ "confidence" ].asDouble() == 0.9 );
        REQUIRE_FALSE( delivery.verifier.isMember( "unified" ) );
    }

    SECTION( "a valid report that passes keeps the high-confidence claim" )
    {
        DeliveryExtras extras;
        extras.verificationReport = parse(
            R"({"schema":"sicnu.verification.report/1","specId":"spec.p",)"
            R"("overall":"pass","counts":{"pass":2,"fail":0}})" );
        auto delivery = assembler.assemble( deliveredResult(), extras );
        REQUIRE( delivery.claims[ 0 ][ "confidence" ].asDouble() == 0.9 );
        REQUIRE( delivery.verifier[ "unified" ][ "verdict" ].asString() == "PASS" );
    }
}

TEST_CASE( "externally supplied claims are labeled external, platform claims platform",
           "[agent_ops][r33t05][delivery][claims]" )
{
    DeliveryAssembler assembler;

    DeliveryExtras extras;
    extras.claims = parse( R"([{"claim":"vendor_says","value":"success","confidence":0.9},)"
                           R"({"claim":"own_source","value":"x","source":"callback"},)"
                           R"({"claim":"session_outcome","value":"delivered","source":"platform"}])" );
    auto delivery = assembler.assemble( deliveredResult(), extras );
    REQUIRE( delivery.claims.size() == 3 );
    REQUIRE( delivery.claims[ 0 ][ "source" ].asString() == "external" );
    // A caller-declared source is preserved, not overwritten.
    REQUIRE( delivery.claims[ 1 ][ "source" ].asString() == "callback" );
    REQUIRE( delivery.claims[ 2 ][ "source" ].asString() == "external" );
    // The external claim's self-asserted confidence rode along verbatim.
    REQUIRE( delivery.claims[ 0 ][ "confidence" ].asDouble() == 0.9 );

    // The platform's own evidence-gated claim carries its source.
    auto platform = assembler.assemble( deliveredResult(), {} );
    REQUIRE( platform.claims.size() == 1 );
    REQUIRE( platform.claims[ 0 ][ "source" ].asString() == "platform" );

    // And the capsule export keeps the distinction on the wire.
    const Json::Value capsule = assembler.capsuleExportDocument( delivery );
    REQUIRE( capsule[ "claims" ][ 0 ][ "source" ].asString() == "external" );
    REQUIRE( capsule[ "claims" ][ 2 ][ "source" ].asString() == "external" );
}
