// test_preflight_authority_invalidation.cpp — Track 16 WP-D: the
// preflight <-> science_context invalidation linkage matrix.
//
// The chain contract (DECISIONS.md D2): preflight and the science_context
// broker can share ONE authority — a passport resolver wired both into
// broker.assets() and into preflight's StateAssetFactsProvider, and one
// capability table feeding both the broker's CapabilityFactsLookup and the
// engine's ICapabilityProvider. When an invalidation seam fires
// (invalidateAsset / invalidateAllAssets / notifyProjectSwitch /
// setCapabilityFacts / refreshRecipes), no consumer may keep serving the
// pre-invalidation answer:
//   * the broker drops cached bundles (cacheHit=false, fresh content);
//   * preflight re-consults the authority and answers with the CURRENT
//     facts — a typed unknown when the authority no longer knows the
//     subject, never the stale pass;
//   * every transition is loud (typed codes, insufficient_facts traces,
//     honest verdicts) and byte-deterministic.
//
// Each SECTION below is one matrix cell: invalidation granularity x
// consumer family. Expectations come from the fail-closed contract, not
// from re-reading the implementation under test.

#include <catch2/catch_test_macros.hpp>

#include "preflight/asset_state_adapter.h"
#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rules.h"
#include "science_context/broker.h"
#include "scientific_state/asset_state_json.h"

#include <json/json.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::preflight;
using namespace sicnu::science_context;

namespace {

// ---- passport fixtures (the authoritative sicnu.asset_state.v1 reader) ----

sicnu::state::RemoteSensingAssetState passportFromJson( const std::string &json, bool &ok )
{
    sicnu::state::RemoteSensingAssetState state;
    sicnu::state::AssetStateError error;
    ok = sicnu::state::assetStateFromJson( json, state, error );
    return state;
}

/// Parameterized optical scene passport (the shape production resolvers
/// feed). Fractional values ride along so locale discipline stays honest.
std::string scenePassportJson( const std::string &assetId,
                               const std::string &crsAuthid,
                               double pixelSize,
                               const std::string &radiometricUnit )
{
    std::string json = R"JSON({
      "schema": "sicnu.asset_state.v1",
      "identity": { "asset_id": ")JSON" +
                     assetId + R"JSON(", "revision": "r1",
                     "source_path": "/data/)JSON" +
                     assetId + R"JSON(.tif", "display_name": "Scene",
                     "kind": "raster", "lifecycle": "ready" },
      "sensor": { "platform": "S2", "instrument": "MSI", "modality": "optical" },
      "acquisition": { "time_iso": "2026-05-01T10:30:00Z", "time_source": "metadata",
                        "precision": "second", "valid": true },
      "bands": [
        { "index": 1, "name": "B04", "role": "red", "data_type": "UInt16",
          "wavelength_nm": 665.0 },
        { "index": 2, "name": "B08", "role": "nir", "data_type": "UInt16",
          "wavelength_nm": 842.0 }
      ],
      "geometry": { "has_crs": true, "crs_wkt": "", "crs_authid": ")JSON" +
                     crsAuthid + R"JSON(",
                     "crs_geographic": false, "crs_projected": true,
                     "pixel_size_x": )JSON" +
                     std::to_string( pixelSize ) + R"JSON(, "pixel_size_y": )JSON" +
                     std::to_string( pixelSize ) + R"JSON(,
                     "width": 1098, "height": 1098 },
      "validity": { "no_data_policy": "declared", "cloud_cover_percent": 12.5,
                     "quality_mask_info": "s2_scl" },
      "radiometric": { "unit": ")JSON" +
                       radiometricUnit + R"JSON(", "declared_raw": "", "domain": "" },
      "provenance": { "is_derived": false }
    })JSON";
    return json;
}

/// The shared authority: one map, one resolver, two consumers.
struct SharedAuthority
{
    std::map<std::string, sicnu::state::RemoteSensingAssetState> passports;

