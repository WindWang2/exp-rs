// test_science_context_broker.cpp — Science Context Broker scenarios (A–H DoD).
#include <catch2/catch_test_macros.hpp>

#include "science_context/agent_adapter.h"
#include "science_context/broker.h"
#include "science_context/bundle.h"
#include "science_context/capability_router.h"
#include "science_context/observed_state.h"
#include "scientific_state/asset_state_types.h"
#include "scientific_state/asset_state_json.h"

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
    broker.assets().setResolver( [&]( const std::string &key ) -> std::optional<RemoteSensingAssetState> {
        if ( key == state.assetId )
            return state;
        return std::nullopt;
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
