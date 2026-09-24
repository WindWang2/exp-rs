// tests/test_science_context_live_authorities.cpp
//
// Science Context Broker — live-authority wiring probes.
//
// Round 1 (RED evidence on current master e4904cd3c):
//   G1 cache key ignores budget policy → stale cross-budget bundle served
//   G2 shared broker carries no live recipe authority → recipe:search dead
//   G3 bundle carries no per-section provenance
// The fail-closed guard at the bottom documents behaviour that must survive
// the live-authority work. See .planning/completion-science-context-live-authorities/ledger.md.

#include <catch2/catch_test_macros.hpp>

#include "recipes/recipe_registry.h"
#include "science_context/agent_adapter.h"
#include "science_context/broker.h"
#include "science_context/bundle.h"
#include "science_context/capability_facts.h"
#include "science_context/capability_router.h"
#include "science_context/context_cache.h"
#include "science_context/gdal_asset_source.h"
#include "science_context/live_asset_resolver.h"
#include "science_context/observed_state.h"
#include "scientific_state/asset_state_types.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sicnu::science_context;
using namespace sicnu::state;

namespace
{

/// Points the recipe authority's documented default-dir override at the
/// in-repo pack BEFORE any test touches the shared broker. Runs at static
/// init because Catch2 randomizes case order.
void pointRecipeAuthorityAtRepoPack()
{
#ifdef _WIN32
    (void)_putenv_s( "SICNU_SCIENTIFIC_RECIPES_DIR",
                     ( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/scientific_recipes" ).c_str() );
#else
    ::setenv( "SICNU_SCIENTIFIC_RECIPES_DIR",
              ( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/scientific_recipes" ).c_str(), 1 );
#endif
}

struct RecipeAuthorityEnvGuard
{
    RecipeAuthorityEnvGuard() { pointRecipeAuthorityAtRepoPack(); }
};
const RecipeAuthorityEnvGuard gRecipeAuthorityEnvGuard;

RemoteSensingAssetState makeOptical( const std::string &id, const std::string &radio,
                                     const std::vector<std::string> &roles )
{
    RemoteSensingAssetState s;
    s.assetId = id;
    s.revision = "1";
    s.displayName = id;
    s.sourcePath = "/data/scenes/" + id + ".tif";
    s.kind = AssetKind::Raster;
    s.lifecycle = AssetLifecycle::Ready;
    s.sensor.modality = Modality::Optical;
    s.radiometric.unit = radio;
    s.geometry.hasCrs = true;
    s.geometry.crsAuthid = "EPSG:4326";
    s.geometry.hasPixelSize = true;
    s.geometry.pixelSizeX = 10.0;
    s.geometry.pixelSizeY = 10.0;
    int idx = 1;
    for ( const auto &role : roles )
    {
        BandState b;
        b.index = idx++;
        b.role = role;
        b.name = role;
        s.bands.push_back( b );
    }
    ClaimRecord c;
    c.path = "radiometric.unit";
    c.kind = ClaimKind::Known;
    c.sources = { "test" };
    s.claims.push_back( c );
    return s;
}

std::vector<RecipeDocument> paddedRecipes( int count )
{
    std::vector<RecipeDocument> docs;
    for ( int i = 0; i < count; ++i )
    {
        RecipeDocument d;
        d.recipeId = "lab.pad" + std::to_string( i );
        d.title = "padded recipe title " + std::string( 40, 'x' ) + " " + std::to_string( i );
        d.intent = "ndvi";
        d.modality = "optical";
        d.keywords = { "ndvi", "vegetation" };
        d.stageCount = 3;
        docs.push_back( d );
    }
    return docs;
}

} // namespace

TEST_CASE( "budget change must not serve a stale cached bundle",
           "[science_context_live]" )
{
    ScienceContextBroker broker;
    broker.recipes().setRecipes( paddedRecipes( 6 ) );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );

    SynthesizeRequest wide;
    wide.goal = "compute an ndvi over the primary optical asset";
    wide.intent = "ndvi";
    wide.passports = { state };
    wide.budget.maxBytes = 65536;
    auto first = broker.synthesize( wide );
    REQUIRE( first.cacheHit == false );
    REQUIRE( static_cast<int>( serializeBundle( first.bundle ).size() ) > 1200 );

    SynthesizeRequest tight = wide;
    tight.budget.maxBytes = 1500; // between the wide bundle and the floor
    auto second = broker.synthesize( tight );
    // RED on master: the cache key omits budget policy, so the 64 KiB bundle
    // is served to a caller that asked for a tighter context.
    CHECK( second.cacheHit == false );
    CHECK( static_cast<int>( serializeBundle( second.bundle ).size() ) <= 1500 );
    CHECK( serializeBundle( second.bundle ).size() < serializeBundle( first.bundle ).size() );
    CHECK( second.bundle.truncation.truncated == true );
}

TEST_CASE( "shared broker serves the live scientific recipe pack",
           "[science_context_live]" )
{
    // Guard: the in-repo pack must exist so this probe fails for broker
    // reasons, not environment drift.
    sicnu::recipes::ScientificRecipeRegistry probe;
    REQUIRE( probe.reload() > 0 );

    Json::Value args( Json::objectValue );
    args["text"] = "change detection";
    auto out = agent_adapter::recipeSearch( args );
    // RED on master: default shared broker never loads any recipe authority.
    CHECK( out["total"].asInt() >= 1 );
    CHECK( out["hits"][0]["recipe_id"].asString().rfind( "lab.", 0 ) == 0 );
}

TEST_CASE( "bundle carries per-section provenance", "[science_context_live]" )
{
    ScienceContextBroker broker;
    broker.recipes().setRecipes( paddedRecipes( 2 ) );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    auto result = broker.synthesize( req );

    Json::Value json = bundleToJson( result.bundle );
    // RED on master: no provenance at all.
    CHECK( json.isMember( "sources" ) );
    const Json::Value &src = json["sources"];
    CHECK( src["assets"]["source"].asString() == "inline_input" );
    CHECK( src["capabilities"]["source"].asString() == "builtin_fallback" );
    CHECK( src["capabilities"]["degraded"].asBool() == true );
    CHECK( src["recipes"]["source"].asString() == "inline_input" );
    CHECK( src["planner_facts"]["source"].asString() == "inline_input" );
}

TEST_CASE( "missing resolver fails closed with typed unknown",
           "[science_context_live]" )
{
    ScienceContextBroker broker; // no resolver wired
    SynthesizeRequest req;
    req.goal = "anything";
    req.assetKeys = { "no-such-asset" };
    auto result = broker.synthesize( req );
    REQUIRE( result.bundle.assets.size() == 1 );
    CHECK( result.bundle.assets[0].evidence == EvidenceBucket::Unknown );

    agent_adapter::setSharedBrokerForTest( &broker );
    Json::Value args( Json::objectValue );
    args["asset_id"] = "no-such-asset";
    CHECK_THROWS_AS( agent_adapter::dataAssetPassport( args ), std::runtime_error );
    agent_adapter::setSharedBrokerForTest( nullptr );
}

// ---------------------------------------------------------------------------
// Stage 2: live-authority wiring (scientific_state resolver + GDAL facts,
// ScientificRecipeRegistry, capability knowledge pack via the facts seam).
// ---------------------------------------------------------------------------

namespace
{

void ensureGdal()
{
    static std::once_flag once;
    std::call_once( once, [] { GDALAllRegister(); } );
}

/// Writes a small GeoTIFF with SICNU_* metadata, UTM CRS and red/nir bands —
/// repo convention: fixtures synthesized at runtime, no committed geodata.
std::string writeDeclaredGeoTiff( const std::string &dir, const std::string &name,
                                  const char *radiometric )
{
    ensureGdal();
    const std::string path = ( std::filesystem::path( dir ) / name ).string();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 3, 2, GDT_Byte, nullptr );
    REQUIRE( dataset );

    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    REQUIRE( OSRImportFromEPSG( srs, 32650 ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
    REQUIRE( GDALSetProjection( dataset, wkt ) == CE_None );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );

    double geotransform[6] = { 499980.0, 30.0, 0.0, 4800000.0, 0.0, -30.0 };
    REQUIRE( GDALSetGeoTransform( dataset, geotransform ) == CE_None );

    GDALSetMetadataItem( dataset, "SICNU_MODALITY", "optical", nullptr );
    if ( radiometric )
        GDALSetMetadataItem( dataset, "SICNU_RADIOMETRIC_STATE", radiometric, nullptr );

    const char *names[2] = { "B4", "B5" };
    const char *roles[2] = { "red", "nir" };
    for ( int i = 1; i <= 2; ++i )
    {
        GDALRasterBandH band = GDALGetRasterBand( dataset, i );
        REQUIRE( band );
        GDALSetDescription( band, names[i - 1] );
        GDALSetMetadataItem( band, "SICNU_BAND_ROLE", roles[i - 1], nullptr );
    }
    GDALClose( dataset );
    return path;
}

std::string makeTempDir( const std::string &tag )
{
    namespace fs = std::filesystem;
    // Portable unique-enough suffix (no POSIX getpid — Windows lanes).
    const std::string suffix = std::to_string(
        std::hash<std::string>{}( tag + std::string( CMAKE_SOURCE_DIR ) ) % 100000000 );
    const fs::path dir = fs::temp_directory_path() / ( "sci_ctx_live_" + tag + "_" + suffix );
    std::error_code ec;
    fs::remove_all( dir, ec ); // clear leftovers from a crashed earlier run
    fs::create_directories( dir );
    return dir.string();
}

std::string repoFile( const std::string &rel )
{
    return std::string( CMAKE_SOURCE_DIR ) + "/" + rel;
}

Json::Value readJsonFile( const std::string &path )
{
    std::ifstream in( path );
    REQUIRE( in.is_open() );
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    REQUIRE( Json::parseFromStream( builder, in, &parsed, &errors ) );
    return parsed;
}

/// Real-shaped capability authority: scans the REAL in-repo knowledge pack
/// (data/agent/capabilities/*.json) with the same candidate semantics the
/// harness serves (base entries that declare the intent + variants that
/// declare it), without dragging the Qt-linked agent lib into this Qt-free
/// test binary.
CapabilityFactsLookup packBackedLookup( const std::string &packDir )
{
    CapabilityFactsLookup lookup;
    lookup.authority = "capability_knowledge.pack";
    auto entries = std::make_shared<std::vector<Json::Value>>();
    for ( const auto &item : std::filesystem::directory_iterator( packDir ) )
    {
        if ( item.path().extension() != ".json" )
            continue;
        Json::Value doc = readJsonFile( item.path().string() );
        if ( doc.isArray() )
        {
            for ( const auto &entry : doc )
                entries->push_back( entry );
        }
    }
    auto declaresIntent = []( const Json::Value &intents, const std::string &intent ) {
        if ( !intents.isArray() )
            return false;
        for ( const auto &candidate : intents )
        {
            if ( candidate.isString() && candidate.asString() == intent )
                return true;
        }
        return false;
    };
    lookup.entriesForIntent = [entries, declaresIntent]( const std::string &intent )
        -> std::vector<Json::Value> {
        std::vector<Json::Value> candidates;
        for ( const auto &entry : *entries )
        {
            if ( !entry.isObject() )
                continue;
            if ( declaresIntent( entry["intents"], intent ) )
                candidates.push_back( entry );
            if ( !entry["variants"].isArray() )
                continue;
            for ( const auto &variant : entry["variants"] )
            {
                if ( !variant.isObject() || !declaresIntent( variant["intents"], intent ) )
                    continue;
                // Same overlay semantics as production: variant keys override,
                // everything undeclared still applies from the merged base.
                Json::Value candidate = entry;
                for ( const auto &key : variant.getMemberNames() )
                {
                    if ( std::string( key ) == "when" || std::string( key ) == "intents" )
                        continue;
                    candidate[key] = variant[key];
                }
                if ( candidate.isMember( "when" ) )
                    candidate.removeMember( "when" );
                if ( candidate.isMember( "intents" ) )
                    candidate.removeMember( "intents" );
                candidates.push_back( candidate );
            }
        }
        return candidates;
    };
    lookup.presence = []( const std::string &, const std::string & ) {
        return OperatorPresence::Present;
    };
    return lookup;
}

} // namespace

TEST_CASE( "live GDAL facts flow through the real resolver into the bundle",
           "[science_context_live]" )
{
    const std::string dir = makeTempDir( "gdal" );
    ScienceContextBroker broker;

    AssetFactSources sources;
    sources.dataset = gdalDatasetFactsCollector();
    auto resolver = makeFactsBasedResolver( sources );
    broker.assets().setResolver( resolver );
    broker.assets().setResolverAuthority( "scientific_state.resolve_asset_state+gdal" );

    const std::string path = writeDeclaredGeoTiff( dir, "declared.tif", "surface_reflectance" );
    SynthesizeRequest req;
    req.goal = "compute an ndvi";
    req.intent = "ndvi";
    req.assetKeys = { path };
    auto result = broker.synthesize( req );

    REQUIRE( result.bundle.assets.size() == 1 );
    const AssetSummary &asset = result.bundle.assets[0];
    CHECK( asset.source == "live_authority" );
    CHECK( asset.evidence == EvidenceBucket::Known ); // declared facts stay Known
    CHECK( asset.radiometricUnit == "surface_reflectance" );
    bool hasRed = false;
    bool hasNir = false;
    for ( const auto &role : asset.bandRoles )
    {
        hasRed = hasRed || role == "red";
        hasNir = hasNir || role == "nir";
    }
    CHECK( hasRed );
    CHECK( hasNir );
    CHECK( result.bundle.sources.assets.source == ContentSource::LiveAuthority );
    CHECK( result.bundle.sources.assets.authority ==
           "scientific_state.resolve_asset_state+gdal" );
    CHECK( result.bundle.sources.plannerFacts.source == ContentSource::LiveAuthority );
    // NDVI feasible from live facts only.
    REQUIRE( !result.bundle.capabilities.empty() );
    CHECK( result.bundle.capabilities[0].status == "direct" );

    // No declared radiometric marker: the authority's documented FSM default
    // applies — an explicitly Assumed digital_number, never a fabricated Known.
    const std::string undeclared = writeDeclaredGeoTiff( dir, "undeclared.tif", nullptr );
    SynthesizeRequest req2;
    req2.goal = "compute an ndvi";
    req2.intent = "ndvi";
    req2.assetKeys = { undeclared };
    auto r2 = broker.synthesize( req2 );
    REQUIRE( r2.bundle.assets.size() == 1 );
    CHECK( r2.bundle.assets[0].radiometricUnit == "digital_number" );
    // The FSM default is only Assumed: at claim level the radiometric is
    // assumed, while the Known band roles dominate the summary bucket.
    auto rawUndeclared = resolver( undeclared );
    REQUIRE( rawUndeclared );
    bool assumedRadiometric = false;
    for ( const auto &claim : rawUndeclared->claims )
        assumedRadiometric = assumedRadiometric ||
                             ( claim.path == "radiometric.unit" &&
                               claim.kind == ClaimKind::Assumed );
    CHECK( assumedRadiometric );
    REQUIRE( !r2.bundle.capabilities.empty() );
    // DN must be calibrated before NDVI — prep, never direct.
    CHECK( r2.bundle.capabilities[0].status == "prep" );
    bool calibrationPrep = false;
    for ( const auto &action : r2.bundle.capabilities[0].prepActions )
        calibrationPrep =
            calibrationPrep || action.find( "calibrate_to_reflectance" ) != std::string::npos;
    CHECK( calibrationPrep );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "conflicted radiometric survives the live chain un-downgraded",
           "[science_context_live]" )
{
    // Fake fact source, REAL authority: two declarative radiometric
    // observations disagree — resolveAssetState must surface Conflicted and
    // the broker must never auto-pick a winner.
    sicnu::state::DatasetFacts conflicting;
    conflicting.sourcePath = "/data/scenes/conflicted.tif";
    conflicting.driverName = "GTiff";
    conflicting.bandCount = 2;
    conflicting.metadata.add( "SICNU_RADIOMETRIC_STATE", "digital_number",
                              "gdal:test" );
    conflicting.metadata.add( "SICNU_RADIOMETRIC_STATE", "surface_reflectance",
                              "catalog:AssetSnapshot" );
    // Band roles present so capability evaluation reaches the radiometry gate
    // (band-role absence must not mask the radiometric conflict).
    sicnu::state::BandFacts red;
    red.index = 1;
    red.name = "B4";
    red.metadata.add( "SICNU_BAND_ROLE", "red", "gdal:test" );
    sicnu::state::BandFacts nir;
    nir.index = 2;
    nir.name = "B5";
    nir.metadata.add( "SICNU_BAND_ROLE", "nir", "gdal:test" );
    conflicting.bands = { red, nir };

    AssetFactSources sources;
    sources.dataset = [&conflicting]( const std::string &key )
        -> std::optional<sicnu::state::DatasetFacts> {
        if ( key != conflicting.sourcePath )
            return std::nullopt;
        return conflicting;
    };
    auto resolver = makeFactsBasedResolver( sources );
    auto state = resolver( conflicting.sourcePath );
    REQUIRE( state );
    bool conflictedClaim = false;
    for ( const auto &claim : state->claims )
        conflictedClaim = conflictedClaim || claim.kind == ClaimKind::Conflicted;
    CHECK( conflictedClaim );

    auto summary = AssetStateProvider::summarize( *state );
    CHECK( summary.evidence == EvidenceBucket::Conflicted );
    CHECK( summary.radiometricUnit.empty() );

    // Broker path: conflicted evidence is unavailable for compute, and the
    // planner projection blocks execution — never a silent downgrade.
    ScienceContextBroker broker;
    broker.assets().setResolver( resolver );
    broker.assets().setResolverAuthority( "scientific_state.resolve_asset_state" );
    SynthesizeRequest req;
    req.goal = "compute an ndvi";
    req.intent = "ndvi";
    req.assetKeys = { conflicting.sourcePath };
    auto result = broker.synthesize( req );
    REQUIRE( result.bundle.assets.size() == 1 );
    CHECK( result.bundle.assets[0].evidence == EvidenceBucket::Conflicted );
    REQUIRE( !result.bundle.capabilities.empty() );
    CHECK( result.bundle.capabilities[0].status == "unavailable" );
    bool conflictReason = false;
    for ( const auto &reason : result.bundle.capabilities[0].reasons )
        conflictReason = conflictReason || reason.find( "RADIOMETRIC_CONFLICTED" ) !=
                                                   std::string::npos;
    CHECK( conflictReason );
    CHECK( result.planning.executionBlocked == true );
}

TEST_CASE( "deleted asset fails closed after invalidation", "[science_context_live]" )
{
    namespace fs = std::filesystem;
    const std::string dir = makeTempDir( "deleted" );
    ScienceContextBroker broker;
    AssetFactSources sources;
    sources.dataset = gdalDatasetFactsCollector();
    broker.assets().setResolver( makeFactsBasedResolver( sources ) );
    broker.assets().setResolverAuthority( "scientific_state.resolve_asset_state+gdal" );

    const std::string path = writeDeclaredGeoTiff( dir, "gone.tif", "surface_reflectance" );
    SynthesizeRequest req;
    req.goal = "inspect";
    req.assetKeys = { path };
    auto first = broker.synthesize( req );
    REQUIRE( first.bundle.assets[0].evidence == EvidenceBucket::Known );

    REQUIRE( fs::remove( path ) );
    broker.invalidateAsset( path );
    auto after = broker.synthesize( req );
    // Stale bundle must not be served: the deleted asset is typed unknown.
    REQUIRE( after.bundle.assets.size() == 1 );
    CHECK( after.bundle.assets[0].evidence == EvidenceBucket::Unknown );
    CHECK( after.bundle.assets[0].source == "unavailable" );
    CHECK( after.bundle.sources.assets.source == ContentSource::Unavailable );
    CHECK( after.bundle.sources.assets.degraded == true );

    std::error_code ec;
    fs::remove_all( dir, ec );
}

TEST_CASE( "registry sync serves real pack and reload invalidation is fail-closed",
           "[science_context_live]" )
{
    namespace fs = std::filesystem;
    const std::string dir = makeTempDir( "pack" );
    const auto source = fs::path( repoFile( "data/agent/scientific_recipes/lab.lab04_change_detection.json" ) );
    REQUIRE( fs::exists( source ) );
    const auto packaged = fs::path( dir ) / source.filename();
    fs::copy_file( source, packaged );

    sicnu::recipes::ScientificRecipeRegistry registry;
    registry.setDirectory( dir );
    ScienceContextBroker broker;
    broker.setRecipeRegistry( &registry );
    REQUIRE( broker.refreshRecipes() );

    // The pack's own intent vocabulary is "change_detection"; the broker's
    // closed intent is "change" — the projection matches on keywords too.
    RecipeQuery q;
    q.text = "change detection";
    auto hits = broker.recipes().search( q );
    REQUIRE( !hits.hits.empty() );
    CHECK( hits.hits[0].recipeId == "lab.lab04_change_detection" );
    CHECK( broker.recipes().sourceInfo().mode == RecipeSourceMode::LiveAuthority );
    CHECK( broker.recipes().sourceInfo().registryStatus == "ok" );

    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "change detection between two optical scenes";
    req.passports = { state };
    auto r1 = broker.synthesize( req );
    CHECK( r1.cacheHit == false );
    CHECK( !r1.bundle.recipes.empty() );
    CHECK( r1.bundle.sources.recipes.source == ContentSource::LiveAuthority );
    auto r2 = broker.synthesize( req );
    CHECK( r2.cacheHit == true );

    // Deleted pack document: reload fails closed, stale bundles are gone.
    fs::remove( packaged );
    CHECK_FALSE( broker.refreshRecipes() );
    auto r3 = broker.synthesize( req );
    CHECK( r3.cacheHit == false );
    CHECK( r3.bundle.recipes.empty() );
    CHECK( r3.bundle.sources.recipes.source == ContentSource::Unavailable );

    std::error_code ec;
    fs::remove_all( dir, ec );
}

TEST_CASE( "capability facts come from the knowledge pack, not the builtin table",
           "[science_context_live]" )
{
    const std::string packDir = repoFile( "data/agent/capabilities" );
    REQUIRE( std::filesystem::is_directory( packDir ) );
    CapabilityFactsLookup lookup = packBackedLookup( packDir );

    CapabilityQuery q;
    q.intent = "ndvi";
    q.observedState =
        observedStateFromPassport( makeOptical( "sr", "surface_reflectance", { "red", "nir" } ) );
    q.facts = &lookup;
    auto routed = routeCapabilities( q );

    REQUIRE( !routed.entries.empty() );
    CHECK( routed.factsFromAuthority == true );
    CHECK( routed.factsAuthority == "capability_knowledge.pack" );
    // Authority-owned facts: the knowledge entry id and vocabulary, not the
    // broker's builtin spec.
    CHECK( routed.entries[0].capabilityId == "rs:ndvi" );
    CHECK( routed.entries[0].status == "direct" );

    // Variant-served intents must NOT lose their band-role requirements: the
    // NDRE variant of rs:spectral_index needs red_edge+nir, so an asset with
    // only red+nir is unavailable, and one with red_edge+nir is direct.
    auto hasEntry = []( const CapabilityRouterResult &r, const std::string &id ) {
        for ( const auto &e : r.entries )
        {
            if ( e.capabilityId == id )
                return e;
        }
        return CapabilityEntry{};
    };
    const auto ndreBase = observedStateFromPassport(
        makeOptical( "ndre1", "surface_reflectance", { "red", "nir" } ) );
    CapabilityQuery ndreQ;
    ndreQ.intent = "ndre";
    ndreQ.observedState = ndreBase;
    ndreQ.facts = &lookup;
    auto ndreMissing = routeCapabilities( ndreQ );
    const CapabilityEntry spectralMissing = hasEntry( ndreMissing, "rs:spectral_index" );
    CHECK( spectralMissing.capabilityId == "rs:spectral_index" );
    CHECK( spectralMissing.status != "direct" );

    const auto ndreWith = observedStateFromPassport(
        makeOptical( "ndre2", "surface_reflectance", { "red_edge", "nir" } ) );
    CapabilityQuery ndreQ2 = ndreQ;
    ndreQ2.observedState = ndreWith;
    auto ndreOk = routeCapabilities( ndreQ2 );
    const CapabilityEntry spectralOk = hasEntry( ndreOk, "rs:spectral_index" );
    CHECK( spectralOk.capabilityId == "rs:spectral_index" );
    CHECK( spectralOk.status == "direct" );

    // Multi-candidate intents list every serving operator, not an arbitrary
    // first match (classify is served by rs:, otb: and gdal: operators).
    CapabilityQuery classifyQ;
    classifyQ.intent = "classify";
    classifyQ.observedState = ndreBase;
    classifyQ.facts = &lookup;
    auto classifyRouted = routeCapabilities( classifyQ );
    CHECK( classifyRouted.entries.size() >= 2 );

    // The builtin fallback must stay disabled while an authority is wired.
    auto offVocab = packBackedLookup( packDir );
    offVocab.entriesForIntent = []( const std::string & ) { return std::vector<Json::Value>{}; };
    CapabilityQuery q2 = q;
    q2.intent = "ndvi";
    q2.facts = &offVocab;
    auto routed2 = routeCapabilities( q2 );
    CHECK( routed2.intentStatus == "unresolved" );
    REQUIRE( !routed2.entries.empty() );
    CHECK( routed2.entries[0].capabilityId == "unknown" );
    CHECK( routed2.factsAuthority == "capability_knowledge.pack" );
}

TEST_CASE( "operator presence gates capability status honestly",
           "[science_context_live]" )
{
    CapabilityFactsLookup absent;
    absent.authority = "capability_knowledge.test";
    absent.entriesForIntent =
        []( const std::string &intent ) -> std::vector<Json::Value> {
        if ( intent != "ndvi" )
            return {};
        Json::Value entry( Json::objectValue );
        entry["id"] = "rs:ndvi";
        Json::Value roles( Json::objectValue );
        roles["red"] = 1;
        roles["nir"] = 1;
        entry["band_roles"] = roles;
        return { entry };
    };
    absent.presence = []( const std::string &, const std::string & ) {
        return OperatorPresence::Absent;
    };

    CapabilityQuery q;
    q.intent = "ndvi";
    q.observedState =
        observedStateFromPassport( makeOptical( "sr", "surface_reflectance", { "red", "nir" } ) );
    q.facts = &absent;
    auto routedAbsent = routeCapabilities( q );
    REQUIRE( !routedAbsent.entries.empty() );
    CHECK( routedAbsent.entries[0].status == "impossible" );
    CHECK( routedAbsent.entries[0].reasons[0] == "OPERATOR_NOT_REGISTERED:rs:ndvi" );

    // Unknown presence must never read as a clean direct success — asserted
    // unconditionally: direct is only allowed with the capped score.
    CapabilityFactsLookup unknownPresence = absent;
    unknownPresence.presence = []( const std::string &, const std::string & ) {
        return OperatorPresence::Unknown;
    };
    CapabilityQuery q2 = q;
    q2.facts = &unknownPresence;
    auto routedUnknown = routeCapabilities( q2 );
    REQUIRE( !routedUnknown.entries.empty() );
    CHECK( routedUnknown.presenceUnknown == true );
    bool surfaced = false;
    for ( const auto &reason : routedUnknown.entries[0].reasons )
        surfaced = surfaced || reason.find( "OPERATOR_PRESENCE_UNKNOWN" ) != std::string::npos;
    CHECK( surfaced );
    const bool notDirect = routedUnknown.entries[0].status != "direct";
    const bool scoreCapped = routedUnknown.entries[0].score <= 0.9;
    const bool honestUnknown = notDirect || scoreCapped;
    CHECK( honestUnknown );

    // Authority minima above the observed (deduped) role evidence fail closed.
    CapabilityFactsLookup heavy = absent;
    heavy.presence = []( const std::string &, const std::string & ) {
        return OperatorPresence::Present;
    };
    heavy.entriesForIntent =
        []( const std::string &intent ) -> std::vector<Json::Value> {
        if ( intent != "ndvi" )
            return {};
        Json::Value entry( Json::objectValue );
        entry["id"] = "rs:ndvi";
        Json::Value roles( Json::objectValue );
        roles["red"] = 1;
        roles["nir"] = 3;
        entry["band_roles"] = roles;
        return { entry };
    };
    CapabilityQuery q3 = q;
    q3.facts = &heavy;
    auto routedHeavy = routeCapabilities( q3 );
    REQUIRE( !routedHeavy.entries.empty() );
    CHECK( routedHeavy.entries[0].status == "prep" );
    bool countSurfaced = false;
    for ( const auto &reason : routedHeavy.entries[0].reasons )
        countSurfaced = countSurfaced || reason.find( "BAND_ROLE_COUNT_UNVERIFIED:nir:3" ) !=
                                              std::string::npos;
    CHECK( countSurfaced );
}

TEST_CASE( "one impossible candidate does not block a servable goal",
           "[science_context_live]" )
{
    // Mirrors the real sar_change-on-optical shape: one candidate is direct,
    // another impossible — the planner must not report executionBlocked.
    CapabilityFactsLookup mixed;
    mixed.authority = "capability_knowledge.test";
    mixed.entriesForIntent = []( const std::string &intent )
        -> std::vector<Json::Value> {
        if ( intent != "change" )
            return {};
        Json::Value optical( Json::objectValue );
        optical["id"] = "rs:change";
        Json::Value opticalModality( Json::arrayValue );
        opticalModality.append( "optical" );
        opticalModality.append( "sar" );
        optical["modality"] = opticalModality;
        Json::Value roles( Json::objectValue );
        roles["red"] = 1;
        roles["nir"] = 1;
        optical["band_roles"] = roles;

        Json::Value sar( Json::objectValue );
        sar["id"] = "rs:sar_change";
        Json::Value sarModality( Json::arrayValue );
        sarModality.append( "sar" );
        sar["modality"] = sarModality;
        return { optical, sar };
    };
    mixed.presence = []( const std::string &, const std::string & ) {
        return OperatorPresence::Present;
    };

    ScienceContextBroker broker;
    broker.setCapabilityFacts( mixed );
    broker.recipes().setRecipes( paddedRecipes( 2 ) );
    SynthesizeRequest req;
    req.goal = "change detection";
    req.intent = "change";
    req.passports = { makeOptical( "sr", "surface_reflectance", { "red", "nir" } ) };
    auto result = broker.synthesize( req );

    bool sawDirect = false;
    bool sawImpossible = false;
    for ( const auto &c : result.bundle.capabilities )
    {
        sawDirect = sawDirect || c.status == "direct";
        sawImpossible = sawImpossible || c.status == "impossible";
    }
    CHECK( sawDirect );
    CHECK( sawImpossible );
    CHECK( result.planning.executionBlocked == false );

    // Every candidate unusable ⇒ blocked (fail-closed is preserved).
    CapabilityFactsLookup allImpossible = mixed;
    allImpossible.entriesForIntent = []( const std::string &intent )
        -> std::vector<Json::Value> {
        if ( intent != "change" )
            return {};
        Json::Value sar( Json::objectValue );
        sar["id"] = "rs:sar_change";
        Json::Value sarModality( Json::arrayValue );
        sarModality.append( "sar" );
        sar["modality"] = sarModality;
        return { sar };
    };
    ScienceContextBroker broker2;
    broker2.setCapabilityFacts( allImpossible );
    broker2.recipes().setRecipes( paddedRecipes( 2 ) );
    SynthesizeRequest req2 = req;
    req2.useCache = false;
    auto blocked = broker2.synthesize( req2 );
    CHECK( blocked.planning.executionBlocked == true );
}

TEST_CASE( "provenance round-trips and repeated synthesis stays byte-stable",
           "[science_context_live]" )
{
    ScienceContextBroker broker;
    broker.recipes().setRecipes( paddedRecipes( 2 ) );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };

    auto result = broker.synthesize( req );
    Json::Value json = bundleToJson( result.bundle );
    ScientificContextBundle restored;
    std::string error;
    REQUIRE( bundleFromJson( json, restored, &error ) );
    CHECK( serializeBundle( restored ) == serializeBundle( result.bundle ) );
    CHECK( restored.sources.capabilities.source == ContentSource::BuiltinFallback );
    CHECK( restored.sources.capabilities.degraded == true );
    CHECK( restored.sources.assets.source == ContentSource::InlineInput );

    // Determinism: same inputs, cache off → identical bytes and id.
    req.useCache = false;
    auto again = broker.synthesize( req );
    CHECK( serializeBundle( again.bundle ) == serializeBundle( result.bundle ) );
    CHECK( again.bundle.bundleId == result.bundle.bundleId );
}

TEST_CASE( "context cache stays bounded under distinct requests",
           "[science_context_live]" )
{
    ScienceContextBroker broker;
    broker.recipes().setRecipes( paddedRecipes( 2 ) );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    for ( int i = 0; i < ContextCache::kMaxEntries + 12; ++i )
    {
        SynthesizeRequest req;
        req.goal = "goal " + std::to_string( i );
        req.intent = "ndvi";
        req.passports = { state };
        broker.synthesize( req );
    }
    CHECK( broker.cache().size() <= ContextCache::kMaxEntries );
}

TEST_CASE( "offline session without wired authorities fabricates nothing",
           "[science_context_live]" )
{
    ScienceContextBroker broker; // no resolver, no registry, no capability facts
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "classify this scene";
    req.intent = "classify";
    req.passports = { state };
    req.constraints.offline = true;
    auto result = broker.synthesize( req );

    const bool offlineNoted = [&] {
        for ( const auto &q : result.bundle.openQuestions )
        {
            if ( q.find( "offline_mode_no_remote_enrichment" ) != std::string::npos )
                return true;
        }
        return false;
    }();
    CHECK( offlineNoted );
    // Recipes: nothing loaded ⇒ unavailable, never invented.
    CHECK( result.bundle.recipes.empty() );
    CHECK( result.bundle.sources.recipes.source == ContentSource::Unavailable );
    // Capabilities: the labelled builtin fallback only — visibly degraded.
    CHECK( result.bundle.sources.capabilities.source == ContentSource::BuiltinFallback );
    CHECK( result.bundle.sources.capabilities.degraded == true );
    // Planner facts: caller-supplied passport, labelled as such.
    CHECK( result.bundle.sources.plannerFacts.source == ContentSource::InlineInput );
}
