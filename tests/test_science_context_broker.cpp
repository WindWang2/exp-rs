// test_science_context_broker.cpp — Science Context Broker scenarios (A–H DoD).
#include <catch2/catch_test_macros.hpp>

#include "science_context/agent_adapter.h"
#include "science_context/broker.h"
#include "science_context/bundle.h"
#include "science_context/capability_facts.h"
#include "science_context/capability_router.h"
#include "science_context/gdal_asset_source.h"
#include "science_context/live_asset_resolver.h"
#include "science_context/observed_state.h"
#include "scientific_state/asset_state_types.h"
#include "scientific_state/asset_state_json.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

using namespace sicnu::science_context;
using namespace sicnu::state;

namespace {

RemoteSensingAssetState makeOptical( const std::string &id, const std::string &radio,
                                     const std::vector<std::string> &roles,
                                     ClaimKind radioKind = ClaimKind::Known )
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
    c.kind = radioKind;
    c.sources = { "test" };
    if ( radioKind == ClaimKind::Conflicted )
        c.alternatives = { "digital_number", "surface_reflectance" };
    s.claims.push_back( c );
    for ( const auto &role : roles )
    {
        ClaimRecord bc;
        bc.path = "bands." + role + ".role";
        bc.kind = ClaimKind::Known;
        bc.sources = { "test" };
        s.claims.push_back( bc );
    }
    return s;
}

RemoteSensingAssetState makeSar( const std::string &id )
{
    RemoteSensingAssetState s;
    s.assetId = id;
    s.revision = "1";
    s.displayName = id;
    s.sourcePath = "/mnt/sar/" + id + ".tif";
    s.kind = AssetKind::Raster;
    s.sensor.modality = Modality::Sar;
    s.radiometric.unit = "sigma0";
    ClaimRecord c;
    c.path = "radiometric.unit";
    c.kind = ClaimKind::Known;
    c.sources = { "test" };
    s.claims.push_back( c );
    return s;
}

void seedRecipes( RecipeRouter &router )
{
    std::vector<RecipeDocument> docs;
    {
        RecipeDocument d;
        d.recipeId = "lab.ndvi_optical";
        d.title = "NDVI optical";
        d.intent = "ndvi";
        d.modality = "optical";
        d.keywords = { "ndvi", "vegetation" };
        d.stageCount = 3;
        d.hasHumanOnly = true;
        d.hasVerifierHooks = true;
        docs.push_back( d );
    }
    {
        RecipeDocument d;
        d.recipeId = "lab.ndvi_alt";
        d.title = "NDVI alt";
        d.intent = "ndvi";
        d.modality = "optical";
        d.keywords = { "ndvi" };
        d.stageCount = 2;
        docs.push_back( d );
    }
    {
        RecipeDocument d;
        d.recipeId = "lab.sar_change";
        d.title = "SAR change";
        d.intent = "sar_change";
        d.modality = "sar";
        d.keywords = { "sar", "change" };
        d.stageCount = 4;
        docs.push_back( d );
    }
    router.setRecipes( docs );
    router.setRegistryRevision( 7 );
}

ScienceContextBroker makeBroker()
{
    ScienceContextBroker broker;
    seedRecipes( broker.recipes() );
    return broker;
}

} // namespace

TEST_CASE( "optical DN to NDVI needs calibration", "[science_context]" )
{
    auto state = makeOptical( "dn1", "digital_number", { "red", "nir" } );
    CapabilityQuery q;
    q.intent = "ndvi";
    q.observedState = observedStateFromPassport( state );
    auto r = routeCapabilities( q );
    REQUIRE( r.entries.size() == 1 );
    CHECK( r.entries[0].status == "prep" );
    CHECK( r.entries[0].reasons[0].find( "DN_NEEDS_CALIBRATION" ) != std::string::npos );
}

