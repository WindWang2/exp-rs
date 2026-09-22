// tests/test_verifier_schema.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice A: status lattice,
// versioned spec/check/evidence/report serde, structural validation
// (including kind-specific params and resource budgets), canonical JSON
// determinism and the SHA-256 digest primitive.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only — no Qt, no
// QGIS (the core is headless by design).

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "verify/verify_error_codes.h"
#include "verify/verify_sha256.h"
#include "verify/verify_status.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

VerificationCheckSpec makeCheck( const std::string &id, const std::string &kind,
                                 Json::Value params = Json::Value( Json::objectValue ) )
{
    VerificationCheckSpec check;
    check.checkId = id;
    check.kind = kind;
    check.params = std::move( params );
    return check;
}

VerificationSpec validSpec()
{
    VerificationSpec spec;
    spec.specId = "spec.preprocess.optical";
    spec.scope = "node";
    Json::Value metricParams( Json::objectValue );
    metricParams["metric"] = "ndvi_mean";
    metricParams["min"] = -1.0;
    metricParams["max"] = 1.0;
    spec.checks.push_back( makeCheck( "ndvi-range", "metric.range", metricParams ) );
    Json::Value existsParams( Json::objectValue );
    existsParams["path"] = "out.tif";
    spec.checks.push_back( makeCheck( "output-exists", "artifact.exists", existsParams ) );
    return spec;
}

} // namespace

// ---------------------------------------------------------------------------
// Status lattice
// ---------------------------------------------------------------------------

TEST_CASE( "status wire forms are closed and stable", "[verify][schema][A]" )
{
    REQUIRE( std::string( statusToWire( VerificationStatus::Pass ) ) == "pass" );
    REQUIRE( std::string( statusToWire( VerificationStatus::Fail ) ) == "fail" );
    REQUIRE( std::string( statusToWire( VerificationStatus::Indeterminate ) ) == "indeterminate" );
    REQUIRE( std::string( statusToString( VerificationStatus::Indeterminate ) ) == "Indeterminate" );
}

TEST_CASE( "aggregation lattice is fail-closed", "[verify][schema][A]" )
{
    using S = VerificationStatus;
    REQUIRE( aggregateStatus( {} ) == S::Indeterminate );
    REQUIRE( aggregateStatus( { S::Pass, S::Pass } ) == S::Pass );
    REQUIRE( aggregateStatus( { S::Pass, S::Fail } ) == S::Fail );
    REQUIRE( aggregateStatus( { S::Pass, S::Indeterminate } ) == S::Indeterminate );
    REQUIRE( aggregateStatus( { S::Indeterminate, S::Fail } ) == S::Fail );
    REQUIRE( aggregateStatus( { S::Fail, S::Pass, S::Indeterminate, S::Pass } ) == S::Fail );
}

// ---------------------------------------------------------------------------
// SHA-256 primitive
// ---------------------------------------------------------------------------

