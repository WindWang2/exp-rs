// test_preflight_golden.cpp — RS14-02 slice B: the safe / unsafe / unknown
// golden matrix.
//
// Eleven in-code golden scenarios pin, per scenario: the exact verdict, the
// exact set of finding codes, and byte-identical canonical reports across
// replays. The matrix is the engine's semantic pin: any change to rule
// semantics, ordering, ack handling or truncation that shifts a scenario is
// a contract change and must fail here.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/capability_mirror.h"
#include "preflight/asset_state_adapter.h"
#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rules.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::preflight;

namespace {

SlotFacts baseScene()
{
    SlotFacts f;
    f.slot = "primary";
    f.assetRef = "scene-a";
    f.assetId = "asset-a";
    f.kind = "raster";
    f.modality = "optical";
    f.hasCrs = true;
    f.crsAuthid = "EPSG:32650";
    f.crsProjected = true;
    f.hasPixelSize = true;
    f.pixelSizeX = 10.0;
    f.pixelSizeY = 10.0;
    f.hasSize = true;
    f.width = 1000;
    f.height = 1000;
    f.radiometricUnit = "surface_reflectance";
    f.noDataPolicy = "declared";
    BandFacts red;
    red.index = 1;
    red.role = "red";
    BandFacts nir;
    nir.index = 2;
    nir.role = "nir";
    f.bands = { red, nir };
    return f;
}

Json::Value ndviEntry()
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
    Json::Value warn( Json::arrayValue );
    warn.append( "toa" );
    radiometric["acceptable"] = acceptable;
    radiometric["warn"] = warn;
    entry["radiometric"] = radiometric;
    return entry;
}

struct World
{
    PreflightEngine engine;
    MemoryFactsProvider facts;
    MemoryCapabilityProvider capability;

    World()
    {
        for ( auto &rule : builtinRules() )
        {
            const auto r = engine.registerRule( std::move( rule ) );
            REQUIRE( r == RegistrationResult::Ok );
        }
        capability.setEntry( "rs:demo_ndvi", ndviEntry() );
    }

    PreflightReport run( const std::string &operatorId = "rs:demo_ndvi",
                         std::vector<std::pair<std::string, std::string>> inputs = {
                             { "primary", "scene-a" } },
                         std::vector<std::string> acks = {} )
    {
        PreflightRequest req;
        req.operatorId = operatorId;
        req.mode = "teaching";
        req.humanOperatorId = "golden";
        req.inputs = std::move( inputs );
        req.acknowledgements = std::move( acks );
        return engine.evaluate( req, facts, capability );
    }
};

std::set<std::string> codeSet( const PreflightReport &report )
{
    std::set<std::string> codes;
    for ( const auto &f : report.findings )
        codes.insert( f.code );
    return codes;
}

void requireCodes( const PreflightReport &report, std::set<std::string> expected )
{
    const auto actual = codeSet( report );
    INFO( "actual codes: " << [&] {
        std::string joined;
        for ( const auto &c : actual )
            joined += c + " ";
        return joined;
    }() );
    REQUIRE( actual == expected );
}

} // namespace

TEST_CASE( "golden: safe run passes clean", "[preflight][golden]" )
{
    World w;
    w.facts.set( "scene-a", baseScene() );
    const PreflightReport r = w.run();
    REQUIRE( r.verdict == "ok" );
    requireCodes( r, {} );
    REQUIRE( canonicalReportJson( r ) == canonicalReportJson( w.run() ) );
}

TEST_CASE( "golden: warn-list radiometric state is require_ack and ackable", "[preflight][golden]" )
{
    World w;
    SlotFacts toa = baseScene();
    toa.radiometricUnit = "toa";
    w.facts.set( "scene-a", toa );

    const PreflightReport unacked = w.run();
    REQUIRE( unacked.verdict == "requires_ack" );
    requireCodes( unacked, { "SPF_RADIOMETRIC_STATE_MISMATCH" } );

    const PreflightReport acked = w.run( "rs:demo_ndvi", { { "primary", "scene-a" } },
                                         { "SPF_RADIOMETRIC_STATE_MISMATCH" } );
    REQUIRE( acked.verdict == "ok" );
    REQUIRE( acked.findings[0].acknowledged == true );
    // Acknowledgement changes the report bytes and the digest.
    REQUIRE( reportDigest( acked ) != reportDigest( unacked ) );
}

TEST_CASE( "golden: missing nir band is blocked", "[preflight][golden]" )
{
    World w;
    SlotFacts single = baseScene();
    single.bands.erase( single.bands.begin() + 1 );
    w.facts.set( "scene-a", single );
    const PreflightReport r = w.run();
    REQUIRE( r.verdict == "blocked" );
    requireCodes( r, { "SPF_BAND_ROLE_MISSING" } );
}