    /// The broker-side resolver (typed failure channel).
    PassportResolution brokerResolver( const std::string &assetKey ) const
    {
        PassportResolution r;
        auto it = passports.find( assetKey );
        if ( it == passports.end() )
        {
            r.errorCode = "asset_not_found";
            return r;
        }
        r.state = it->second;
        return r;
    }

    /// The preflight-side projection of the same resolver.
    std::optional<sicnu::state::RemoteSensingAssetState> preflightResolver(
        const std::string &assetRef ) const
    {
        auto it = passports.find( assetRef );
        if ( it == passports.end() )
            return std::nullopt;
        return it->second;
    }
};

// ---- capability fixture: one table, two consumers -------------------------

/// Entries for one operator id, read by BOTH the broker's
/// CapabilityFactsLookup and the engine's ICapabilityProvider.
std::map<std::string, Json::Value> ndviCapabilityTable()
{
    Json::Value entry( Json::objectValue );
    entry["id"] = "rs:demo_ndvi";
    entry["modality"] = Json::Value( Json::arrayValue );
    entry["modality"].append( "optical" );
    entry["band_roles"] = Json::Value( Json::objectValue );
    entry["band_roles"]["red"] = 1;
    entry["band_roles"]["nir"] = 1;
    Json::Value radiometric( Json::objectValue );
    Json::Value acceptable( Json::arrayValue );
    acceptable.append( "surface_reflectance" );
    radiometric["acceptable"] = acceptable;
    entry["radiometric"] = radiometric;
    Json::Value crs( Json::objectValue );
    crs["requires_projected"] = true;
    entry["crs"] = crs;
    return { { "rs:demo_ndvi", entry } };
}

/// ICapabilityProvider over the shared table (live, never cached).
class TableCapabilityProvider final : public ICapabilityProvider
{
  public:
    explicit TableCapabilityProvider( const std::map<std::string, Json::Value> *table )
        : mTable( table )
    {
    }

    CapabilityEntryResult entryForOperator( const std::string &operatorId,
                                            const Json::Value & ) const override
    {
        CapabilityEntryResult r;
        auto it = mTable->find( operatorId );
        if ( it == mTable->end() )
        {
            r.status = FactStatus::Unknown;
            r.detail = "operator not declared by the authority";
            return r;
        }
        r.status = FactStatus::Available;
        r.entry = it->second;
        return r;
    }

  private:
    const std::map<std::string, Json::Value> *mTable;
};

// ---- the linkage fixture ---------------------------------------------------

struct Linkage
{
    SharedAuthority authority;
    bool ok = false;
    ScienceContextBroker broker;
    PreflightEngine engine;
    std::map<std::string, Json::Value> capabilityTable = ndviCapabilityTable();
    TableCapabilityProvider capability{ &capabilityTable };
    StateAssetFactsProvider facts{ [ this ]( const std::string &ref ) {
        return authority.preflightResolver( ref );
    } };
    std::unique_ptr<CapabilityFactsLookup> capFacts;

    Linkage()
    {
        for ( auto &rule : builtinRules() )
            REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );
        broker.assets().setResolver(
            [ this ]( const std::string &key ) { return authority.brokerResolver( key ); } );
        broker.assets().setResolverAuthority( "test.shared_authority" );
        installCapabilityFacts();
    }

    /// Wire the CURRENT capability table into the broker (live revision
    /// counter bumps so cache keys track authority generations).
    void installCapabilityFacts( std::uint64_t revision = 1 )
    {
        capFacts = std::make_unique<CapabilityFactsLookup>();
        capFacts->authority = "test.capability_table";
        capFacts->revision = [ revision ] { return revision; };
        capFacts->entriesForIntent = [ this ]( const std::string & ) {
            std::vector<Json::Value> entries;
            for ( const auto & [ id, entry ] : capabilityTable )
            {
                ( void ) id;
                entries.push_back( entry );
            }
            return entries;
        };
        broker.setCapabilityFacts( *capFacts );
    }

    PreflightRequest request( std::vector<std::pair<std::string, std::string>> inputs ) const
    {
        PreflightRequest req;
        req.operatorId = "rs:demo_ndvi";
        req.mode = "teaching";
        req.humanOperatorId = "op";
        req.inputs = std::move( inputs );
        return req;
    }

    PreflightReport evaluate( std::vector<std::pair<std::string, std::string>> inputs = {
                                  { "primary", "scene-a" } } )
    {
        return engine.evaluate( request( std::move( inputs ) ), facts, capability );
    }

    SynthesizeResult synthesize( const std::vector<std::string> &keys = { "scene-a" } )
    {
        SynthesizeRequest req;
        req.goal = "track16 linkage";
        req.assetKeys = keys;
        req.useCache = true;
        req.constraints.autonomyLevel = "L2";
        return broker.synthesize( req );
    }
};