TEST_CASE( "sha256 matches RFC 6234 / NIST vectors", "[verify][schema][A]" )
{
    REQUIRE( sha256Hex( std::string( "" ) ) ==
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    REQUIRE( sha256Hex( std::string( "abc" ) ) ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    REQUIRE( sha256Hex( std::string( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" ) ) ==
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
    // 8-bit block boundary case: exactly one full 64-byte block.
    const std::string block64( 64, 'x' );
    REQUIRE( sha256Hex( block64 ).size() == 64 );
    // Million 'a' (cross-block streaming): NIST "aaaa..." vector.
    std::string million( 1000000, 'a' );
    REQUIRE( sha256Hex( million ) ==
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" );
}

// ---------------------------------------------------------------------------
// Spec serde + validation
// ---------------------------------------------------------------------------

TEST_CASE( "spec round-trips through canonical JSON", "[verify][schema][A]" )
{
    const VerificationSpec spec = validSpec();
    const Json::Value doc = specToJson( spec );
    REQUIRE( doc["schema"].asString() == "sicnu.verification.spec/1" );
    REQUIRE( doc["specId"].asString() == "spec.preprocess.optical" );
    REQUIRE( doc["scope"].asString() == "node" );
    REQUIRE( doc["checks"].size() == 2 );
    REQUIRE( doc["checks"][0]["checkId"].asString() == "ndvi-range" );
    REQUIRE( doc["checks"][0]["kind"].asString() == "metric.range" );
    REQUIRE( doc["checks"][0]["params"]["metric"].asString() == "ndvi_mean" );

    VerificationSpec parsed;
    std::string error;
    REQUIRE( specFromJson( doc, parsed, error ) );
    REQUIRE( parsed.specId == spec.specId );
    REQUIRE( parsed.scope == spec.scope );
    REQUIRE( parsed.checks.size() == spec.checks.size() );
    REQUIRE( parsed.checks[0].checkId == "ndvi-range" );
    REQUIRE( parsed.checks[0].params["min"].asDouble() == -1.0 );
    // Declaration order survives (renderers and reports depend on it).
    REQUIRE( parsed.checks[1].checkId == "output-exists" );
    // Round-trip stability: digest of parsed == digest of original.
    REQUIRE( specDigest( parsed ) == specDigest( spec ) );
}

TEST_CASE( "specFromJson strictly rejects schema violations", "[verify][schema][A]" )
{
    const Json::Value doc = specToJson( validSpec() );

    struct Bad
    {
        std::string what;
        std::function<Json::Value()> make;
    };
    const std::vector<Bad> cases = {
        { "wrong schema marker",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["schema"] = "sicnu.verification.spec/2";
              return d;
          } },
        { "unknown top-level field",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["extra"] = true;
              return d;
          } },
        { "unknown check field",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["checks"][0]["shortcut"] = true;
              return d;
          } },
        { "unknown kind",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["checks"][0]["kind"] = "metric.vibes";
              return d;
          } },
        { "unknown scope",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["scope"] = "universe";
              return d;
          } },
        { "missing specId",
          [] {
              Json::Value d = specToJson( validSpec() );
              d.removeMember( "specId" );
              return d;
          } },
        { "empty checkId",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["checks"][0]["checkId"] = "";
              return d;
          } },
        { "params not an object",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["checks"][0]["params"] = "ndvi_mean";
              return d;
          } },
        { "duplicate check ids",
          [] {
              Json::Value d = specToJson( validSpec() );
              d["checks"][1]["checkId"] = d["checks"][0]["checkId"];
              return d;
          } },
        { "non-object root",
          [] { return Json::Value( Json::arrayValue ); } },
    };

    for ( const auto &bad : cases )
    {
        INFO( "case: " << bad.what );
        VerificationSpec parsed;
        std::string error;
        REQUIRE_FALSE( specFromJson( bad.make(), parsed, error ) );
        REQUIRE_FALSE( error.empty() );
    }
}

