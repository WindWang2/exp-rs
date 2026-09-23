// tests/test_verifier_adversarial.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice G: the adversarial battery.
// Hostile JSON, hostile in-memory params, the NaN/inf discipline, digest
// tamper-evidence, determinism/replay, the 81-permutation lattice fuzz,
// resource bounds and the mutation oracles that prove the tests can kill a
// wrong implementation.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "verify/projections.h"
#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_levels.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

VerificationCheckSpec check( const std::string &id, const std::string &kind, Json::Value params )
{
    VerificationCheckSpec spec;
    spec.checkId = id;
    spec.kind = kind;
    spec.params = std::move( params );
    return spec;
}

VerificationCheckResult result( const std::string &id, const std::string &kind, VerificationStatus status,
                                const std::string &code = "", const std::string &message = "" )
{
    VerificationCheckResult checkResult;
    checkResult.checkId = id;
    checkResult.kind = kind;
    checkResult.status = status;
    checkResult.code = code;
    checkResult.message = message;
    return checkResult;
}

Json::Value existsParams( const std::string &path )
{
    Json::Value params( Json::objectValue );
    params["path"] = path;
    return params;
}

/// The combined provider bundle shaped like the future real adapters: an
/// artifact/filesystem probe, a grid summarizer with bounded-sample facts,
/// an execution-state snapshot, a provenance document store and a metric
/// registry.
class FakeViews : public IArtifactProbe, public IGridProbe, public IStateView,
                  public IProvenanceView, public IMetricView
{
  public:
    std::map<std::string, ArtifactInfo> artifacts;
    std::map<std::string, GridInfo> grids;
    std::map<std::string, Json::Value> states;
    std::map<std::string, double> metrics;
    std::map<std::string, Json::Value> provenanceByPath;
    std::map<std::string, Json::Value> provenanceByRun;

    std::optional<ArtifactInfo> probe( const std::string &path ) override
    {
        const auto found = artifacts.find( path );
        if ( found == artifacts.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> readJson( const std::string & ) override { return std::nullopt; }
    std::optional<GridInfo> grid( const std::string &path ) override
    {
        const auto found = grids.find( path );
        if ( found == grids.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> state( const std::string &key ) override
    {
        const auto found = states.find( key );
        if ( found == states.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> provenanceForPath( const std::string &path ) override
    {
        const auto found = provenanceByPath.find( path );
        if ( found == provenanceByPath.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> provenanceForRun( const std::string &runId ) override
    {
        const auto found = provenanceByRun.find( runId );
        if ( found == provenanceByRun.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<double> metric( const std::string &name ) override
    {
        const auto found = metrics.find( name );
        if ( found == metrics.end() )
            return std::nullopt;
        return found->second;
    }

    /// One bundle implements every seam — the context binds it to all five.
    VerificationContext context() { return VerificationContext{ this, this, this, this, this }; }
};

// A fixed-seed xorshift for replay/mutation inputs — deterministic forever.
std::uint64_t next( std::uint64_t &state )
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

class CountingProbe : public IArtifactProbe
{
  public:
    std::map<std::string, ArtifactInfo> artifacts;
    int calls = 0;

    std::optional<ArtifactInfo> probe( const std::string &path ) override
    {
        ++calls;
        const auto found = artifacts.find( path );
        if ( found == artifacts.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> readJson( const std::string & ) override
    {
        ++calls;
        return std::nullopt;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Lattice fuzz: every aggregate of 4 statuses obeys the fail-closed lattice
// ---------------------------------------------------------------------------

TEST_CASE( "81-permutation lattice fuzz: aggregates are fail-closed", "[verify][adversarial][G]" )
{
    const VerificationStatus all[3] = { VerificationStatus::Pass, VerificationStatus::Fail,
                                        VerificationStatus::Indeterminate };
    for ( int a = 0; a < 3; ++a )
        for ( int b = 0; b < 3; ++b )
            for ( int c = 0; c < 3; ++c )
                for ( int d = 0; d < 3; ++d )
                {
                    const std::vector<VerificationStatus> statuses = { all[a], all[b], all[c], all[d] };
                    const bool anyFail = a + b + c + d > 0 && ( all[a] == VerificationStatus::Fail ||
                                                                all[b] == VerificationStatus::Fail ||
                                                                all[c] == VerificationStatus::Fail ||
                                                                all[d] == VerificationStatus::Fail );
                    const bool anyInd = all[a] == VerificationStatus::Indeterminate ||
                                        all[b] == VerificationStatus::Indeterminate ||
                                        all[c] == VerificationStatus::Indeterminate ||
                                        all[d] == VerificationStatus::Indeterminate;
                    const VerificationStatus expected = anyFail ? VerificationStatus::Fail
                                                        : anyInd ? VerificationStatus::Indeterminate
                                                                 : VerificationStatus::Pass;
                    REQUIRE( aggregateStatus( statuses ) == expected );

                    // The report lattice agrees, and the sealed body round-trips.
                    std::vector<VerificationCheckResult> checks;
                    for ( std::size_t index = 0; index < statuses.size(); ++index )
                    {
                        VerificationCheckResult check;
                        check.checkId = "c" + std::to_string( index );
                        check.kind = "artifact.exists";
                        check.status = statuses[index];
                        if ( statuses[index] == VerificationStatus::Fail )
                            check.code = kCodeArtifactMissing;
                        if ( statuses[index] == VerificationStatus::Indeterminate )
                            check.code = kCodeProviderMissing;
                        checks.push_back( check );
                    }
                    const VerificationReport report =
                        buildReport( "spec.fuzz", "node", std::string( 64, 'a' ), std::move( checks ) );
                    REQUIRE( report.overall == expected );

                    const std::string digest = report.digest();
                    REQUIRE_FALSE( digest.empty() );
                    const Json::Value body = report.toCanonicalJson();
                    VerificationReport resealed;
                    std::string error;
                    REQUIRE( VerificationReport::fromCanonicalJson( body, resealed, error, digest ) );
                    REQUIRE( resealed.overall == expected );
                }
}

// ---------------------------------------------------------------------------
// Hostile content: typed refusals, never escaping exceptions
// ---------------------------------------------------------------------------

TEST_CASE( "parseSpec bounds a 20k-deep bomb", "[verify][adversarial][G]" )
{
    const std::string bomb = "{\"schema\":\"sicnu.verification.spec/1\",\"specId\":\"s\","
                             "\"scope\":\"node\",\"checks\":" + std::string( 20000, '[' );
    VerificationSpec parsed;
    std::string error;
    std::vector<std::string> validationErrors;
    REQUIRE_FALSE( parseSpec( bomb, parsed, error, validationErrors ) );
    REQUIRE( error.find( "invalid JSON" ) != std::string::npos );
}

TEST_CASE( "evaluateCheck answers hostile params with typed refusals, never throws",
           "[verify][adversarial][G]" )
{
    const VerificationContext context;

    struct HostileCase
    {
        const char *kind;
        Json::Value params;
    };

    Json::Value existsWrong( Json::arrayValue );
    existsWrong.append( 1 );

    Json::Value gridWrong( Json::objectValue );
    gridWrong["path"] = 17;
    gridWrong["width"] = "many";

    Json::Value stateWrong( Json::objectValue );
    stateWrong["expectations"] = "present";

    Json::Value metricWrong( Json::objectValue );
    metricWrong["metric"] = Json::Value( Json::arrayValue );
    metricWrong["min"] = "low";

    Json::Value relationalWrong( Json::objectValue );
    Json::Value relations( Json::arrayValue );
    Json::Value rel( Json::objectValue );
    rel["op"] = 3;
    rel["left"] = Json::Value( Json::objectValue );
    rel["right"] = Json::Value( Json::objectValue );
    relations.append( rel );
    relationalWrong["relations"] = relations;

    Json::Value provenanceWrong( Json::objectValue );
    provenanceWrong["path"] = true;
    Json::Value fields( Json::objectValue );
    provenanceWrong["requiredFields"] = fields;

    Json::Value reproWrong( Json::objectValue );
    reproWrong["path"] = 9;
    reproWrong["expectedDigest"] = 42;

    Json::Value crossWrong( Json::objectValue );
    Json::Value outputs( Json::objectValue );
    crossWrong["outputs"] = outputs;
    crossWrong["sameGrid"] = "yes";

    const std::vector<HostileCase> cases = {
        { "artifact.exists", existsWrong },
        { "artifact.grid", gridWrong },
        { "state.invariant", stateWrong },
        { "metric.range", metricWrong },
        { "relational.consistency", relationalWrong },
        { "provenance.complete", provenanceWrong },
        { "reproducibility.digest", reproWrong },
        { "cross.output.consistency", crossWrong },
        { "artifact.exists", Json::Value( Json::nullValue ) },
        { "artifact.grid", Json::Value( Json::intValue ) },
    };

    for ( const HostileCase &hostile : cases )
    {
        INFO( "kind=" << hostile.kind << " params=" << hostile.params );
        VerificationCheckSpec spec = check( "h", hostile.kind, hostile.params );
        VerificationCheckResult result;
        REQUIRE_NOTHROW( result = evaluateCheck( spec, context ) );
        REQUIRE( result.status != VerificationStatus::Pass );
        REQUIRE( isVerifierCode( result.code ) );
    }
}

TEST_CASE( "the non-finite discipline holds across the engine", "[verify][adversarial][G]" )
{
    // Every kind that touches a non-finite observation answers Indeterminate
    // with the i_ class — the mutation being killed: a comparison that lets
    // NaN silently satisfy (or violate) a predicate.
    FakeViews views;
    views.metrics["m"] = std::nan( "" );
    views.grids["g.tif"].width = 10;
    views.grids["g.tif"].nodataFraction = std::nan( "" );
    views.states["k"] = Json::Value( -HUGE_VAL );

    const VerificationContext context = views.context();

    VerificationCheckSpec metric = check( "m1", "metric.range", [ & ] {
        Json::Value p( Json::objectValue );
        p["metric"] = "m";
        p["max"] = 1.0;
        return p;
    }() );
    REQUIRE( evaluateCheck( metric, context ).code == kCodeMetricNotFinite );

    VerificationCheckSpec grid = check( "g1", "artifact.grid", [ & ] {
        Json::Value p( Json::objectValue );
        p["path"] = "g.tif";
        p["maxNodataFraction"] = 0.5;
        return p;
    }() );
    REQUIRE( evaluateCheck( grid, context ).code == kCodeMetricNotFinite );

    VerificationCheckSpec state = check( "s1", "state.invariant", [ & ] {
        Json::Value p( Json::objectValue );
        Json::Value expectations( Json::arrayValue );
        Json::Value e( Json::objectValue );
        e["key"] = "k";
        e["op"] = "gt";
        e["value"] = 0;
        expectations.append( e );
        p["expectations"] = expectations;
        return p;
    }() );
    REQUIRE( evaluateCheck( state, context ).status == VerificationStatus::Indeterminate );
}

// ---------------------------------------------------------------------------
// Tamper-evidence
// ---------------------------------------------------------------------------

TEST_CASE( "digest tampering is refused in every direction", "[verify][adversarial][G]" )
{
    std::vector<VerificationCheckResult> checks;
    checks.push_back( result( "a", "artifact.exists", VerificationStatus::Pass ) );
    checks.push_back( result( "b", "metric.range", VerificationStatus::Fail, kCodeMetricOutOfRange, "high" ) );
    const VerificationReport report = buildReport( "spec.t", "node", std::string( 64, 'a' ), checks );
    const Json::Value body = report.toCanonicalJson();
    const std::string digest = report.digest();

    VerificationReport resealed;
    std::string error;

    SECTION( "message flipped, counts kept: the seal catches it" )
    {
        Json::Value hostile = body;
        hostile["checks"][1]["message"] = "fine actually";
        // Re-verify against the ORIGINAL digest: the mutated body must not
        // seal (this kills the "digest ignores message bytes" mutant).
        std::string error2;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, resealed, error2, digest ) );
        REQUIRE( error2.find( "digest mismatch" ) != std::string::npos );
        // The mutated body still parses (without a seal expectation) and
        // seals under its own digest — but that digest differs from the
        // original, so history cannot be rewritten silently.
        VerificationReport mutated;
        std::string error3;
        REQUIRE( VerificationReport::fromCanonicalJson( hostile, mutated, error3, "" ) );
        REQUIRE( mutated.digest() != digest );
    }
    SECTION( "status flipped while keeping its code: typed refusal" )
    {
        Json::Value hostile = body;
        hostile["checks"][1]["status"] = "pass";
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, resealed, error, "" ) );
        REQUIRE( error.find( "pass must not carry a code" ) != std::string::npos );
    }
    SECTION( "counts edited directly: derived-count refusal" )
    {
        Json::Value hostile = body;
        hostile["counts"]["pass"] = 5;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, resealed, error, "" ) );
        REQUIRE( error.find( "counts" ) != std::string::npos );
    }
    SECTION( "overall flipped: lattice refusal" )
    {
        Json::Value hostile = body;
        hostile["overall"] = "pass";
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, resealed, error, "" ) );
        REQUIRE( error.find( "overall" ) != std::string::npos );
    }
    SECTION( "a fully forged pass (status + counts) still breaks the overall lattice" )
    {
        Json::Value hostile = body;
        hostile["checks"][1]["status"] = "pass";
        hostile["checks"][1].removeMember( "code" );
        hostile["counts"]["fail"] = 0;
        hostile["counts"]["pass"] = 2;
        REQUIRE_FALSE( VerificationReport::fromCanonicalJson( hostile, resealed, error, "" ) );
        REQUIRE( error.find( "overall" ) != std::string::npos );
    }
}

TEST_CASE( "a report carrying non-finite evidence cannot be sealed", "[verify][adversarial][G]" )
{
    std::vector<VerificationCheckResult> checks;
    VerificationCheckResult ok = result( "a", "metric.range", VerificationStatus::Pass );
    VerificationEvidence evidence;
    evidence.source = "metric:m";
    evidence.observed["value"] = std::nan( "" );
    ok.evidence = evidence;
    checks.push_back( ok );

    const VerificationReport report = buildReport( "spec.nan", "node", std::string( 64, 'a' ), checks );
    // The refusal sentinel: no digest at all, never sha256("") pretending.
    REQUIRE( report.digest().empty() );
}

// ---------------------------------------------------------------------------
// Determinism / replay
// ---------------------------------------------------------------------------

TEST_CASE( "fixed-seed replay is byte-identical", "[verify][adversarial][G]" )
{
    for ( std::uint64_t seed = 1; seed <= 8; ++seed )
    {
        std::uint64_t state = seed;
        VerificationSpec spec;
        spec.specId = "spec.replay";
        spec.scope = "node";
        for ( int index = 0; index < 12; ++index )
        {
            const double bound = static_cast<double>( next( state ) % 1000 ) / 8.0;
            Json::Value params( Json::objectValue );
            params["metric"] = "m" + std::to_string( index % 3 );
            params["min"] = 0.0;
            params["max"] = bound;
            spec.checks.push_back( check( "c" + std::to_string( index ), "metric.range", params ) );
        }

        FakeViews views;
        for ( int index = 0; index < 3; ++index )
            views.metrics["m" + std::to_string( index )] =
                static_cast<double>( next( state ) % 2000 ) / 1000.0;

        const VerificationContext context = views.context();
        const std::string first = evaluate( spec, context ).digest();
        const std::string second = evaluate( spec, context ).digest();
        REQUIRE_FALSE( first.empty() );
        REQUIRE( first == second );
    }
}

// ---------------------------------------------------------------------------
// Mutation oracles — each assertion below kills a specific wrong mutation.
// ---------------------------------------------------------------------------

TEST_CASE( "mutation oracle: negative zero cannot fork the digest", "[verify][adversarial][G]" )
{
    Json::Value a( Json::objectValue );
    a["x"] = 0.0;
    Json::Value b( Json::objectValue );
    b["x"] = -0.0;

    // Kills the mutant "remove normalizeNegativeZero": the two texts — and
    // the digests — must be identical because 0.0 == -0.0.
    REQUIRE( canonicalJsonText( a ) == canonicalJsonText( b ) );
    REQUIRE( canonicalJsonText( b ).find( '-' ) == std::string::npos );
}

TEST_CASE( "mutation oracle: an evaluator removed from the dispatch table goes red",
           "[verify][adversarial][G]" )
{
    // Kills the mutant "add a kind to kCheckKinds without an evaluator":
    // the interlock must be able to see the gap.
    for ( const std::string &kind : kCheckKinds )
    {
        REQUIRE( hasEvaluator( kind ) );
        // And with NO providers attached, no kind can pass (kills the mutant
        // "dispatch defaults to Pass").
        const VerificationContext context;
        const VerificationCheckResult result = evaluateCheck( check( "x", kind, Json::Value( Json::objectValue ) ), context );
        REQUIRE( result.status != VerificationStatus::Pass );
    }
}

TEST_CASE( "mutation oracle: engine probes exactly what the spec names", "[verify][adversarial][G]" )
{
    // Kills the mutant "engine rescans other paths / walks beyond params".
    CountingProbe probe;
    ArtifactInfo info;
    info.exists = true;
    probe.artifacts["named.tif"] = info;

    VerificationSpec spec;
    spec.specId = "spec.bounded";
    spec.scope = "node";
    spec.checks.push_back( check( "one", "artifact.exists", existsParams( "named.tif" ) ) );

    const VerificationContext wired{ &probe, nullptr, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, wired );
    REQUIRE( report.overall == VerificationStatus::Pass );
    REQUIRE( probe.calls == 1 );
}

// ---------------------------------------------------------------------------
// Real-shaped integration: a whole node verification against a provider
// bundle shaped like the future real adapters
// ---------------------------------------------------------------------------

TEST_CASE( "real-shaped integration: node postcondition end-to-end with seal and tamper check",
           "[verify][adversarial][G]" )
{
    FakeViews views;
    // A raster the real adapter would summarize from a bounded sample.
    views.grids["out.tif"].width = 1024;
    views.grids["out.tif"].height = 1024;
    views.grids["out.tif"].bandCount = 1;
    views.grids["out.tif"].crs = "EPSG:32650";
    views.grids["out.tif"].nodataFraction = 0.02;
    views.grids["out.tif"].finiteFraction = 0.98;
    views.artifacts["out.tif"].exists = true;
    views.artifacts["out.tif"].kind = "raster";
    views.artifacts["out.tif"].digest = std::string( 64, '7' );
    views.artifacts["reproduce.tif"].exists = true;
    views.artifacts["reproduce.tif"].kind = "raster";
    views.artifacts["reproduce.tif"].digest = std::string( 64, '7' );
    views.metrics["ndvi_mean"] = 0.42;
    views.states["stage"] = Json::Value( "complete" );
    // exp-rs-prov/1-shaped provenance document.
    Json::Value provenance( Json::objectValue );
    provenance["schema"] = "exp-rs-prov/1";
    provenance["tool"] = "ndvi";
    Json::Value dimensions( Json::objectValue );
    dimensions["time"] = "2026-09-24";
    provenance["dimensions"] = dimensions;
    views.provenanceByPath["out.tif"] = provenance;

    VerificationSpec spec;
    spec.specId = "spec.ndvi.node";
    spec.scope = "node";
    {
        Json::Value params( Json::objectValue );
        Json::Value expectations( Json::arrayValue );
        Json::Value e( Json::objectValue );
        e["key"] = "stage";
        e["op"] = "eq";
        e["value"] = "complete";
        expectations.append( e );
        params["expectations"] = expectations;
        spec.checks.push_back( check( "stage", "state.invariant", params ) );
    }
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.tif";
        params["width"] = 1024;
        params["height"] = 1024;
        params["crs"] = "EPSG:32650";
        params["maxNodataFraction"] = 0.05;
        spec.checks.push_back( check( "grid", "artifact.grid", params ) );
    }
    {
        Json::Value params( Json::objectValue );
        params["metric"] = "ndvi_mean";
        params["min"] = -1.0;
        params["max"] = 1.0;
        spec.checks.push_back( check( "range", "metric.range", params ) );
    }
    {
        Json::Value params( Json::objectValue );
        params["path"] = "out.tif";
        Json::Value fields( Json::arrayValue );
        fields.append( "tool" );
        params["requiredFields"] = fields;
        Json::Value dims( Json::arrayValue );
        dims.append( "time" );
        params["requiredDimensions"] = dims;
        spec.checks.push_back( check( "prov", "provenance.complete", params ) );
    }
    {
        Json::Value params( Json::objectValue );
        params["leftPath"] = "out.tif";
        params["rightPath"] = "reproduce.tif";
        spec.checks.push_back( check( "repro", "reproducibility.digest", params ) );
    }

    const VerificationContext context = views.context();
    const VerificationReport report = verifyPlanNodePostcondition( spec, context );
    REQUIRE( report.overall == VerificationStatus::Pass );
    REQUIRE( report.checks.size() == 5 );

    // Persist + strict re-read: the seal survives the round trip.
    const std::string digest = report.digest();
    const Json::Value body = report.toCanonicalJson();
    VerificationReport resealed;
    std::string error;
    REQUIRE( VerificationReport::fromCanonicalJson( body, resealed, error, digest ) );

    // And the whole-task rollup over this node passes with it.
    VerificationSpec taskSpec;
    taskSpec.specId = "spec.ndvi.task";
    taskSpec.scope = "task";
    {
        Json::Value params( Json::objectValue );
        params["metric"] = "ndvi_mean";
        params["min"] = -1.0;
        params["max"] = 1.0;
        taskSpec.checks.push_back( check( "task.range", "metric.range", params ) );
    }
    const TaskOutcome outcome = verifyWholeTask( taskSpec, context, { report } );
    REQUIRE( outcome.overall == VerificationStatus::Pass );

    // Flip one observed fact at the source: verification must notice.
    views.metrics["ndvi_mean"] = 42.0;
    const VerificationReport diverged = verifyPlanNodePostcondition( spec, context );
    REQUIRE( diverged.overall == VerificationStatus::Fail );
    REQUIRE( diverged.checks[2].code == kCodeMetricOutOfRange );
}