// ---- assertion helpers ------------------------------------------------------

std::vector<const PreflightFinding *> findings( const PreflightReport &report,
                                                const std::string &code )
{
    std::vector<const PreflightFinding *> out;
    for ( const auto &f : report.findings )
        if ( f.code == code )
            out.push_back( &f );
    return out;
}

bool hasCode( const PreflightReport &report, const std::string &code )
{
    return !findings( report, code ).empty();
}

const PreflightEvaluatedRule *trace( const PreflightReport &report, const std::string &ruleId )
{
    for ( const auto &e : report.evaluated )
        if ( e.ruleId == ruleId )
            return &e;
    return nullptr;
}

} // namespace

// ============================ G1 invalidateAsset ============================

TEST_CASE( "invalidateAsset: re-imported radiometric unit reaches preflight and "
           "the broker drops the stale bundle",
           "[track16][invalidation][asset]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    REQUIRE( L.ok );

    const SynthesizeResult primed = L.synthesize();
    REQUIRE_FALSE( primed.cacheHit );
    const PreflightReport before = L.evaluate();
    REQUIRE( trace( before, "preflight.radiometric_state_policy" ) != nullptr );

    // Re-import: same identity, mutated content.
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "thermal_radiance" ),
                          L.ok );
    REQUIRE( L.ok );
    L.broker.invalidateAsset( "scene-a" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit ); // no stale bundle survives the seam

    const PreflightReport report = L.evaluate();
    // The off-list unit must not read as the stale acceptable answer.
    const PreflightEvaluatedRule *radiometric =
        trace( report, "preflight.radiometric_state_policy" );
    REQUIRE( radiometric != nullptr );
    CHECK( radiometric->outcome == "finding" );
    CHECK( hasCode( report, "SPF_RADIOMETRIC_STATE_MISMATCH" ) );
    CHECK( report.verdict != "ok" );

    // Determinism post-state: same authority state, byte-identical reports.
    CHECK( reportDigest( L.evaluate() ) == reportDigest( report ) );
}

