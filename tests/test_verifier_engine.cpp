// tests/test_verifier_engine.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice B: engine dispatch, the
// vocabulary-engine interlock, provider-missing semantics and the
// state.invariant / artifact.* check families.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
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

class FakeArtifactProbe : public IArtifactProbe
{
  public:
    std::map<std::string, ArtifactInfo> artifacts;
    std::map<std::string, Json::Value> documents;
    int probeCalls = 0;
    int readJsonCalls = 0;

    std::optional<ArtifactInfo> probe( const std::string &path ) override
    {
        ++probeCalls;
        const auto found = artifacts.find( path );
        if ( found == artifacts.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> readJson( const std::string &path ) override
    {
        ++readJsonCalls;
        const auto found = documents.find( path );
        if ( found == documents.end() )
            return std::nullopt;
        return found->second;
    }
};

class FakeGridProbe : public IGridProbe
{
  public:
    std::map<std::string, GridInfo> grids;
    int gridCalls = 0;

    std::optional<GridInfo> grid( const std::string &path ) override
    {
        ++gridCalls;
        const auto found = grids.find( path );
        if ( found == grids.end() )
            return std::nullopt;
        return found->second;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Vocabulary-engine interlock
// ---------------------------------------------------------------------------

TEST_CASE( "every declared check kind has an engine evaluator", "[verify][engine][B]" )
{
    for ( const std::string &kind : kCheckKinds )
        REQUIRE( hasEvaluator( kind ) );
    REQUIRE_FALSE( hasEvaluator( "artifact.vibes" ) );
    REQUIRE_FALSE( hasEvaluator( "" ) );
}

TEST_CASE( "an unknown kind never evaluates to a pass", "[verify][engine][B]" )
{
    // Direct dispatch bypassing validateSpec: the interlock backstop must
    // answer Indeterminate (capability gap), never Pass, never Fail.
    VerificationCheckSpec forged = makeCheck( "forged", "kind.nobody.implements" );
    const VerificationContext context;
    const VerificationCheckResult result = evaluateCheck( forged, context );
    REQUIRE( result.status == VerificationStatus::Indeterminate );
    REQUIRE( result.code == kCodeProviderMissing );
}

// ---------------------------------------------------------------------------
// Provider-missing is Indeterminate, never a pass
// ---------------------------------------------------------------------------

TEST_CASE( "an empty context drives every family to provider-missing", "[verify][engine][B]" )
{
    VerificationSpec spec;
    spec.specId = "spec.no.providers";
    spec.scope = "node";
    spec.checks.push_back( makeCheck( "st", "state.invariant", [ ] {
        Json::Value p( Json::objectValue );
        Json::Value expectations( Json::arrayValue );
        Json::Value e( Json::objectValue );
        e["key"] = "k";
        e["op"] = "present";
        expectations.append( e );
        p["expectations"] = expectations;
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "ex", "artifact.exists", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "out.tif";
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "gr", "artifact.grid", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "out.tif";
        p["width"] = 10;
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "me", "metric.range", [ ] {
        Json::Value p( Json::objectValue );
        p["metric"] = "m";
        p["max"] = 1.0;
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "pv", "provenance.complete", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "out.tif";
        Json::Value fields( Json::arrayValue );
        fields.append( "derivations" );
        p["requiredFields"] = fields;
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "rp", "reproducibility.digest", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "out.tif";
        p["expectedDigest"] = std::string( 64, 'a' );
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "co", "cross.output.consistency", [ ] {
        Json::Value p( Json::objectValue );
        Json::Value outputs( Json::arrayValue );
        Json::Value o1( Json::objectValue );
        o1["path"] = "a.tif";
        Json::Value o2( Json::objectValue );
        o2["path"] = "b.tif";
        outputs.append( o1 );
        outputs.append( o2 );
        p["outputs"] = outputs;
        p["sameGrid"] = true;
        return p;
    }() ) );

    REQUIRE( validateSpec( spec ).empty() );
    const VerificationContext context;
    const VerificationReport report = evaluate( spec, context );

    REQUIRE( report.overall == VerificationStatus::Indeterminate );
    REQUIRE( report.indeterminateCount() == 7 );
    REQUIRE( report.failCount() == 0 );
    for ( const VerificationCheckResult &check : report.checks )
    {
        REQUIRE( check.status == VerificationStatus::Indeterminate );
        REQUIRE( check.code == kCodeProviderMissing );
    }
}

// ---------------------------------------------------------------------------
// state.invariant
// ---------------------------------------------------------------------------

TEST_CASE( "state.invariant judges recorded facts fail-closed", "[verify][engine][B]" )
{
    FakeStateView state;
    state.values["stage"] = "complete";
    state.values["attempt"] = 3;
    state.values["ratio"] = 0.5;

    const auto present = [ & ]( const std::string &key, const std::string &op, Json::Value value ) {
        VerificationCheckSpec check = makeCheck( "st", "state.invariant" );
        Json::Value expectations( Json::arrayValue );
        Json::Value e( Json::objectValue );
        e["key"] = key;
        e["op"] = op;
        if ( !value.isNull() )
            e["value"] = value;
        expectations.append( e );
        check.params["expectations"] = expectations;
        return evaluateCheck( check, VerificationContext{ nullptr, nullptr, &state, nullptr, nullptr } );
    };

    SECTION( "present / absent" )
    {
        REQUIRE( present( "stage", "present", Json::Value() ).status == VerificationStatus::Pass );
        REQUIRE( present( "ghost", "absent", Json::Value() ).status == VerificationStatus::Pass );
        REQUIRE( present( "ghost", "present", Json::Value() ).status == VerificationStatus::Fail );
        REQUIRE( present( "ghost", "present", Json::Value() ).code == kCodeStateViolated );
        REQUIRE( present( "stage", "absent", Json::Value() ).status == VerificationStatus::Fail );
    }
    SECTION( "eq compares numbers by value: int 3 == real 3.0" )
    {
        REQUIRE( present( "attempt", "eq", Json::Value( 3.0 ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "ratio", "eq", Json::Value( 0.5 ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "stage", "eq", Json::Value( "complete" ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "stage", "eq", Json::Value( "partial" ) ).status == VerificationStatus::Fail );
    }
    SECTION( "ne and ordering" )
    {
        REQUIRE( present( "stage", "ne", Json::Value( "failed" ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "attempt", "gt", Json::Value( 2 ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "attempt", "le", Json::Value( 3 ) ).status == VerificationStatus::Pass );
        REQUIRE( present( "attempt", "lt", Json::Value( 3 ) ).status == VerificationStatus::Fail );
    }
    SECTION( "ordering against a non-numeric state value fails typed" )
    {
        const VerificationCheckResult result = present( "stage", "gt", Json::Value( 1 ) );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeTypeMismatch );
    }
    SECTION( "a non-finite observed value is indeterminate, never a pass" )
    {
        state.values["ratio"] = std::nan( "" );
        const VerificationCheckResult result = present( "ratio", "eq", Json::Value( 0.5 ) );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricNotFinite );
    }
    SECTION( "a non-finite observed value blocks gt/ne too" )
    {
        state.values["ratio"] = HUGE_VAL;
        REQUIRE( present( "ratio", "ne", Json::Value( 0.5 ) ).status == VerificationStatus::Indeterminate );
        REQUIRE( present( "ratio", "gt", Json::Value( 0 ) ).status == VerificationStatus::Indeterminate );
    }
}

TEST_CASE( "state.invariant folds multiple expectations fail-closed", "[verify][engine][B]" )
{
    FakeStateView state;
    state.values["a"] = 1;
    state.values["nan"] = std::nan( "" );

    VerificationCheckSpec check = makeCheck( "multi", "state.invariant" );
    Json::Value expectations( Json::arrayValue );
    Json::Value first( Json::objectValue );
    first["key"] = "nan";
    first["op"] = "eq";
    first["value"] = 1.0;
    Json::Value second( Json::objectValue );
    second["key"] = "a";
    second["op"] = "eq";
    second["value"] = 999;
    expectations.append( first );
    expectations.append( second );
    check.params["expectations"] = expectations;

    // Indeterminate (first) + Fail (second) folds to Fail — the lattice rule
    // applies inside one check, not only across the report.
    const VerificationCheckResult result =
        evaluateCheck( check, VerificationContext{ nullptr, nullptr, &state, nullptr, nullptr } );
    REQUIRE( result.status == VerificationStatus::Fail );
    REQUIRE( result.code == kCodeStateViolated );
    // The message names the FIRST blocking expectation (declaration order).
    REQUIRE( result.message.find( "nan" ) != std::string::npos );
}

TEST_CASE( "state keys with control characters are sanitized in messages", "[verify][engine][B]" )
{
    FakeStateView state;
    VerificationCheckSpec check = makeCheck( "st", "state.invariant" );
    Json::Value expectations( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["key"] = std::string( "bad\nkey" );
    e["op"] = "present";
    expectations.append( e );
    check.params["expectations"] = expectations;

    const VerificationCheckResult result =
        evaluateCheck( check, VerificationContext{ nullptr, nullptr, &state, nullptr, nullptr } );
    REQUIRE( result.status == VerificationStatus::Fail );
    REQUIRE( result.message.find( '\n' ) == std::string::npos );
}

// ---------------------------------------------------------------------------
// artifact.exists / artifact.type
// ---------------------------------------------------------------------------

TEST_CASE( "artifact.exists distinguishes missing, unreadable and small", "[verify][engine][B]" )
{
    FakeArtifactProbe artifacts;
    ArtifactInfo info;
    info.exists = true;
    info.kind = "raster";
    info.sizeBytes = 100;
    artifacts.artifacts["out.tif"] = info;
    // A definitive non-existence answer: the adapter can distinguish
    // "stat says absent" (missing) from "cannot answer" (unreadable).
    ArtifactInfo absent;
    absent.exists = false;
    artifacts.artifacts["ghost.tif"] = absent;

    const auto exists = [ & ]( const std::string &path, bool withMinBytes = false, int minBytes = 0 ) {
        VerificationCheckSpec check = makeCheck( "ex", "artifact.exists" );
        check.params["path"] = path;
        if ( withMinBytes )
            check.params["minBytes"] = minBytes;
        return evaluateCheck( check, VerificationContext{ &artifacts, nullptr, nullptr, nullptr, nullptr } );
    };

    REQUIRE( exists( "out.tif" ).status == VerificationStatus::Pass );
    REQUIRE( exists( "out.tif", true, 100 ).status == VerificationStatus::Pass );
    REQUIRE( exists( "out.tif", true, 101 ).status == VerificationStatus::Fail );
    REQUIRE( exists( "out.tif", true, 101 ).code == kCodeArtifactTooSmall );

    const VerificationCheckResult missing = exists( "ghost.tif" );
    REQUIRE( missing.status == VerificationStatus::Fail );
    REQUIRE( missing.code == kCodeArtifactMissing );

    // A path the probe cannot answer for is a capability/readability gap:
    // absent from the fake's map, probe() answers nullopt.
    const VerificationCheckResult unreadable = exists( "locked.tif" );
    REQUIRE( unreadable.status == VerificationStatus::Indeterminate );
    REQUIRE( unreadable.code == kCodeArtifactUnreadable );
}

TEST_CASE( "artifact.type pins the observed kind", "[verify][engine][B]" )
{
    FakeArtifactProbe artifacts;
    ArtifactInfo info;
    info.exists = true;
    info.kind = "table";
    artifacts.artifacts["cars.csv"] = info;

    VerificationCheckSpec check = makeCheck( "ty", "artifact.type" );
    check.params["path"] = "cars.csv";
    check.params["kind"] = "raster";
    const VerificationContext context{ &artifacts, nullptr, nullptr, nullptr, nullptr };

    const VerificationCheckResult mismatch = evaluateCheck( check, context );
    REQUIRE( mismatch.status == VerificationStatus::Fail );
    REQUIRE( mismatch.code == kCodeTypeMismatch );

    check.params["kind"] = "table";
    REQUIRE( evaluateCheck( check, context ).status == VerificationStatus::Pass );
}

// ---------------------------------------------------------------------------
// artifact.grid
// ---------------------------------------------------------------------------

TEST_CASE( "artifact.grid pins every declared constraint", "[verify][engine][B]" )
{
    FakeGridProbe grids;
    GridInfo grid;
    grid.width = 512;
    grid.height = 256;
    grid.bandCount = 3;
    grid.crs = "EPSG:32650";
    grid.nodataFraction = 0.1;
    grid.finiteFraction = 0.9;
    grids.grids["out.tif"] = grid;

    const auto gridCheck = [ & ]( Json::Value params ) {
        VerificationCheckSpec check = makeCheck( "gr", "artifact.grid" );
        check.params = std::move( params );
        return evaluateCheck( check, VerificationContext{ nullptr, &grids, nullptr, nullptr, nullptr } );
    };

    Json::Value all( Json::objectValue );
    all["path"] = "out.tif";
    all["width"] = 512;
    all["height"] = 256;
    all["bandCount"] = 3;
    all["crs"] = "EPSG:32650";
    all["maxNodataFraction"] = 0.2;
    all["minFiniteFraction"] = 0.8;
    REQUIRE( gridCheck( all ).status == VerificationStatus::Pass );

    SECTION( "dimension mismatch" )
    {
        all["width"] = 511;
        const VerificationCheckResult result = gridCheck( all );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeGridMismatch );
    }
    SECTION( "crs mismatch" )
    {
        all["crs"] = "EPSG:4326";
        REQUIRE( gridCheck( all ).code == kCodeGridMismatch );
    }
    SECTION( "fraction ceilings are inclusive" )
    {
        all["maxNodataFraction"] = 0.1;
        all["minFiniteFraction"] = 0.9;
        REQUIRE( gridCheck( all ).status == VerificationStatus::Pass );
        all["maxNodataFraction"] = 0.05;
        REQUIRE( gridCheck( all ).code == kCodeGridMismatch );
    }
    SECTION( "a NaN observed fraction is indeterminate, never a silent pass" )
    {
        grids.grids["out.tif"].nodataFraction = std::nan( "" );
        all["maxNodataFraction"] = 1.0;
        const VerificationCheckResult result = gridCheck( all );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricNotFinite );
    }
    SECTION( "an unprobeable grid is a readability gap" )
    {
        all["path"] = "ghost.tif";
        const VerificationCheckResult result = gridCheck( all );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeArtifactUnreadable );
    }
}

// ---------------------------------------------------------------------------
// artifact.schema
// ---------------------------------------------------------------------------

TEST_CASE( "artifact.schema pins marker and required keys", "[verify][engine][B]" )
{
    FakeArtifactProbe artifacts;
    ArtifactInfo info;
    info.exists = true;
    info.kind = "json";
    artifacts.artifacts["doc.json"] = info;
    Json::Value document( Json::objectValue );
    document["schema"] = "sicnu.example.v1";
    document["width"] = 10;
    artifacts.documents["doc.json"] = document;

    const auto schema = [ & ]( Json::Value params ) {
        VerificationCheckSpec check = makeCheck( "sc", "artifact.schema" );
        check.params = std::move( params );
        return evaluateCheck( check, VerificationContext{ &artifacts, nullptr, nullptr, nullptr, nullptr } );
    };

    Json::Value params( Json::objectValue );
    params["path"] = "doc.json";
    params["schemaId"] = "sicnu.example.v1";
    Json::Value keys( Json::arrayValue );
    keys.append( "schema" );
    keys.append( "width" );
    params["requiredKeys"] = keys;
    REQUIRE( schema( params ).status == VerificationStatus::Pass );

    SECTION( "wrong marker" )
    {
        params["schemaId"] = "sicnu.other.v1";
        const VerificationCheckResult result = schema( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeSchemaMismatch );
    }
    SECTION( "missing key" )
    {
        keys[1] = "height";
        params["requiredKeys"] = keys;
        REQUIRE( schema( params ).code == kCodeSchemaMismatch );
    }
    SECTION( "exists but not JSON is a detected schema violation" )
    {
        artifacts.documents.clear();
        const VerificationCheckResult result = schema( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeSchemaMismatch );
    }
}

// ---------------------------------------------------------------------------
// evaluate(): spec gate + resource bounds
// ---------------------------------------------------------------------------

TEST_CASE( "evaluate refuses an invalid spec with a synthetic spec.valid Fail",
           "[verify][engine][B]" )
{
    VerificationSpec spec;
    spec.specId = "spec.bad";
    spec.scope = "node";
    spec.checks.push_back( makeCheck( "v", "metric.range" ) ); // vacuous

    const VerificationContext context;
    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.checks.size() == 1 );
    REQUIRE( report.checks[0].checkId == "spec.valid" );
    REQUIRE( report.checks[0].kind == "spec.valid" );
    REQUIRE( report.checks[0].status == VerificationStatus::Fail );
    REQUIRE( report.checks[0].code == kCodeInvalidSpec );
    REQUIRE( report.overall == VerificationStatus::Fail );
}

TEST_CASE( "evaluate refuses an unsealable spec (non-finite in-memory params)",
           "[verify][engine][B]" )
{
    VerificationSpec spec;
    spec.specId = "spec.nan";
    spec.scope = "node";
    VerificationCheckSpec check = makeCheck( "st", "state.invariant" );
    Json::Value expectations( Json::arrayValue );
    Json::Value e( Json::objectValue );
    e["key"] = "k";
    e["op"] = "eq";
    e["value"] = std::nan( "" ); // not expressible in JSON text; smuggled in memory
    expectations.append( e );
    check.params["expectations"] = expectations;
    spec.checks.push_back( check );
    // validateSpec alone accepts it (value is non-null) — the engine's seal
    // gate is the honest refusal.
    REQUIRE( validateSpec( spec ).empty() );

    const VerificationContext context;
    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.checks.size() == 1 );
    REQUIRE( report.checks[0].checkId == "spec.valid" );
    REQUIRE( report.checks[0].status == VerificationStatus::Fail );
    REQUIRE( report.checks[0].code == kCodeInvalidSpec );
}

TEST_CASE( "a spec over the check budget is refused before any probe runs",
           "[verify][engine][B]" )
{
    VerificationSpec spec;
    spec.specId = "spec.bloated";
    spec.scope = "node";
    for ( std::size_t index = 0; index <= kMaxChecksPerSpec; ++index )
        spec.checks.push_back( makeCheck( "c" + std::to_string( index ), "artifact.exists", [ ] {
            Json::Value p( Json::objectValue );
            p["path"] = "out.tif";
            return p;
        }() ) );

    FakeArtifactProbe artifacts;
    const VerificationContext context{ &artifacts, nullptr, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.checks.size() == 1 );
    REQUIRE( report.checks[0].checkId == "spec.valid" );
    REQUIRE( report.checks[0].code == kCodeInvalidSpec );
    // The refusal happens before evaluation touches a single provider.
    REQUIRE( artifacts.probeCalls == 0 );
}

TEST_CASE( "the engine probes only what the spec names — no implicit scan",
           "[verify][engine][B]" )
{
    FakeArtifactProbe artifacts;
    FakeGridProbe grids;
    ArtifactInfo info;
    info.exists = true;
    artifacts.artifacts["a.tif"] = info;
    ArtifactInfo info2;
    info2.exists = true;
    artifacts.artifacts["b.tif"] = info2;

    VerificationSpec spec;
    spec.specId = "spec.bounded";
    spec.scope = "node";
    spec.checks.push_back( makeCheck( "a", "artifact.exists", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "a.tif";
        return p;
    }() ) );
    spec.checks.push_back( makeCheck( "b", "artifact.exists", [ ] {
        Json::Value p( Json::objectValue );
        p["path"] = "b.tif";
        return p;
    }() ) );

    const VerificationContext context{ &artifacts, &grids, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, context );
    REQUIRE( report.overall == VerificationStatus::Pass );
    // Exactly one probe per artifact check: two checks, two probe calls,
    // zero grid calls, zero readJson calls.
    REQUIRE( artifacts.probeCalls == 2 );
    REQUIRE( grids.gridCalls == 0 );
    REQUIRE( artifacts.readJsonCalls == 0 );
}