TEST_CASE( "validateSpec enforces structure and kind params", "[verify][schema][A]" )
{
    SECTION( "valid spec validates clean" )
    {
        REQUIRE( validateSpec( validSpec() ).empty() );
    }

    SECTION( "empty checks rejected" )
    {
        VerificationSpec spec = validSpec();
        spec.checks.clear();
        const auto errors = validateSpec( spec );
        REQUIRE_FALSE( errors.empty() );
    }

    SECTION( "over-budget spec rejected" )
    {
        VerificationSpec spec = validSpec();
        Json::Value params( Json::objectValue );
        params["path"] = "a.tif";
        for ( int i = 0; i <= 256; ++i )
            spec.checks.push_back( makeCheck( "c" + std::to_string( i ), "artifact.exists", params ) );
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "duplicate ids rejected" )
    {
        VerificationSpec spec = validSpec();
        spec.checks[1].checkId = spec.checks[0].checkId;
        bool found = false;
        for ( const auto &e : validateSpec( spec ) )
            found = found || e.find( "duplicate" ) != std::string::npos;
        REQUIRE( found );
    }

    SECTION( "state.invariant: expectations required, ops closed, budget enforced" )
    {
        Json::Value params( Json::objectValue ); // no expectations
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "s1", "state.invariant", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["expectations"] = Json::Value( Json::arrayValue );
        Json::Value badOp( Json::objectValue );
        badOp["key"] = "stage";
        badOp["op"] = "resonates_with";
        badOp["value"] = "calibrated";
        params["expectations"].append( badOp );
        spec.checks = { makeCheck( "s1", "state.invariant", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["expectations"] = Json::Value( Json::arrayValue );
        for ( int i = 0; i <= 64; ++i )
        {
            Json::Value good( Json::objectValue );
            good["key"] = "k" + std::to_string( i );
            good["op"] = "present";
            params["expectations"].append( good );
        }
        spec.checks = { makeCheck( "s1", "state.invariant", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "artifact.exists: path required" )
    {
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "e1", "artifact.exists", Json::Value( Json::objectValue ) ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "artifact.type: kind vocabulary closed" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.gpkg";
        params["kind"] = "tensor";
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "t1", "artifact.type", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["kind"] = "vector";
        spec.checks = { makeCheck( "t1", "artifact.type", params ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "artifact.grid: vacuous (zero-constraint) check rejected" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.tif";
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "g1", "artifact.grid", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["width"] = 512;
        spec.checks = { makeCheck( "g1", "artifact.grid", params ) };
        REQUIRE( validateSpec( spec ).empty() );

        params["maxNodataFraction"] = 1.5; // out of [0,1]
        spec.checks = { makeCheck( "g1", "artifact.grid", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "artifact.schema: needs requiredKeys or schemaId" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "report.json";
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "sc1", "artifact.schema", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["requiredKeys"] = Json::Value( Json::arrayValue );
        params["requiredKeys"].append( "summary" );
        spec.checks = { makeCheck( "sc1", "artifact.schema", params ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "metric.range: bounds required and ordered" )
    {
        Json::Value params( Json::objectValue );
        params["metric"] = "kappa";
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "m1", "metric.range", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["min"] = 1.0;
        params["max"] = 0.0; // min > max
        spec.checks = { makeCheck( "m1", "metric.range", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["min"] = 0.0;
        params["max"] = 1.0;
        spec.checks = { makeCheck( "m1", "metric.range", params ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "relational.consistency: closed ops, sum_is shape" )
    {
        Json::Value params( Json::objectValue );
        params["relations"] = Json::Value( Json::arrayValue );
        Json::Value rel( Json::objectValue );
        rel["left"] = "area_before";
        rel["op"] = "morphism";
        rel["right"] = "area_after";
        params["relations"].append( rel );
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "r1", "relational.consistency", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        Json::Value sumRel( Json::objectValue );
        sumRel["left"] = "total";
        sumRel["op"] = "sum_is";
        Json::Value operands( Json::arrayValue );
        operands.append( "class_a" );
        operands.append( "class_b" );
        sumRel["right"] = operands;
        params["relations"] = Json::Value( Json::arrayValue );
        params["relations"].append( sumRel );
        spec.checks = { makeCheck( "r1", "relational.consistency", params ) };
        REQUIRE( validateSpec( spec ).empty() );

        // sum_is over the budget: rejected.
        operands = Json::Value( Json::arrayValue );
        for ( int i = 0; i <= 64; ++i )
            operands.append( "x" + std::to_string( i ) );
        sumRel["right"] = operands;
        params["relations"] = Json::Value( Json::arrayValue );
        params["relations"].append( sumRel );
        spec.checks = { makeCheck( "r1", "relational.consistency", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "provenance.complete: target and requirement both required" )
    {
        Json::Value params( Json::objectValue );
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "p1", "provenance.complete", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["path"] = "out.tif";
        spec.checks = { makeCheck( "p1", "provenance.complete", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() ); // still vacuous

        params["requiredFields"] = Json::Value( Json::arrayValue );
        params["requiredFields"].append( "operator" );
        spec.checks = { makeCheck( "p1", "provenance.complete", params ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "reproducibility.digest: one of two forms, digest hex-checked" )
    {
        Json::Value params( Json::objectValue );
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "d1", "reproducibility.digest", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["path"] = "out.tif";
        params["expectedDigest"] = "nothex";
        spec.checks = { makeCheck( "d1", "reproducibility.digest", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["expectedDigest"] = std::string( 64, 'a' );
        spec.checks = { makeCheck( "d1", "reproducibility.digest", params ) };
        REQUIRE( validateSpec( spec ).empty() );

        Json::Value pairParams( Json::objectValue );
        pairParams["leftPath"] = "a.tif";
        pairParams["rightPath"] = "b.tif";
        spec.checks = { makeCheck( "d1", "reproducibility.digest", pairParams ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "cross.output.consistency: >=2 outputs and >=1 constraint" )
    {
        Json::Value params( Json::objectValue );
        params["outputs"] = Json::Value( Json::arrayValue );
        Json::Value out( Json::objectValue );
        out["path"] = "a.tif";
        params["outputs"].append( out );
        VerificationSpec spec = validSpec();
        spec.checks = { makeCheck( "x1", "cross.output.consistency", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );

        params["outputs"].append( out );
        spec.checks = { makeCheck( "x1", "cross.output.consistency", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() ); // no constraint yet

        params["sameGrid"] = true;
        spec.checks = { makeCheck( "x1", "cross.output.consistency", params ) };
        REQUIRE( validateSpec( spec ).empty() );
    }

    SECTION( "error list is deterministic across calls" )
    {
        VerificationSpec spec = validSpec();
        spec.checks[0].checkId = "";
        const auto e1 = validateSpec( spec );
        const auto e2 = validateSpec( spec );
        REQUIRE( e1 == e2 );
        REQUIRE_FALSE( e1.empty() );
    }
}

TEST_CASE( "parseSpec wires bounded parse + schema + validation", "[verify][schema][A]" )
{
    const std::string text = canonicalJsonText( specToJson( validSpec() ) );
    VerificationSpec spec;
    std::string error;
    std::vector<std::string> validationErrors;
    REQUIRE( parseSpec( text, spec, error, validationErrors ) );
    REQUIRE( validationErrors.empty() );
    REQUIRE( spec.specId == "spec.preprocess.optical" );

    // Depth bomb: 20000 nested arrays must be a typed parse failure, not a
    // stack overflow (jsoncpp stackLimit guard).
    std::string bomb( 20000 * 2, '[' );
    REQUIRE_FALSE( parseSpec( bomb, spec, error, validationErrors ) );
    REQUIRE_FALSE( error.empty() );
}

// ---------------------------------------------------------------------------
// Canonical JSON + report
// ---------------------------------------------------------------------------

TEST_CASE( "canonical JSON sorts keys and formats doubles at 12 significant digits",
           "[verify][schema][A]" )
{
    Json::Value doc( Json::objectValue );
    doc["zebra"] = 1;
    doc["alpha"] = 2;
    doc["value"] = 0.1 + 0.2; // 0.30000000000000004 -> "0.3"
    const std::string text = canonicalJsonText( doc );
    const auto alphaPos = text.find( "\"alpha\"" );
    const auto zebraPos = text.find( "\"zebra\"" );
    REQUIRE( alphaPos != std::string::npos );
    REQUIRE( zebraPos != std::string::npos );
    REQUIRE( alphaPos < zebraPos );
    REQUIRE( text.find( "0.3" ) != std::string::npos );
    REQUIRE( text.find( "30000000000" ) == std::string::npos );
    // 0.1+0.2 and 0.3 canonically coincide -> identical digests.
    Json::Value other( Json::objectValue );
    other["zebra"] = 1;
    other["alpha"] = 2;
    other["value"] = 0.3;
    REQUIRE( canonicalJsonText( doc ) == canonicalJsonText( other ) );
}

TEST_CASE( "buildReport aggregates the lattice and counts", "[verify][schema][A]" )
{
    using S = VerificationStatus;
    std::vector<VerificationCheckResult> checks( 3 );
    checks[0].checkId = "c1";
    checks[0].kind = "metric.range";
    checks[0].status = S::Pass;
    checks[1].checkId = "c2";
    checks[1].kind = "artifact.exists";
    checks[1].status = S::Fail;
    checks[1].code = kCodeArtifactMissing;
    checks[2].checkId = "c3";
    checks[2].kind = "provenance.complete";
    checks[2].status = S::Indeterminate;
    checks[2].code = kCodeProvenanceMissing;

    const VerificationReport report = buildReport( "spec.x", "node", "d" + std::string( 63, '0' ), checks );
    REQUIRE( report.overall == S::Fail );
    REQUIRE( report.passCount() == 1 );
    REQUIRE( report.failCount() == 1 );
    REQUIRE( report.indeterminateCount() == 1 );
}

TEST_CASE( "report digest is deterministic, content-sensitive and self-verifying",
           "[verify][schema][A]" )
{
    using S = VerificationStatus;
    auto makeChecks = []( S second ) {
        std::vector<VerificationCheckResult> checks( 2 );
        checks[0].checkId = "c1";
        checks[0].kind = "metric.range";
        checks[0].status = S::Pass;
        checks[1].checkId = "c2";
        checks[1].kind = "artifact.exists";
        checks[1].status = second;
        if ( second == S::Fail )
            checks[1].code = kCodeArtifactMissing;
        return checks;
    };

    const VerificationReport a = buildReport( "spec.x", "node", std::string( 64, 'a' ), makeChecks( S::Pass ) );
    const VerificationReport b = buildReport( "spec.x", "node", std::string( 64, 'a' ), makeChecks( S::Pass ) );
    REQUIRE( a.digest() == b.digest() );
    REQUIRE( a.toCanonicalJson() == b.toCanonicalJson() );

    const VerificationReport c = buildReport( "spec.x", "node", std::string( 64, 'a' ), makeChecks( S::Fail ) );
    REQUIRE( a.digest() != c.digest() );

    // No wall clock in the body: the document has no timestamp-shaped field.
    const Json::Value doc = a.toCanonicalJson();
    REQUIRE( doc["schema"].asString() == "sicnu.verification.report/1" );
    REQUIRE( !doc.isMember( "generatedUtc" ) );
    REQUIRE( !doc.isMember( "timestamp" ) );
    REQUIRE( doc["overall"].asString() == "pass" );
    REQUIRE( doc["counts"]["pass"].asInt64() == 2 );
    REQUIRE( doc["checks"][1]["checkId"].asString() == "c2" );

    // Round-trip: canonical parse + digest re-verification accepts the
    // untampered report.
    VerificationReport parsed;
    std::string error;
    REQUIRE( VerificationReport::fromCanonicalJson( doc, parsed, error, a.digest() ) );
    REQUIRE( parsed.overall == S::Pass );
    REQUIRE( parsed.checks.size() == 2 );
    REQUIRE( parsed.checks[0].evidence == std::nullopt );

    // Tamper: flip a status in the JSON body -> digest mismatch is caught.
    Json::Value tampered = doc;
    tampered["checks"][1]["status"] = "fail";
    tampered["checks"][1]["code"] = kCodeArtifactMissing;
    REQUIRE_FALSE( VerificationReport::fromCanonicalJson( tampered, parsed, error, a.digest() ) );

    // Unknown field rejected at the report surface too.
    Json::Value extra = doc;
    extra["notes"] = "injected";
    REQUIRE_FALSE( VerificationReport::fromCanonicalJson( extra, parsed, error, a.digest() ) );
}

TEST_CASE( "evidence round-trips through canonical JSON", "[verify][schema][A]" )
{
    VerificationEvidence evidence;
    evidence.source = "grid:out.tif";
    evidence.observed["width"] = 512;
    evidence.observed["nodataFraction"] = 0.42;
    evidence.expected["maxNodataFraction"] = 0.30;
    evidence.hasSampling = true;
    evidence.sampledPoints = 4096;

    const Json::Value doc = evidence.toCanonicalJson();
    VerificationEvidence parsed;
    std::string error;
    REQUIRE( parsed.fromCanonicalJson( doc, error ) );
    REQUIRE( parsed.source == evidence.source );
    REQUIRE( parsed.observed["width"].asInt() == 512 );
    REQUIRE( parsed.sampledPoints == 4096 );
    REQUIRE( parsed.hasSampling );

    Json::Value extra = doc;
    extra["fabricated"] = 1;
    REQUIRE_FALSE( parsed.fromCanonicalJson( extra, error ) );
}

// ---------------------------------------------------------------------------
// Error-code vocabulary
// ---------------------------------------------------------------------------

TEST_CASE( "error code vocabulary classifies e_/i_ prefixes", "[verify][schema][A]" )
{
    REQUIRE( isVerifierCode( kCodeInvalidSpec ) );
    REQUIRE( isVerifierCode( kCodeMetricMissing ) );
    REQUIRE_FALSE( isVerifierCode( "verify:x_nope" ) );
    REQUIRE_FALSE( isVerifierCode( "" ) );
    REQUIRE_FALSE( isIndeterminateCode( kCodeInvalidSpec ) );
    REQUIRE( isIndeterminateCode( kCodeProviderMissing ) );
    REQUIRE( isIndeterminateCode( kCodeDigestUnavailable ) );
}

// ---------------------------------------------------------------------------
// Hostile shapes: typed refusal, never an escaping exception
// (hardening/verifier-grader-explain-evidence — fail-closed contract)
// ---------------------------------------------------------------------------

TEST_CASE( "hostile spec params are typed validation errors, not exceptions",
           "[verify][schema][hostile]" )
{
    VerificationSpec spec = validSpec();

    SECTION( "state.invariant op of object type" )
    {
        Json::Value params( Json::objectValue );
        params["expectations"] = Json::Value( Json::arrayValue );
        Json::Value expectation( Json::objectValue );
        expectation["key"] = "stage";
        expectation["op"] = Json::Value( Json::objectValue ); // hostile: not a string
        params["expectations"].append( expectation );
        spec.checks = { makeCheck( "s1", "state.invariant", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "relational.consistency op of array type" )
    {
        Json::Value params( Json::objectValue );
        params["relations"] = Json::Value( Json::arrayValue );
        Json::Value relation( Json::objectValue );
        relation["left"] = "a";
        relation["op"] = Json::Value( Json::arrayValue ); // hostile: not a string
        relation["right"] = "b";
        params["relations"].append( relation );
        spec.checks = { makeCheck( "r1", "relational.consistency", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "artifact.type kind of object type" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.gpkg";
        params["kind"] = Json::Value( Json::objectValue ); // hostile: not a string
        spec.checks = { makeCheck( "t1", "artifact.type", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "reproducibility.digest fingerprintAlgorithm of object type" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.tif";
        params["expectedDigest"] = std::string( 64, 'a' );
        params["fingerprintAlgorithm"] = Json::Value( Json::objectValue ); // hostile
        spec.checks = { makeCheck( "d1", "reproducibility.digest", params ) };
        REQUIRE_FALSE( validateSpec( spec ).empty() );
    }

    SECTION( "counts beyond int64 range are typed validation errors" )
    {
        SECTION( "artifact.exists minBytes" )
        {
            Json::Value params( Json::objectValue );
            params["path"] = "out.tif";
            params["minBytes"] = Json::Value( Json::UInt64( 1ULL ) << 63 );
            spec.checks = { makeCheck( "e1", "artifact.exists", params ) };
            REQUIRE_FALSE( validateSpec( spec ).empty() );
        }
        SECTION( "artifact.grid width must not throw on the numeric read" )
        {
            Json::Value params( Json::objectValue );
            params["path"] = "out.tif";
            params["width"] = Json::Value( Json::UInt64( 1ULL ) << 63 );
            spec.checks = { makeCheck( "g1", "artifact.grid", params ) };
            REQUIRE_FALSE( validateSpec( spec ).empty() );
        }
    }

    SECTION( "and the full parseSpec path stays typed end to end" )
    {
        const std::string text = R"({"schema":"sicnu.verification.spec/1","specId":"s",)"
                                 R"("scope":"node","checks":[{"checkId":"c1",)"
                                 R"("kind":"state.invariant","params":{"expectations":[)"
                                 R"({"key":"k","op":{"nested":true}}]}]})";
        VerificationSpec parsed;
        std::string error;
        std::vector<std::string> validationErrors;
        REQUIRE_FALSE( parseSpec( text, parsed, error, validationErrors ) );
        REQUIRE_FALSE( error.empty() );
    }
}

TEST_CASE( "hostile evidence shapes are typed refusals, not exceptions",
           "[verify][schema][hostile]" )
{
    VerificationEvidence evidence;
    evidence.source = "grid:out.tif";
    evidence.observed["width"] = 512;
    evidence.expected["width"] = 512;

    SECTION( "non-bool hasSampling refused" )
    {
        Json::Value doc = evidence.toCanonicalJson();
        doc["hasSampling"] = "yes"; // hostile: string
        VerificationEvidence parsed;
        std::string error;
        REQUIRE_FALSE( parsed.fromCanonicalJson( doc, error ) );
        REQUIRE_FALSE( error.empty() );
    }

    SECTION( "negative sampledPoints refused (no unsigned wrap-around)" )
    {
        Json::Value doc = evidence.toCanonicalJson();
        doc["hasSampling"] = true;
        doc["sampledPoints"] = -5; // hostile: negative
        VerificationEvidence parsed;
        std::string error;
        REQUIRE_FALSE( parsed.fromCanonicalJson( doc, error ) );
    }
}

TEST_CASE( "the canonical seal refuses non-finite bodies and non-hex spec digests",
           "[verify][schema][hostile]" )
{
    using S = VerificationStatus;

    SECTION( "an in-memory infinity makes the report unsealable, not NaN-as-null" )
    {
        std::vector<VerificationCheckResult> checks( 1 );
        checks[0].checkId = "c1";
        checks[0].kind = "artifact.grid";
        checks[0].status = S::Pass;
        VerificationEvidence evidence;
        evidence.source = "grid:out.tif";
        evidence.observed["nodataFraction"] = HUGE_VAL; // +inf, in-memory only
        checks[0].evidence = evidence;
        const VerificationReport report =
            buildReport( "spec.x", "node", std::string( 64, 'a' ), checks );
        // Pre-fix behavior sealed `1e+9999` (parser-divergent) with a valid
        // digest; the seal must refuse instead.
        CHECK( report.digest().empty() );
    }

    SECTION( "a non-finite body is refused even when no expectedDigest is supplied" )
    {
        // jsoncpp parses 1e999 to a non-finite double without a parse error;
        // the seal-skip path (empty expectedDigest) must still fail closed.
        std::vector<VerificationCheckResult> checks( 1 );
        checks[0].checkId = "c1";
        checks[0].kind = "artifact.grid";
        checks[0].status = S::Pass;
        const VerificationReport report =
            buildReport( "spec.x", "node", std::string( 64, 'a' ), checks );
        Json::Value hostile = report.toCanonicalJson();
        Json::Value evidence( Json::objectValue );
        evidence["source"] = "grid:out.tif";
        evidence["observed"]["nodataFraction"] = Json::Value( 1e999 );
        evidence["expected"] = Json::Value( Json::objectValue );
        hostile["checks"][0]["evidence"] = evidence;
        VerificationReport parsed;
        std::string error;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, parsed, error, "" ) );
        CHECK( error.find( "non-finite" ) != std::string::npos );
    }

    SECTION( "healthy reports keep a full digest" )
    {
        std::vector<VerificationCheckResult> checks( 1 );
        checks[0].checkId = "c1";
        checks[0].kind = "artifact.exists";
        checks[0].status = S::Pass;
        const VerificationReport report =
            buildReport( "spec.x", "node", std::string( 64, 'a' ), checks );
        CHECK( report.digest().size() == 64 );
    }

    SECTION( "a non-hex specDigest is refused on the wire" )
    {
        std::vector<VerificationCheckResult> checks( 1 );
        checks[0].checkId = "c1";
        checks[0].kind = "artifact.exists";
        checks[0].status = S::Pass;
        const VerificationReport report =
            buildReport( "spec.x", "node", std::string( 64, 'a' ), checks );
        Json::Value doc = report.toCanonicalJson();
        doc["specDigest"] = "not-a-digest";
        VerificationReport parsed;
        std::string error;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( doc, parsed, error, "" ) );
        CHECK( error.find( "specDigest" ) != std::string::npos );
    }
}

TEST_CASE( "hostile report counts are typed refusals, not exceptions",
           "[verify][schema][hostile]" )
{
    using S = VerificationStatus;
    std::vector<VerificationCheckResult> checks( 1 );
    checks[0].checkId = "c1";
    checks[0].kind = "artifact.exists";
    checks[0].status = S::Pass;
    const VerificationReport report = buildReport( "spec.x", "node", std::string( 64, 'a' ), checks );
    const Json::Value doc = report.toCanonicalJson();
    VerificationReport parsed;
    std::string error;

    SECTION( "string-typed count refused" )
    {
        Json::Value hostile = doc;
        hostile["counts"]["pass"] = "1";
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, parsed, error, "" ) );
        REQUIRE_FALSE( error.empty() );
    }

    SECTION( "object-typed count refused" )
    {
        Json::Value hostile = doc;
        hostile["counts"]["fail"] = Json::Value( Json::objectValue );
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, parsed, error, "" ) );
    }

    SECTION( "negative count refused" )
    {
        Json::Value hostile = doc;
        hostile["counts"]["indeterminate"] = -1;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, parsed, error, "" ) );
    }
}