TEST_CASE( "SR to NDVI is direct", "[science_context]" )
{
    auto state = makeOptical( "sr1", "surface_reflectance", { "red", "nir" } );
    CapabilityQuery q;
    q.intent = "ndvi";
    q.observedState = observedStateFromPassport( state );
    auto r = routeCapabilities( q );
    REQUIRE( !r.entries.empty() );
    CHECK( r.entries[0].status == "direct" );
}

TEST_CASE( "missing band role blocks NDVI", "[science_context]" )
{
    auto state = makeOptical( "noblue", "surface_reflectance", { "red" } ); // no nir
    CapabilityQuery q;
    q.intent = "ndvi";
    q.observedState = observedStateFromPassport( state );
    auto r = routeCapabilities( q );
    REQUIRE( !r.entries.empty() );
    CHECK( r.entries[0].status == "unavailable" );
    CHECK( r.entries[0].reasons[0].find( "MISSING_BAND_ROLE" ) != std::string::npos );
}

TEST_CASE( "CRS grid conflict between assets", "[science_context]" )
{
    auto a = makeOptical( "a", "surface_reflectance", { "red", "nir" } );
    auto b = makeOptical( "b", "surface_reflectance", { "red", "nir" } );
    b.geometry.crsAuthid = "EPSG:3857";
    auto broker = makeBroker();
    SynthesizeRequest req;
    req.goal = "compute NDVI";
    req.intent = "ndvi";
    req.passports = { a, b };
    auto result = broker.synthesize( req );
    REQUIRE( !result.bundle.capabilities.empty() );
    CHECK( result.bundle.capabilities[0].status == "unavailable" );
    CHECK( result.bundle.capabilities[0].reasons[0].find( "CRS_GRID" ) != std::string::npos );
}

TEST_CASE( "SAR vs optical-only recipe filtered", "[science_context]" )
{
    auto broker = makeBroker();
    auto sar = makeSar( "s1" );
    RecipeQuery q;
    q.intent = "ndvi";
    q.observedState = observedStateFromPassport( sar );
    auto hits = broker.recipes().search( q );
    for ( const auto &h : hits.hits )
        CHECK( h.modality != "optical" );
    CHECK( hits.hits.empty() );
}

TEST_CASE( "deterministic recipe order", "[science_context]" )
{
    auto broker = makeBroker();
    RecipeQuery q;
    q.intent = "ndvi";
    q.text = "ndvi vegetation";
    auto a = broker.recipes().search( q );
    auto b = broker.recipes().search( q );
    REQUIRE( a.hits.size() == b.hits.size() );
    REQUIRE( a.hits.size() >= 2 );
    for ( size_t i = 0; i < a.hits.size(); ++i )
    {
        CHECK( a.hits[i].recipeId == b.hits[i].recipeId );
        CHECK( a.hits[i].score == b.hits[i].score );
    }
    // score desc, then id asc among ties — lab.ndvi_alt vs lab.ndvi_optical
    CHECK( a.hits[0].score >= a.hits[1].score );
}

TEST_CASE( "unknown operator intent", "[science_context]" )
{
    CapabilityQuery q;
    q.intent = "teleport_raster";
    auto r = routeCapabilities( q );
    REQUIRE( !r.entries.empty() );
    CHECK( r.entries[0].status == "impossible" );
    CHECK( r.intentStatus == "unresolved" );
}