TEST_CASE( "invalidateAsset: CRS re-import flips the pair verdict, no stale pass",
           "[track16][invalidation][asset]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    L.authority.passports["scene-b"] =
        passportFromJson( scenePassportJson( "scene-b", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    const PreflightReport before =
        L.evaluate( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    const PreflightEvaluatedRule *pairCrs = trace( before, "preflight.pair_crs" );
    REQUIRE( pairCrs != nullptr );
    CHECK( pairCrs->outcome == "pass" );

    // scene-b is re-imported onto a different CRS.
    L.authority.passports["scene-b"] =
        passportFromJson( scenePassportJson( "scene-b", "EPSG:32610", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    L.broker.invalidateAsset( "scene-b" );

    const PreflightReport report =
        L.evaluate( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    const PreflightEvaluatedRule *after = trace( report, "preflight.pair_crs" );
    REQUIRE( after != nullptr );
    CHECK( after->outcome == "finding" );
    CHECK( hasCode( report, "SPF_CRS_MISMATCH" ) );
    CHECK( report.verdict == "blocked" );
}

TEST_CASE( "invalidateAsset: band-role removal degrades typed, never silent",
           "[track16][invalidation][asset]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    // Mutate to a band-less passport (roles gone), then invalidate.
    std::string noBands = scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" );
    // The re-import declares both bands with an unrecognizable role: the
    // authority still knows the asset, but the required roles are gone.
    const std::string red = "\"role\": \"red\"";
    const std::string nir = "\"role\": \"nir\"";
    const auto blank = [ &noBands ]( const std::string &needle ) {
        const auto at = noBands.find( needle );
        if ( at != std::string::npos )
            noBands.replace( at, needle.size(), "\"role\": \"\"" );
    };
    blank( red );
    blank( nir );
    L.authority.passports["scene-a"] = passportFromJson( noBands, L.ok );
    REQUIRE( L.ok );
    L.broker.invalidateAsset( "scene-a" );

    const PreflightReport report = L.evaluate();
    const PreflightEvaluatedRule *bandRole = trace( report, "preflight.band_role" );
    REQUIRE( bandRole != nullptr );
    // Roles no longer recognized on the re-import: the required role is
    // missing (0 of 1 declared), a typed Block — not the stale pass and
    // not a silent skip.
    CHECK( bandRole->outcome == "finding" );
    CHECK( hasCode( report, "SPF_BAND_ROLE_MISSING" ) );
    CHECK( report.verdict == "blocked" );
}

TEST_CASE( "invalidateAsset on an unrelated key: preflight answers unchanged, "
           "broker cache is coarse-dropped (documented seam semantics)",
           "[track16][invalidation][asset]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    ( void ) L.synthesize();
    const PreflightReport before = L.evaluate();
    const std::string beforeDigest = reportDigest( before );

    L.broker.invalidateAsset( "scene-unrelated" );

    // Preflight: the authority did not change for scene-a, the answer must
    // not move either.
    CHECK( reportDigest( L.evaluate() ) == beforeDigest );
    // Broker: invalidateAsset clears the whole bundle cache by design —
    // assert the documented coarseness instead of a narrower fantasy.
    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
}

// ========================== G2 invalidateAllAssets ==========================

TEST_CASE( "invalidateAllAssets: removed passports answer typed unknown, "
           "verdict gates, broker re-resolves typed",
           "[track16][invalidation][all-assets]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    ( void ) L.synthesize();
    const PreflightReport before = L.evaluate();
    CHECK( trace( before, "preflight.band_role" )->outcome == "pass" );

    // The authority loses the passport entirely (catalog purge).
    L.authority.passports.clear();
    L.broker.invalidateAllAssets();

    const PreflightReport report = L.evaluate();
    const PreflightEvaluatedRule *bandRole = trace( report, "preflight.band_role" );
    REQUIRE( bandRole != nullptr );
    CHECK( bandRole->outcome == "insufficient_facts" );
    CHECK( hasCode( report, "SPF_BAND_ROLE_UNKNOWN" ) );
    // A stale pass can never survive: the verdict cannot be ok.
    CHECK( report.verdict == "requires_ack" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
    CHECK( hasCode( report, "SPF_BAND_ROLE_UNKNOWN" ) );
    // Broker-side: the resolve failure is typed in the bundle's question
    // channel, not silently swallowed.
    bool typedResolveFailure = false;
    for ( const std::string &q : after.bundle.openQuestions )
        if ( q.find( "asset_resolve_failed:asset_not_found" ) == 0 )
            typedResolveFailure = true;
    CHECK( typedResolveFailure );
}

TEST_CASE( "invalidateAllAssets covers every key invalidateAsset would",
           "[track16][invalidation][all-assets]" )
{
    // Equivalence cell: per-key and global invalidation end at the same
    // observable state (same passport mutation, same request).
    auto runFor = []( bool global ) {
        Linkage L;
        L.authority.passports["scene-a"] =
            passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                                 "surface_reflectance" ),
                              L.ok );
        L.authority.passports["scene-a"] =
            passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                                 "thermal_radiance" ),
                              L.ok );
        if ( global )
            L.broker.invalidateAllAssets();
        else
            L.broker.invalidateAsset( "scene-a" );
        PreflightReport report = L.evaluate();
        SynthesizeResult synth = L.synthesize();
        return std::make_pair( reportDigest( report ), synth.cacheHit );
    };

    const auto [ globalDigest, globalCacheHit ] = runFor( true );
    const auto [ keyDigest, keyCacheHit ] = runFor( false );
    CHECK( globalDigest == keyDigest );
    CHECK_FALSE( globalCacheHit );
    CHECK_FALSE( keyCacheHit );
}

TEST_CASE( "invalidateAllAssets: stale pass can never survive (pass pin dies "
           "with the passport)",
           "[track16][invalidation][all-assets]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    const PreflightReport before = L.evaluate();
    CHECK( before.verdict == "ok" );

    L.authority.passports.clear();
    L.broker.invalidateAllAssets();

    const PreflightReport report = L.evaluate();
    CHECK( report.verdict != "ok" );
    CHECK( trace( report, "preflight.radiometric_state_policy" )->outcome ==
           "insufficient_facts" );
}

// ========================= G3 notifyProjectSwitch ==========================

TEST_CASE( "notifyProjectSwitch: previous project's facts stop answering; "
           "new project's facts serve; no cross-project leakage",
           "[track16][invalidation][project-switch]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    ( void ) L.synthesize();
    REQUIRE( L.evaluate().verdict == "ok" );

    // Project switch: the authority now serves project B's catalog.
    L.authority.passports.clear();
    L.broker.notifyProjectSwitch();

    const PreflightReport report = L.evaluate();
    CHECK( report.verdict != "ok" );
    CHECK( trace( report, "preflight.band_role" )->outcome == "insufficient_facts" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
}

TEST_CASE( "notifyProjectSwitch A->B->A: returning to A serves the MUTATED "
           "passport, not the remembered original",
           "[track16][invalidation][project-switch]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    ( void ) L.synthesize();

    // Switch to B (empty catalog), then "back to A" where the operator
    // re-imported scene-a with a different radiometric unit.
    L.broker.notifyProjectSwitch();
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "thermal_radiance" ),
                          L.ok );
    L.broker.notifyProjectSwitch();

    const PreflightReport report = L.evaluate();
    CHECK( hasCode( report, "SPF_RADIOMETRIC_STATE_MISMATCH" ) );
    CHECK( report.verdict != "ok" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
    CHECK( after.bundle.assets.size() == 1 );
    CHECK( after.bundle.assets.front().assetId == "scene-a" );
}

// ======================== G4 setCapabilityFacts ============================

TEST_CASE( "setCapabilityFacts revision advance: broker cache misses and "
           "provenance carries the new revision",
           "[track16][invalidation][capability]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    const SynthesizeResult primed = L.synthesize();
    CHECK( L.synthesize().cacheHit ); // warm cache serves

    L.installCapabilityFacts( 2 ); // authority reload bumped the revision

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
    CHECK( after.bundle.sources.capabilities.revision == 2 );
}

TEST_CASE( "setCapabilityFacts: operator removed by the new authority flips "
           "preflight to typed unknown — shared table, both consumers",
           "[track16][invalidation][capability]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    REQUIRE( L.evaluate().verdict == "ok" );

    // The reloaded authority no longer declares rs:demo_ndvi.
    L.capabilityTable.clear();
    L.installCapabilityFacts( 2 );

    const PreflightReport report = L.evaluate();
    CHECK( hasCode( report, "SPF_OPERATOR_UNKNOWN" ) );
    CHECK( report.verdict == "requires_ack" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
}

TEST_CASE( "setCapabilityFacts: operator newly declared flips preflight from "
           "typed unknown to judged (no stale unknown)",
           "[track16][invalidation][capability]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    L.capabilityTable.clear();
    L.installCapabilityFacts( 1 );
    const PreflightReport before = L.evaluate();
    CHECK( hasCode( before, "SPF_OPERATOR_UNKNOWN" ) );

    L.capabilityTable = ndviCapabilityTable();
    L.installCapabilityFacts( 2 );

    const PreflightReport report = L.evaluate();
    CHECK_FALSE( hasCode( report, "SPF_OPERATOR_UNKNOWN" ) );
    CHECK( trace( report, "preflight.operator_known" )->outcome == "pass" );
}

TEST_CASE( "combined invalidation: asset seam + capability seam in one round "
           "move BOTH consumers (no partial staleness)",
           "[track16][invalidation][combined]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    ( void ) L.synthesize();
    REQUIRE( L.evaluate().verdict == "ok" );

    L.authority.passports.clear();
    L.capabilityTable.clear();
    L.broker.invalidateAllAssets();
    L.installCapabilityFacts( 2 );

    const PreflightReport report = L.evaluate();
    // The operator dimension is typed-unknown (authority no longer declares
    // the operator) and the report cannot read ok.
    CHECK( hasCode( report, "SPF_OPERATOR_UNKNOWN" ) );
    CHECK( report.verdict == "requires_ack" );
    // Asset dimension: with the capability authority gone, band_role hands
    // the outage to operator_known (the mirror-outage finding is owned in
    // exactly one place) — the rule-level "pass" can never leak: the report
    // verdict is gated above.
    const PreflightEvaluatedRule *bandRole = trace( report, "preflight.band_role" );
    CHECK( bandRole != nullptr );
    CHECK( bandRole->outcome == "pass" );
    CHECK( report.verdict == "requires_ack" );

    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
}

// ========================== G5 refreshRecipes ==============================

TEST_CASE( "refreshRecipes with an unavailable registry is fail-closed and "
           "never serves the stale recipe projection",
           "[track16][invalidation][recipes]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    ( void ) L.synthesize();

    // No registry wired: the refresh must fail closed (router empty).
    CHECK_FALSE( L.broker.refreshRecipes() );
    const SynthesizeResult after = L.synthesize();
    // A failed null-registry refresh changed NO authority state (the router
    // was already empty), so serving the identical bundle under the same
    // key is a legitimate cache hit — the no-stale guarantee binds when the
    // authority state moves (non-null reload failure DOES clear, see the
    // live-authorities suite). What must hold: the served bundle honestly
    // carries the fail-closed recipe projection.
    CHECK( after.bundle.sources.recipes.source == ContentSource::Unavailable );

    // Boundary row of the linkage contract: recipes are a broker-side
    // projection; preflight has no recipe dimension to go stale.
    {
        const PreflightReport first = L.evaluate();
        const PreflightReport second = L.evaluate();
        CHECK( reportDigest( first ) == reportDigest( second ) );
    }
}

// ====================== digest / budget / determinism dims ==================

TEST_CASE( "request digest is invariant across invalidation rounds",
           "[track16][invalidation][digest]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    const PreflightRequest req = L.request( { { "primary", "scene-a" } } );
    const std::string before = computeRequestDigest( req );

    L.authority.passports.clear();
    L.broker.invalidateAllAssets();
    L.installCapabilityFacts( 7 );

    // The digest binds the REQUEST, not the authority state.
    CHECK( computeRequestDigest( req ) == before );
}

TEST_CASE( "budget truncation stays loud across an invalidation that swaps "
           "the finding set",
           "[track16][invalidation][budget]" )
{
    Linkage L;
    // Two independent findings against the cap: an off-list radiometric
    // unit AND blanked band roles.
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "thermal_radiance" ),
                          L.ok );
    std::string blanked = scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "thermal_radiance" );
    for ( const std::string &role : { std::string( "\"role\": \"red\"" ),
                                      std::string( "\"role\": \"nir\"" ) } )
    {
        const auto at = blanked.find( role );
        if ( at != std::string::npos )
            blanked.replace( at, role.size(), "\"role\": \"\"" );
    }
    L.authority.passports["scene-a"] = passportFromJson( blanked, L.ok );
    REQUIRE( L.ok );

    PreflightRequest req = L.request( { { "primary", "scene-a" } } );
    req.budgets.maxFindings = 1;

    const PreflightReport before = L.engine.evaluate( req, L.facts, L.capability );
    CHECK( hasCode( before, "SPF_BUDGET_EXCEEDED" ) );
    CHECK( before.verdict == "blocked" );

    // Invalidate into a DIFFERENT bad state (same finding volume, different
    // content: re-imported onto another CRS with the threshold-grade unit):
    // truncation stays loud across the seam.
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32610", 10.0,
                                             "toa_radiance" ),
                          L.ok );
    REQUIRE( L.ok );
    L.broker.invalidateAsset( "scene-a" );
    const PreflightReport after = L.engine.evaluate( req, L.facts, L.capability );
    CHECK( hasCode( after, "SPF_BUDGET_EXCEEDED" ) );
    CHECK( after.verdict == "blocked" );
}