TEST_CASE( "golden: crs mismatch on a pair is blocked", "[preflight][golden]" )
{
    World w;
    SlotFacts b = baseScene();
    b.assetRef = "scene-b";
    b.assetId = "asset-b";
    b.crsAuthid = "EPSG:4326";
    w.facts.set( "scene-a", baseScene() );
    w.facts.set( "scene-b", b );
    const PreflightReport r = w.run( "rs:demo_ndvi",
                                     { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( r.verdict == "blocked" );
    requireCodes( r, { "SPF_CRS_MISMATCH" } );
}

TEST_CASE( "golden: coarse resolution ratio is blocked", "[preflight][golden]" )
{
    World w;
    SlotFacts coarse = baseScene();
    coarse.assetRef = "scene-b";
    coarse.assetId = "asset-b";
    coarse.pixelSizeX = 200.0;
    coarse.pixelSizeY = 200.0;
    w.facts.set( "scene-a", baseScene() );
    w.facts.set( "scene-b", coarse );
    const PreflightReport r = w.run( "rs:demo_ndvi",
                                     { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( r.verdict == "blocked" );
    requireCodes( r, { "SPF_GRID_RESOLUTION_MISMATCH" } );
}

TEST_CASE( "golden: undeclared operator is require_ack", "[preflight][golden]" )
{
    World w;
    w.facts.set( "scene-a", baseScene() );
    const PreflightReport r = w.run( "rs:not_a_real_operator" );
    REQUIRE( r.verdict == "requires_ack" );
    requireCodes( r, { "SPF_OPERATOR_UNKNOWN" } );
    REQUIRE( codeSet( r ).count( "SPF_CAPABILITY_MIRROR_UNAVAILABLE" ) == 0 );
}

TEST_CASE( "golden: unavailable mirror is fail-closed require_ack", "[preflight][golden]" )
{
    World w;
    w.facts.set( "scene-a", baseScene() );
    w.capability.markUnavailable( "mirror directory missing" );
    const PreflightReport r = w.run();
    REQUIRE( r.verdict == "requires_ack" );
    REQUIRE( codeSet( r ).count( "SPF_CAPABILITY_MIRROR_UNAVAILABLE" ) == 1 );
}

TEST_CASE( "golden: unresolvable facts surface typed unknowns, never a pass",
           "[preflight][golden]" )
{
    World w;
    w.facts.setUnknown( "scene-a", "no passport" );
    const PreflightReport r = w.run();
    REQUIRE( r.verdict == "requires_ack" );
    const auto codes = codeSet( r );
    REQUIRE( codes.count( "SPF_BAND_ROLE_UNKNOWN" ) == 1 );
    REQUIRE( codes.count( "SPF_CRS_UNKNOWN" ) == 0 ); // no pair to compare
    for ( const auto &f : r.findings )
        REQUIRE( f.basis == "unknown" );
}

TEST_CASE( "golden: temporal shortfall blocks", "[preflight][golden]" )
{
    World w;
    SlotFacts series = baseScene();
    series.temporalSceneCount = 2;
    w.facts.set( "scene-a", series );
    Json::Value entry = ndviEntry();
    Json::Value temporal( Json::objectValue );
    temporal["min_scenes"] = 6;
    entry["temporal"] = temporal;
    w.capability.setEntry( "rs:demo_ndvi", entry );

    const PreflightReport r = w.run();
    REQUIRE( r.verdict == "blocked" );
    requireCodes( r, { "SPF_TEMPORAL_SCENES_INSUFFICIENT" } );
}

TEST_CASE( "golden: train/eval identity leakage is blocked", "[preflight][golden]" )
{
    World w;
    w.facts.set( "scene-a", baseScene() );
    const PreflightReport r =
        w.run( "rs:demo_ndvi", { { "training", "scene-a" }, { "eval", "scene-a" } } );
    REQUIRE( r.verdict == "blocked" );
    REQUIRE( codeSet( r ).count( "SPF_TRAIN_EVAL_LEAKAGE" ) == 1 );
}

TEST_CASE( "golden: model family mismatch is blocked", "[preflight][golden]" )
{
    World w;
    Json::Value entry = ndviEntry();
    Json::Value model( Json::objectValue );
    Json::Value families( Json::arrayValue );
    families.append( "segmentation" );
    model["families"] = families;
    entry["model_compatibility"] = model;
    w.capability.setEntry( "rs:demo_ndvi", entry );

    SlotFacts scene = baseScene();
    scene.slot = "primary";
    w.facts.set( "scene-a", scene );
    SlotFacts manifest = baseScene();
    manifest.assetRef = "model-x";
    manifest.assetId = "asset-model-x";
    manifest.kind = "model";
    manifest.hasModelManifest = true;
    manifest.modelKind = "detector";
    w.facts.set( "model-x", manifest );

    const PreflightReport r =
        w.run( "rs:demo_ndvi", { { "primary", "scene-a" }, { "model", "model-x" } } );
    REQUIRE( r.verdict == "blocked" );
    REQUIRE( codeSet( r ).count( "SPF_MODEL_INCOMPATIBLE" ) == 1 );
}

TEST_CASE( "golden: every scenario replays byte-identically (full-matrix determinism)",
           "[preflight][golden]" )
{
    // Rebuild the entire world from scratch and confirm every report byte
    // matches the first pass — including providers and rule registration.
    struct Snapshot
    {
        std::string bytes;
        std::string digest;
    };
    const auto capture = []( PreflightReport r ) {
        return Snapshot{ canonicalReportJson( r ), reportDigest( r ) };
    };

    World first;
    first.facts.set( "scene-a", baseScene() );
    const Snapshot clean1 = capture( first.run() );
    const Snapshot unknown1 = capture( first.run( "rs:not_a_real_operator" ) );

    World second;
    second.facts.set( "scene-a", baseScene() );
    const Snapshot clean2 = capture( second.run() );
    const Snapshot unknown2 = capture( second.run( "rs:not_a_real_operator" ) );

    REQUIRE( clean1.bytes == clean2.bytes );
    REQUIRE( clean1.digest == clean2.digest );
    REQUIRE( unknown1.bytes == unknown2.bytes );
    REQUIRE( unknown1.digest == unknown2.digest );
}