TEST_CASE( "registry and asset invalidation", "[science_context]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "x", "surface_reflectance", { "red", "nir" } );
    broker.assets().setResolver( [&]( const std::string &key ) -> PassportResolution {
        if ( key == state.assetId )
            return PassportResolution{ state, "", "" };
        return PassportResolution{ std::nullopt, "asset_not_found", "" };
    } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.assetKeys = { "x" };
    auto r1 = broker.synthesize( req );
    CHECK( r1.cacheHit == false );
    auto r2 = broker.synthesize( req );
    CHECK( r2.cacheHit == true );
    broker.assets().setCatalogGeneration( 2 ); // invalidates asset cache + key changes
    broker.cache().clear();
    broker.recipes().setRegistryRevision( 99 );
    auto r3 = broker.synthesize( req );
    CHECK( r3.cacheHit == false );
}

TEST_CASE( "autonomy L2 no autonomous exec", "[science_context]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    req.constraints.autonomyLevel = "L2";
    auto result = broker.synthesize( req );
    CHECK( result.bundle.constraints.allowAutonomousExec == false );
    bool found = false;
    for ( const auto &q : result.bundle.openQuestions )
    {
        if ( q.find( "no_autonomous_exec" ) != std::string::npos )
            found = true;
    }
    CHECK( found );
    auto planning = result.planning;
    bool lim = false;
    for ( const auto &l : planning.limitations )
    {
        if ( l.isString() && l.asString().find( "autonomy_forbids" ) != std::string::npos )
            lim = true;
    }
    CHECK( lim );
}

TEST_CASE( "offline mode noted", "[science_context]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    req.constraints.offline = true;
    auto result = broker.synthesize( req );
    CHECK( result.bundle.constraints.offline == true );
    bool found = false;
    for ( const auto &q : result.bundle.openQuestions )
    {
        if ( q.find( "offline" ) != std::string::npos )
            found = true;
    }
    CHECK( found );
}

TEST_CASE( "truncation metadata", "[science_context]" )
{
    auto broker = makeBroker();
    // Add many recipes
    std::vector<RecipeDocument> many;
    for ( int i = 0; i < 20; ++i )
    {
        RecipeDocument d;
        d.recipeId = "lab.r" + std::to_string( i );
        d.title = "r";
        d.intent = "ndvi";
        d.modality = "optical";
        d.keywords = { "ndvi" };
        many.push_back( d );
    }
    broker.recipes().setRecipes( many );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    req.budget.maxRecipes = 3;
    req.budget.maxBytes = 65536;
    auto result = broker.synthesize( req );
    CHECK( result.bundle.recipes.size() <= 3 );
    CHECK( result.bundle.truncation.truncated == true );
    CHECK( result.bundle.truncation.droppedRecipes > 0 );
}

TEST_CASE( "over-budget byte trim", "[science_context]" )
{
    auto broker = makeBroker();
    std::vector<RecipeDocument> many;
    for ( int i = 0; i < 30; ++i )
    {
        RecipeDocument d;
        d.recipeId = "lab.big" + std::to_string( i );
        d.title = std::string( 80, 'x' );
        d.intent = "ndvi";
        d.modality = "optical";
        d.keywords = { "ndvi", "vegetation", "index" };
        many.push_back( d );
    }
    broker.recipes().setRecipes( many );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi vegetation index analysis please";
    req.intent = "ndvi";
    req.passports = { state };
    req.budget.maxRecipes = 20;
    req.budget.maxBytes = 1800; // force byte trim below a full multi-recipe bundle
    auto result = broker.synthesize( req );
    CHECK( result.bundle.truncation.truncated == true );
    const int serialized = static_cast<int>( serializeBundle( result.bundle ).size() );
    CHECK( serialized == result.bundle.truncation.finalBytes );
    CHECK( serialized <= req.budget.maxBytes );
    CHECK( result.bundle.truncation.originalBytes > result.bundle.truncation.finalBytes );
}

TEST_CASE( "hostile JSON schema rejected", "[science_context]" )
{
    ScientificContextBundle b;
    Json::Value bad( Json::objectValue );
    bad["schema"] = "exp.science_context.v999";
    bad["goal"] = "x";
    std::string err;
    CHECK_FALSE( bundleFromJson( bad, b, &err ) );
    CHECK( err.find( "schema" ) != std::string::npos );

    Json::Value hostile( Json::arrayValue );
    hostile.append( "nope" );
    CHECK_FALSE( bundleFromJson( hostile, b, &err ) );
}

TEST_CASE( "tool parity scientific:context and capabilities", "[science_context]" )
{
    auto broker = makeBroker();
    agent_adapter::setSharedBrokerForTest( &broker );
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    Json::Value args( Json::objectValue );
    args["goal"] = "ndvi";
    args["intent"] = "ndvi";
    args["passport"] = assetStateToJson( state );
    args["autonomy_level"] = "L2";
    auto ctx = agent_adapter::scientificContext( args );
    CHECK( ctx["schema"].asString() == kBundleSchemaId );
    CHECK( ctx.isMember( "planning_context" ) );
    CHECK( ctx["counts"]["capabilities"].asInt() >= 1 );
    CHECK( ctx.isMember( "truncated" ) );

    auto caps = agent_adapter::scientificCapabilities( args );
    CHECK( caps["capabilities"].isArray() );
    CHECK( caps["capabilities"][0]["status"].asString() == "direct" );

    auto passport = agent_adapter::dataAssetPassport( args );
    CHECK( passport["path_hint"].asString() == "sr.tif" ); // basename only
    CHECK( passport["path_hint"].asString().find( '/' ) == std::string::npos );

    auto recipes = agent_adapter::recipeSearch( args );
    CHECK( recipes["auto_execute"].asBool() == false );
    CHECK( recipes["hits"].isArray() );
    agent_adapter::setSharedBrokerForTest( nullptr );
}

TEST_CASE( "non-ASCII path hint redaction", "[science_context]" )
{
    auto state = makeOptical( "cn", "surface_reflectance", { "red", "nir" } );
    state.sourcePath = "/数据/场景/植被.tif";
    auto summary = AssetStateProvider::summarize( state );
    CHECK( summary.pathHint == "植被.tif" );
    CHECK( summary.pathHint.find( '/' ) == std::string::npos );
}

TEST_CASE( "byte-stable bundle same inputs", "[science_context]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    req.useCache = false;
    auto a = broker.synthesize( req );
    auto b = broker.synthesize( req );
    CHECK( serializeBundle( a.bundle ) == serializeBundle( b.bundle ) );
    CHECK( a.bundle.bundleId == b.bundle.bundleId );
}

TEST_CASE( "conflicted radiometric never auto-picked", "[science_context]" )
{
    auto state = makeOptical( "c", "digital_number", { "red", "nir" }, ClaimKind::Conflicted );
    auto summary = AssetStateProvider::summarize( state );
    CHECK( summary.evidence == EvidenceBucket::Conflicted );
    CHECK( summary.radiometricUnit.empty() );
    auto obs = observedStateFromPassport( state );
    CHECK_FALSE( obs.isMember( "radiometric_state" ) );
    CHECK( obs["radiometric_conflicted"].asBool() == true );
}

TEST_CASE( "DoD path passport to planner projection", "[science_context]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "compute NDVI";
    req.intent = "ndvi";
    req.passports = { state };
    req.constraints.autonomyLevel = "L2";
    auto result = broker.synthesize( req );
    CHECK( result.bundle.schemaId == kBundleSchemaId );
    REQUIRE( !result.bundle.assets.empty() );
    REQUIRE( !result.bundle.capabilities.empty() );
    CHECK( result.bundle.capabilities[0].status == "direct" );
    REQUIRE( !result.bundle.recipes.empty() );
    CHECK( result.planning.intent == "ndvi" );
    CHECK( result.bundle.planner.inputFacts.isMember( "primary" ) );
    auto compileReq = planningContextToCompileRequest( result.planning );
    CHECK( compileReq["intent"].asString() == "ndvi" );
    CHECK( compileReq.isMember( "input_facts" ) );
    CHECK( compileReq["execution_blocked"].asBool() == false );
}

TEST_CASE( "tool errors on hostile args", "[science_context]" )
{
    CHECK_THROWS_AS( agent_adapter::scientificContext( Json::Value( Json::arrayValue ) ),
                     std::runtime_error );
    Json::Value args( Json::objectValue );
    args["passport_json"] = "{not json";
    CHECK_THROWS_AS( agent_adapter::dataAssetPassport( args ), std::runtime_error );
}

// ---------------------------------------------------------------------------
// R3 Track 03 — live authority / cache invalidation / budget oracles.
// ---------------------------------------------------------------------------

TEST_CASE( "inline passport content change must not hit stale bundle",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "mut", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    auto first = broker.synthesize( req );
    REQUIRE( first.cacheHit == false );
    REQUIRE( first.bundle.assets.size() == 1 );
    CHECK( first.bundle.assets[0].crsAuthid == "EPSG:4326" );

    // Same asset id, same revision, NEW content (re-imported file): the
    // content-sensitive digest must miss, never serve the old projection.
    state.geometry.crsAuthid = "EPSG:3857";
    SynthesizeRequest req2 = req;
    req2.passports = { state };
    auto second = broker.synthesize( req2 );
    CHECK( second.cacheHit == false );
    REQUIRE( second.bundle.assets.size() == 1 );
    CHECK( second.bundle.assets[0].crsAuthid == "EPSG:3857" );
}

TEST_CASE( "invalidateAsset forces re-resolution and serves new content",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "mut", "surface_reflectance", { "red", "nir" } );
    int resolverCalls = 0;
    broker.assets().setResolver(
        [&]( const std::string &key ) -> PassportResolution {
            if ( key != state.assetId )
                return PassportResolution{ std::nullopt, "asset_not_found", "" };
            ++resolverCalls;
            return PassportResolution{ state, "", "" };
        } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.assetKeys = { "mut" };
    auto first = broker.synthesize( req );
    REQUIRE( first.cacheHit == false );

    // Authority reinstall keeps id AND revision but the content changed; the
    // documented invalidation hook must force the resolver to be consulted
    // again and the new content to be served.
    state.radiometric.unit = "digital_number";
    broker.invalidateAsset( "mut" );
    auto second = broker.synthesize( req );
    CHECK( second.cacheHit == false );
    CHECK( resolverCalls >= 2 );
    REQUIRE( second.bundle.assets.size() == 1 );
    CHECK( second.bundle.assets[0].radiometricUnit == "digital_number" );
}

TEST_CASE( "CRS conflict is detected across more than two assets", "[science_context][r3]" )
{
    auto a = makeOptical( "a", "surface_reflectance", { "red", "nir" } );
    auto b = makeOptical( "b", "surface_reflectance", { "red", "nir" } );
    auto c = makeOptical( "c", "surface_reflectance", { "red", "nir" } );
    c.geometry.crsAuthid = "EPSG:3857"; // conflict is between b and c, not a vs b
    auto broker = makeBroker();
    SynthesizeRequest req;
    req.goal = "compute NDVI";
    req.intent = "ndvi";
    req.passports = { a, b, c };
    auto result = broker.synthesize( req );
    REQUIRE( !result.bundle.capabilities.empty() );
    CHECK( result.bundle.capabilities[0].status == "unavailable" );
    REQUIRE( !result.bundle.capabilities[0].reasons.empty() );
    CHECK( result.bundle.capabilities[0].reasons[0].find( "CRS_GRID" ) != std::string::npos );
}

TEST_CASE( "hostile long intent stays inside the byte budget", "[science_context][r3]" )
{
    auto broker = makeBroker();
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = std::string( 100000, 'x' ); // operator echo inflates open questions
    req.budget.maxBytes = 8192;
    auto result = broker.synthesize( req );
    const int serialized = static_cast<int>( serializeBundle( result.bundle ).size() );
    INFO( "serialized=" << serialized << " finalBytes=" << result.bundle.truncation.finalBytes );
    CHECK( serialized <= req.budget.maxBytes );
    CHECK( result.bundle.truncation.truncated == true );
    // The planner projection must mirror the budgeted question set — dropped
    // questions must not survive in a secondary section.
    CHECK( static_cast<int>( result.bundle.planner.openQuestions.size() ) <=
           static_cast<int>( result.bundle.openQuestions.size() ) );
}

TEST_CASE( "hostile long goal is bounded and explicitly marked", "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "sr", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = std::string( 100000, 'y' );
    req.intent = "ndvi";
    req.passports = { state };
    req.budget.maxBytes = 8192;
    auto result = broker.synthesize( req );
    const int serialized = static_cast<int>( serializeBundle( result.bundle ).size() );
    INFO( "serialized=" << serialized << " truncated=" << result.bundle.truncation.truncated );
    // The goal is the ONLY oversized field here, so the emitted bundle must
    // fit the budget — not merely be flagged.
    CHECK( serialized <= req.budget.maxBytes );
    CHECK( result.bundle.truncation.truncated == true );
    CHECK( std::find( result.bundle.truncation.sections.begin(),
                      result.bundle.truncation.sections.end(),
                      "goal" ) != result.bundle.truncation.sections.end() );
}

TEST_CASE( "asset A to B to A requests never cross cache contexts",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    const auto assetA = makeOptical( "assetA", "surface_reflectance", { "red", "nir" } );
    const auto assetB = makeSar( "assetB" ); // different modality → different bundle
    SynthesizeRequest a, b;
    a.goal = "analyze";
    a.intent = "ndvi";
    a.passports = { assetA };
    b.goal = "analyze";
    b.intent = "sar_change";
    b.passports = { assetB };

    for ( int round = 0; round < 3; ++round )
    {
        const auto ra = broker.synthesize( a );
        const auto rb = broker.synthesize( b );
        REQUIRE( ra.bundle.assets.size() == 1 );
        REQUIRE( rb.bundle.assets.size() == 1 );
        CHECK( ra.bundle.assets[0].assetId == "assetA" );
        CHECK( rb.bundle.assets[0].assetId == "assetB" );
        CHECK( ra.bundle.assets[0].modality != rb.bundle.assets[0].modality );
    }
    // Steady state: both directions served from cache with the right content.
    CHECK( broker.synthesize( a ).cacheHit == true );
    CHECK( broker.synthesize( b ).cacheHit == true );
    auto aAgain = broker.synthesize( a );
    REQUIRE( aAgain.bundle.assets.size() == 1 );
    CHECK( aAgain.bundle.assets[0].assetId == "assetA" );
    CHECK( aAgain.bundle.assets[0].radiometricUnit == "surface_reflectance" );
}

TEST_CASE( "one byte of goal difference produces a different bundle",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "one", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req1;
    req1.goal = "compute an ndvi mosaic";
    req1.intent = "ndvi";
    req1.passports = { state };
    SynthesizeRequest req2 = req1;
    req2.goal = "compute an ndvi mosaicX"; // exactly one byte more

    const auto first = broker.synthesize( req1 );
    const auto second = broker.synthesize( req2 );
    CHECK( first.cacheHit == false );
    CHECK( second.cacheHit == false ); // must not collide onto the first key
    CHECK( first.bundle.goal != second.bundle.goal );
    CHECK( first.bundle.bundleId != second.bundle.bundleId );
    // Repeats of each still hit their own entry.
    CHECK( broker.synthesize( req1 ).cacheHit == true );
    CHECK( broker.synthesize( req2 ).cacheHit == true );
}

TEST_CASE( "duplicate request serves identical bytes from cache", "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "dup", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    auto a = broker.synthesize( req );
    auto b = broker.synthesize( req );
    CHECK( b.cacheHit == true );
    CHECK( serializeBundle( a.bundle ) == serializeBundle( b.bundle ) );
    CHECK( a.bundle.bundleId == b.bundle.bundleId );
}

TEST_CASE( "cache eviction is LRU, bounded, and counted", "[science_context][r3]" )
{
    ScienceContextBroker broker;
    seedRecipes( broker.recipes() );
    auto state = makeOptical( "lru", "surface_reflectance", { "red", "nir" } );
    const int overflow = 6;
    for ( int i = 0; i < ContextCache::kMaxEntries + overflow; ++i )
    {
        SynthesizeRequest req;
        req.goal = "goal " + std::to_string( i );
        req.intent = "ndvi";
        req.passports = { state };
        broker.synthesize( req );
    }
    CHECK( broker.cache().size() == ContextCache::kMaxEntries );
    CHECK( broker.cache().evictions() == static_cast<std::uint64_t>( overflow ) );

    // Deterministic victims: requests 0..(overflow-1) were evicted in order,
    // request `overflow` and the LAST request are kept — regardless of
    // unordered_map iteration order. Retention is probed FIRST: a miss
    // re-inserts the entry and itself churns the cache.
    auto request = [&]( int i ) {
        SynthesizeRequest req;
        req.goal = "goal " + std::to_string( i );
        req.intent = "ndvi";
        req.passports = { state };
        return broker.synthesize( req );
    };
    CHECK( request( overflow ).cacheHit == true );
    CHECK( request( ContextCache::kMaxEntries + overflow - 1 ).cacheHit == true );
    CHECK( request( 0 ).cacheHit == false );
    CHECK( request( overflow - 1 ).cacheHit == false );

    // Hits/misses are counted for invalidation-cost probes.
    CHECK( broker.cache().hits() >= 2 );
    CHECK( broker.cache().misses() >= 2 );
}

TEST_CASE( "capability authority revision change invalidates cached bundles",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "cr", "surface_reflectance", { "red", "nir" } );
    std::uint64_t authorityRevision = 1;
    CapabilityFactsLookup lookup;
    lookup.authority = "capability_knowledge.test";
    lookup.entriesForIntent = []( const std::string &intent ) -> std::vector<Json::Value> {
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
    lookup.revision = [&authorityRevision] { return authorityRevision; };
    broker.setCapabilityFacts( lookup );

    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    req.passports = { state };
    CHECK( broker.synthesize( req ).cacheHit == false );
    CHECK( broker.synthesize( req ).cacheHit == true );

    // Authority reinstall: same request, advanced authority revision ⇒ miss.
    authorityRevision = 2;
    SynthesizeRequest req2 = req;
    req2.passports = { state };
    auto after = broker.synthesize( req2 );
    CHECK( after.cacheHit == false );
    CHECK( after.bundle.sources.capabilities.revision == std::uint64_t{ 2 } );
    CHECK( broker.synthesize( req2 ).cacheHit == true );
}

TEST_CASE( "GDAL open failure keeps its typed reason in the bundle",
           "[science_context][r3]" )
{
    ScienceContextBroker broker;
    AssetFactSources sources;
    sources.dataset = gdalDatasetFactsCollector();
    broker.assets().setResolver( makeFactsBasedResolver( sources ) );
    SynthesizeRequest req;
    req.goal = "inspect";
    req.assetKeys = { "/definitely/not/here.tif" };
    auto result = broker.synthesize( req );
    REQUIRE( result.bundle.assets.size() == 1 );
    CHECK( result.bundle.assets[0].evidence == EvidenceBucket::Unknown );
    bool gdalTyped = false;
    for ( const auto &q : result.bundle.openQuestions )
        gdalTyped = gdalTyped ||
                    q.rfind( "asset_resolve_failed:gdal_open_failed", 0 ) == 0;
    CHECK( gdalTyped );
    CHECK( result.bundle.sources.assets.source == ContentSource::Unavailable );
}

TEST_CASE( "resolver failures and missing assets stay typed and distinct",
           "[science_context][r3]" )
{
    ScienceContextBroker broker;
    broker.assets().setResolver(
        []( const std::string &key ) -> PassportResolution {
            if ( key == "gone" )
                return PassportResolution{ std::nullopt, "asset_not_found", "" };
            if ( key == "broken" )
                return PassportResolution{ std::nullopt, "gdal_open_failed",
                                           "not a raster (unit test)" };
            return PassportResolution{ std::nullopt, "", "" };
        } );
    SynthesizeRequest req;
    req.goal = "inspect";
    req.assetKeys = { "gone", "broken" };
    auto result = broker.synthesize( req );
    bool sawMissing = false;
    bool sawGdal = false;
    for ( const auto &q : result.bundle.openQuestions )
    {
        sawMissing = sawMissing || q.rfind( "asset_resolve_failed:asset_not_found", 0 ) == 0;
        sawGdal = sawGdal ||
                  q.rfind( "asset_resolve_failed:gdal_open_failed:not a raster", 0 ) == 0;
    }
    CHECK( sawMissing );
    CHECK( sawGdal );
}

namespace
{

/// Strict UTF-8 shape check: every lead byte announces a sequence length and
/// every continuation byte completes one. A cut mid-sequence fails this.
bool validUtf8( const std::string &s )
{
    int continuation = 0;
    for ( const unsigned char c : s )
    {
        if ( continuation > 0 )
        {
            if ( ( c & 0xC0 ) != 0x80 )
                return false;
            --continuation;
        }
        else if ( ( c & 0x80 ) == 0 )
            continue;
        else if ( ( c & 0xE0 ) == 0xC0 )
            continuation = 1;
        else if ( ( c & 0xF0 ) == 0xE0 )
            continuation = 2;
        else if ( ( c & 0xF8 ) == 0xF0 )
            continuation = 3;
        else
            return false;
    }
    return continuation == 0;
}

} // namespace

TEST_CASE( "hostile multi-byte content stays valid UTF-8 under the budget",
           "[science_context][r3]" )
{
    auto broker = makeBroker();
    auto state = makeOptical( "utf", "surface_reflectance", { "red", "nir" } );
    SynthesizeRequest req;
    // Valid multi-byte input only: 40,000 U+6C34 (水, 3 bytes each) ≈ 120 KB.
    req.goal = std::string( 40000, '\x0' );
    req.goal.clear();
    for ( int i = 0; i < 40000; ++i )
        req.goal += "\xE6\xB0\xB4";
    req.intent = "ndvi";
    req.passports = { state };
    req.budget.maxBytes = 8192;
    auto result = broker.synthesize( req );
    REQUIRE( result.bundle.truncation.truncated );
    const std::string serialized = serializeBundle( result.bundle );

    Json::Value json;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( serialized.data(), serialized.data() + serialized.size(),
                            &json, &errors ) );
    // The budgeted goal must still be well-formed UTF-8: no orphaned
    // continuation bytes at the cut, marker visible on the wire.
    const Json::Value cutGoal = json["goal"];
    REQUIRE( cutGoal.isString() );
    const std::string emitted = cutGoal.asString();
    CHECK( validUtf8( emitted ) );
    CHECK( emitted.size() < req.goal.size() );
    CHECK( emitted.rfind( "[~truncated]" ) != std::string::npos );
    // Every string field in the bundle passes the same shape check.
    for ( const auto &q : json["open_questions"] )
        CHECK( validUtf8( q.asString() ) );
    CHECK( validUtf8( json["intent"].asString() ) );
}

TEST_CASE( "asset trims are counted in truncation metadata", "[science_context][r3]" )
{
    ScienceContextBroker broker;
    seedRecipes( broker.recipes() );
    SynthesizeRequest req;
    req.goal = "ndvi";
    req.intent = "ndvi";
    for ( int i = 0; i < 12; ++i )
        req.passports.push_back( makeOptical( "asset" + std::to_string( i ),
                                              "surface_reflectance", { "red", "nir" } ) );
    req.budget.maxAssets = 3;
    auto result = broker.synthesize( req );
    CHECK( static_cast<int>( result.bundle.assets.size() ) <= 3 );
    CHECK( result.bundle.truncation.droppedAssets >= 9 );
    CHECK( result.bundle.truncation.truncated == true );
}