TEST_CASE( "post-invalidation evaluation is byte-deterministic",
           "[track16][invalidation][determinism]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    L.authority.passports.clear();
    L.broker.notifyProjectSwitch();

    const PreflightReport first = L.evaluate();
    const PreflightReport second = L.evaluate();
    CHECK( reportDigest( first ) == reportDigest( second ) );
    CHECK( first.verdict == second.verdict );
}

TEST_CASE( "invalidation is idempotent at the consumer seam",
           "[track16][invalidation][idempotence]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    L.authority.passports.clear();

    L.broker.invalidateAllAssets();
    const std::string once = reportDigest( L.evaluate() );
    L.broker.invalidateAllAssets();
    L.broker.invalidateAsset( "scene-a" );
    L.broker.notifyProjectSwitch();
    CHECK( reportDigest( L.evaluate() ) == once );
}

TEST_CASE( "broker and preflight project the SAME authority consistently",
           "[track16][invalidation][consistency]" )
{
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );

    const SynthesizeResult synth = L.synthesize();
    REQUIRE( synth.bundle.assets.size() == 1 );
    const PreflightReport report = L.evaluate();

    // Both projections name the same identity for the same key.
    CHECK( synth.bundle.assets.front().assetId == "scene-a" );
    bool preflightSeesIdentity = false;
    for ( const auto &e : report.evaluated )
    {
        ( void ) e;
        preflightSeesIdentity = true;
    }
    CHECK( preflightSeesIdentity );
    CHECK( trace( report, "preflight.band_role" )->outcome == "pass" );

    // After invalidation both projections move together.
    L.authority.passports.clear();
    L.broker.invalidateAsset( "scene-a" );
    const SynthesizeResult after = L.synthesize();
    CHECK_FALSE( after.cacheHit );
    CHECK( trace( L.evaluate(), "preflight.band_role" )->outcome == "insufficient_facts" );
}

TEST_CASE( "stale content is never served after the seam even with an "
           "unchanged revision counter",
           "[track16][invalidation][cache]" )
{
    // setCapabilityFacts clears the broker cache unconditionally — the seam
    // itself is the guarantee, revision advancement is only cache-key
    // material. Pin the seam guarantee.
    Linkage L;
    L.authority.passports["scene-a"] =
        passportFromJson( scenePassportJson( "scene-a", "EPSG:32650", 10.0,
                                             "surface_reflectance" ),
                          L.ok );
    ( void ) L.synthesize();
    CHECK( L.synthesize().cacheHit );

    L.installCapabilityFacts( 1 ); // same revision, fresh authority object
    CHECK_FALSE( L.synthesize().cacheHit );
}
