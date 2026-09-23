// test_preflight_rules.cpp — RS14-02 slice B: the ten builtin rule families.
//
// Facts enter rules ONLY through the provider seam (RuleFacts); a rule that
// cannot verify emits <CODE>_UNKNOWN require_ack with basis "unknown" and an
// insufficient_facts trace — never a silent pass, never a fabricated fact.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rule.h"
#include "preflight/rules.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::preflight;

namespace {

SlotFacts opticalScene()
{
    SlotFacts f;
    f.slot = "primary";
    f.assetRef = "scene-a";
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

Json::Value ndviCapability()
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
    acceptable.append( "toa_reflectance" );
    radiometric["acceptable"] = acceptable;
    Json::Value warn( Json::arrayValue );
    warn.append( "toa" );
    radiometric["warn"] = warn;
    entry["radiometric"] = radiometric;
    Json::Value crs( Json::objectValue );
    crs["requires_projected"] = true;
    entry["crs"] = crs;
    return entry;
}

MemoryCapabilityProvider ndviProvider()
{
    MemoryCapabilityProvider cap;
    cap.setEntry( "rs:demo_ndvi", ndviCapability() );
    return cap;
}

struct Loaded
{
    PreflightEngine engine;
    MemoryFactsProvider facts;
    MemoryCapabilityProvider capability;

    explicit Loaded( Json::Value capabilityEntry = ndviCapability() )
    {
        for ( auto &rule : builtinRules() )
            REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );
        if ( !capabilityEntry.isNull() )
            capability.setEntry( "rs:demo_ndvi", std::move( capabilityEntry ) );
    }

    PreflightReport run( std::vector<std::pair<std::string, std::string>> inputs = {
                             { "primary", "scene-a" } },
                         std::vector<std::string> acks = {} )
    {
        PreflightRequest req;
        req.operatorId = "rs:demo_ndvi";
        req.mode = "teaching";
        req.humanOperatorId = "op";
        req.inputs = std::move( inputs );
        req.acknowledgements = std::move( acks );
        return engine.evaluate( req, facts, capability );
    }
};

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

TEST_CASE( "builtinRules: ten rules, unique sorted ids, revisioned", "[preflight][rules]" )
{
    auto rules = builtinRules();
    REQUIRE( rules.size() == 10 );

    std::vector<std::string> ids;
    for ( auto &r : rules )
        ids.push_back( r->id() );
    REQUIRE( std::is_sorted( ids.begin(), ids.end() ) );
    REQUIRE( std::set<std::string>( ids.begin(), ids.end() ).size() == 10 );
    for ( const auto &id : ids )
    {
        REQUIRE( id.find( "preflight." ) == 0 );
    }
    // The full family list from the slice-B contract.
    for ( const char *family :
          { "preflight.band_role", "preflight.pair_crs", "preflight.pair_resolution_ratio",
            "preflight.radiometric_state_policy", "preflight.modality_policy",
            "preflight.quality_mask", "preflight.temporal_policy",
            "preflight.train_eval_leakage", "preflight.model_compatibility",
            "preflight.operator_known" } )
    {
        REQUIRE( std::find( ids.begin(), ids.end(), family ) != ids.end() );
    }
}

TEST_CASE( "clean NDVI scene passes every rule", "[preflight][rules]" )
{
    Loaded loaded;
    loaded.facts.set( "scene-a", opticalScene() );

    const PreflightReport report = loaded.run();
    REQUIRE( report.verdict == "ok" );
    REQUIRE( report.findings.empty() );
    for ( const auto &e : report.evaluated )
        REQUIRE( e.outcome == "pass" );
}

TEST_CASE( "band_role: missing required role blocks; unknown facts degrade typed",
           "[preflight][rules]" )
{
    Loaded loaded;
    SlotFacts scene = opticalScene();
    scene.bands.erase( scene.bands.begin() ); // drop red
    loaded.facts.set( "scene-a", scene );

    const PreflightReport report = loaded.run();
    REQUIRE( report.verdict == "blocked" );
    const auto block = findings( report, "SPF_BAND_ROLE_MISSING" );
    REQUIRE( block.size() == 1 );
    REQUIRE( block[0]->severity == PreflightSeverity::Block );
    REQUIRE( block[0]->basis == "observed" );
    REQUIRE( block[0]->evidence["role"].asString() == "red" );
    REQUIRE( block[0]->evidence["required"].asInt() == 1 );
    REQUIRE( block[0]->evidence["found"].asInt() == 0 );
    REQUIRE( trace( report, "preflight.band_role" )->outcome == "finding" );
}

TEST_CASE( "band_role: unknown slot facts produce SPF_BAND_ROLE_UNKNOWN, not a pass",
           "[preflight][rules]" )
{
    Loaded loaded;
    loaded.facts.setUnknown( "scene-a", "no passport for reference" );

    const PreflightReport report = loaded.run();
    const auto unknown = findings( report, "SPF_BAND_ROLE_UNKNOWN" );
    REQUIRE( unknown.size() == 1 );
    REQUIRE( unknown[0]->severity == PreflightSeverity::RequireAck );
    REQUIRE( unknown[0]->basis == "unknown" );
    REQUIRE( trace( report, "preflight.band_role" )->outcome == "insufficient_facts" );
}

TEST_CASE( "pair_crs: mismatching pair is blocked; unknown CRS is typed require_ack",
           "[preflight][rules]" )
{
    Loaded loaded;
    SlotFacts a = opticalScene();
    SlotFacts b = opticalScene();
    b.assetRef = "scene-b";
    b.crsAuthid = "EPSG:4326";
    b.crsProjected = false;
    loaded.facts.set( "scene-a", a );
    loaded.facts.set( "scene-b", b );

    const PreflightReport report =
        loaded.run( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( hasCode( report, "SPF_CRS_MISMATCH" ) );
    REQUIRE( findings( report, "SPF_CRS_MISMATCH" )[0]->severity == PreflightSeverity::Block );
    REQUIRE( findings( report, "SPF_CRS_MISMATCH" )[0]->affectedInputs.size() == 2 );

    Loaded unknownPair;
    SlotFacts c = opticalScene();
    c.hasCrs = false;
    unknownPair.facts.set( "scene-a", c );
    unknownPair.facts.set( "scene-b", opticalScene() );
    const PreflightReport unknownReport =
        unknownPair.run( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( hasCode( unknownReport, "SPF_CRS_UNKNOWN" ) );
    REQUIRE( findings( unknownReport, "SPF_CRS_UNKNOWN" )[0]->basis == "unknown" );
    REQUIRE( trace( unknownReport, "preflight.pair_crs" )->outcome == "insufficient_facts" );
}

TEST_CASE( "pair_resolution_ratio: ratio thresholds separate require_ack from block",
           "[preflight][rules]" )
{
    Loaded loaded;
    SlotFacts fine = opticalScene();
    SlotFacts coarse = opticalScene();
    coarse.assetRef = "scene-b";
    coarse.pixelSizeX = 25.0;
    coarse.pixelSizeY = 25.0;
    loaded.facts.set( "scene-a", fine );
    loaded.facts.set( "scene-b", coarse );

    const PreflightReport acked =
        loaded.run( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( hasCode( acked, "SPF_GRID_RESOLUTION_MISMATCH" ) );
    REQUIRE( findings( acked, "SPF_GRID_RESOLUTION_MISMATCH" )[0]->severity ==
             PreflightSeverity::RequireAck );

    Loaded blocking;
    SlotFacts veryCoarse = coarse;
    veryCoarse.pixelSizeX = 200.0;
    veryCoarse.pixelSizeY = 200.0;
    blocking.facts.set( "scene-a", fine );
    blocking.facts.set( "scene-b", veryCoarse );
    const PreflightReport blocked =
        blocking.run( { { "primary", "scene-a" }, { "secondary", "scene-b" } } );
    REQUIRE( hasCode( blocked, "SPF_GRID_RESOLUTION_MISMATCH" ) );
    REQUIRE( findings( blocked, "SPF_GRID_RESOLUTION_MISMATCH" )[0]->severity ==
             PreflightSeverity::Block );
}

TEST_CASE( "radiometric_state_policy: acceptable / warn / off-list / unknown",
           "[preflight][rules]" )
{
    // warn list unit -> require_ack
    Loaded warned;
    SlotFacts dn = opticalScene();
    dn.radiometricUnit = "toa";
    warned.facts.set( "scene-a", dn );
    const PreflightReport warnReport = warned.run();
    REQUIRE( findings( warnReport, "SPF_RADIOMETRIC_STATE_MISMATCH" )[0]->severity ==
             PreflightSeverity::RequireAck );

    // off-list unit -> block
    Loaded blocked;
    SlotFacts raw = opticalScene();
    raw.radiometricUnit = "digital_number";
    blocked.facts.set( "scene-a", raw );
    const PreflightReport blockReport = blocked.run();
    REQUIRE( findings( blockReport, "SPF_RADIOMETRIC_STATE_MISMATCH" )[0]->severity ==
             PreflightSeverity::Block );

    // undeclared unit -> typed unknown
    Loaded opaque;
    SlotFacts blank = opticalScene();
    blank.radiometricUnit.clear();
    opaque.facts.set( "scene-a", blank );
    const PreflightReport unknownReport = opaque.run();
    const auto unknown = findings( unknownReport, "SPF_RADIOMETRIC_STATE_UNKNOWN" );
    REQUIRE( unknown.size() == 1 );
    REQUIRE( unknown[0]->severity == PreflightSeverity::RequireAck );
    REQUIRE( unknown[0]->basis == "unknown" );
    REQUIRE( trace( unknownReport, "preflight.radiometric_state_policy" )->outcome ==
             "insufficient_facts" );
}

TEST_CASE( "modality_policy: SAR input under optical operator blocks; unknown typed",
           "[preflight][rules]" )
{
    Loaded loaded;
    SlotFacts sar = opticalScene();
    sar.modality = "sar";
    loaded.facts.set( "scene-a", sar );
    const PreflightReport report = loaded.run();
    REQUIRE( findings( report, "SPF_MODALITY_MISMATCH" )[0]->severity == PreflightSeverity::Block );

    Loaded opaque;
    SlotFacts mystery = opticalScene();
    mystery.modality = "unknown";
    opaque.facts.set( "scene-a", mystery );
    const PreflightReport unknownReport = opaque.run();
    const auto unknown = findings( unknownReport, "SPF_MODALITY_UNKNOWN" );
    REQUIRE( unknown.size() == 1 );
    REQUIRE( unknown[0]->severity == PreflightSeverity::RequireAck );
}

TEST_CASE( "quality_mask: cloud thresholds are boundary-exact", "[preflight][rules]" )
{
    // 30.0 exactly: no finding (threshold is exclusive).
    {
        Loaded loaded;
        SlotFacts edge = opticalScene();
        edge.hasCloudCover = true;
        edge.cloudCoverPercent = 30.0;
        loaded.facts.set( "scene-a", edge );
        REQUIRE_FALSE( hasCode( loaded.run(), "SPF_CLOUD_COVER_HIGH" ) );
    }
    // 30.5: require_ack.
    {
        Loaded loaded;
        SlotFacts cloudy = opticalScene();
        cloudy.hasCloudCover = true;
        cloudy.cloudCoverPercent = 30.5;
        cloudy.qualityMaskInfo = "landsat_qa_pixel";
        loaded.facts.set( "scene-a", cloudy );
        const PreflightReport report = loaded.run();
        const auto hit = findings( report, "SPF_CLOUD_COVER_HIGH" );
        REQUIRE( hit.size() == 1 );
        REQUIRE( hit[0]->severity == PreflightSeverity::RequireAck );
        REQUIRE( hit[0]->evidence["cloud_cover_percent"].asDouble() == 30.5 );
    }
    // 70.0: block.
    {
        Loaded loaded;
        SlotFacts storm = opticalScene();
        storm.hasCloudCover = true;
        storm.cloudCoverPercent = 70.0;
        loaded.facts.set( "scene-a", storm );
        const PreflightReport report = loaded.run();
        REQUIRE( findings( report, "SPF_CLOUD_COVER_HIGH" )[0]->severity ==
                 PreflightSeverity::Block );
    }
}

TEST_CASE( "temporal_policy: scene shortfall blocks, gaps require ack, order blocks",
           "[preflight][rules]" )
{
    Json::Value entry = ndviCapability();
    Json::Value temporal( Json::objectValue );
    temporal["min_scenes"] = 3;
    temporal["requires_acquisition_time"] = true;
    temporal["max_gap_days"] = 16;
    entry["temporal"] = temporal;

    Loaded loaded( entry );
    SlotFacts scene = opticalScene();
    scene.hasAcquisitionTime = true;
    loaded.facts.set( "scene-a", scene );

    // Too few scenes -> block.
    loaded.facts.at( "scene-a" ).temporalSceneCount = 1;
    const PreflightReport shortReport = loaded.run();
    REQUIRE( findings( shortReport, "SPF_TEMPORAL_SCENES_INSUFFICIENT" )[0]->severity ==
             PreflightSeverity::Block );

    // Enough scenes, oversized gap -> require_ack.
    loaded.facts.at( "scene-a" ).temporalSceneCount = 3;
    loaded.facts.at( "scene-a" ).temporalDates = { "2026-01-01", "2026-02-15", "2026-03-01" };
    const PreflightReport gapReport = loaded.run();
    const auto gap = findings( gapReport, "SPF_TEMPORAL_GAP_EXCEEDED" );
    REQUIRE( gap.size() == 1 );
    REQUIRE( gap[0]->severity == PreflightSeverity::RequireAck );

    // Non-ascending dates -> block.
    loaded.facts.at( "scene-a" ).temporalDates = { "2026-03-01", "2026-01-01", "2026-02-01" };
    REQUIRE( findings( loaded.run(), "SPF_TEMPORAL_ORDER_INVALID" )[0]->severity ==
             PreflightSeverity::Block );

    // Missing acquisition time when required -> typed unknown.
    Loaded needsTime( entry );
    SlotFacts noTime = opticalScene();
    needsTime.facts.set( "scene-a", noTime );
    const PreflightReport unknownTime = needsTime.run();
    REQUIRE( hasCode( unknownTime, "SPF_TEMPORAL_TIME_UNKNOWN" ) );

    // Provider-declared truncation is never silent.
    Loaded truncated( entry );
    SlotFacts many = opticalScene();
    many.hasAcquisitionTime = true;
    many.temporalSceneCount = 40;
    many.temporalTruncated = true;
    truncated.facts.set( "scene-a", many );
    REQUIRE( hasCode( truncated.run(), "SPF_TEMPORAL_DATES_TRUNCATED" ) );
}

TEST_CASE( "train_eval_leakage: identical assets block; derived reuse blocks; unknown typed",
           "[preflight][rules]" )
{
    // Same reference on training and eval -> block.
    {
        Loaded loaded;
        loaded.facts.set( "scene-a", opticalScene() );
        const PreflightReport report =
            loaded.run( { { "training", "scene-a" }, { "eval", "scene-a" } } );
        const auto leak = findings( report, "SPF_TRAIN_EVAL_LEAKAGE" );
        REQUIRE( leak.size() == 1 );
        REQUIRE( leak[0]->severity == PreflightSeverity::Block );
    }
    // Distinct references but eval derived from training -> block.
    {
        Loaded loaded;
        SlotFacts train = opticalScene();
        train.assetId = "asset-origin";
        loaded.facts.set( "scene-a", train );
        SlotFacts eval = opticalScene();
        eval.assetId = "asset-derived";
        eval.derivedFromAssetIds = { "asset-origin" };
        loaded.facts.set( "scene-b", eval );
        const PreflightReport report =
            loaded.run( { { "training", "scene-a" }, { "eval", "scene-b" } } );
        REQUIRE( hasCode( report, "SPF_TRAIN_EVAL_LEAKAGE" ) );
    }
    // Reverse derivation (training manufactured from the eval source) is the
    // same leak.
    {
        Loaded loaded;
        SlotFacts train = opticalScene();
        train.assetId = "asset-derived";
        train.derivedFromAssetIds = { "asset-eval-origin" };
        loaded.facts.set( "scene-a", train );
        SlotFacts eval = opticalScene();
        eval.assetId = "asset-eval-origin";
        loaded.facts.set( "scene-b", eval );
        const PreflightReport report =
            loaded.run( { { "training", "scene-a" }, { "eval", "scene-b" } } );
        REQUIRE( hasCode( report, "SPF_TRAIN_EVAL_LEAKAGE" ) );
        REQUIRE( findings( report, "SPF_TRAIN_EVAL_LEAKAGE" )[0]->basis == "derived" );
    }
    // Unresolvable identity -> typed unknown, no silent pass.
    {
        Loaded loaded;
        loaded.facts.setUnknown( "scene-a", "no passport" );
        loaded.facts.set( "scene-b", opticalScene() );
        const PreflightReport report =
            loaded.run( { { "training", "scene-a" }, { "eval", "scene-b" } } );
        REQUIRE( hasCode( report, "SPF_LEAKAGE_UNKNOWN" ) );
        REQUIRE( trace( report, "preflight.train_eval_leakage" )->outcome ==
                 "insufficient_facts" );
    }
    // No train/eval pair at all -> pass.
    {
        Loaded loaded;
        loaded.facts.set( "scene-a", opticalScene() );
        REQUIRE_FALSE( hasCode( loaded.run(), "SPF_TRAIN_EVAL_LEAKAGE" ) );
    }
}

TEST_CASE( "model_compatibility: family mismatch blocks; missing manifest typed unknown",
           "[preflight][rules]" )
{
    Json::Value entry = ndviCapability();
    Json::Value model( Json::objectValue );
    Json::Value families( Json::arrayValue );
    families.append( "segmentation" );
    model["families"] = families;
    Json::Value roles( Json::objectValue );
    roles["red"] = 1;
    roles["nir"] = 1;
    model["input_band_roles"] = roles;
    entry["model_compatibility"] = model;

    // Compatible model + roles satisfied -> pass.
    {
        Loaded loaded( entry );
        loaded.facts.set( "scene-a", opticalScene() );
        SlotFacts manifest = opticalScene();
        manifest.assetRef = "model-x";
        manifest.slot = "model";
        manifest.kind = "model";
        manifest.hasModelManifest = true;
        manifest.modelKind = "segmentation";
        loaded.facts.set( "model-x", manifest );
        const PreflightReport report =
            loaded.run( { { "primary", "scene-a" }, { "model", "model-x" } } );
        REQUIRE( report.verdict == "ok" );
    }
    // Wrong family -> block.
    {
        Loaded loaded( entry );
        loaded.facts.set( "scene-a", opticalScene() );
        SlotFacts manifest = opticalScene();
        manifest.assetRef = "model-y";
        manifest.slot = "model";
        manifest.kind = "model";
        manifest.hasModelManifest = true;
        manifest.modelKind = "detector";
        loaded.facts.set( "model-y", manifest );
        const PreflightReport report =
            loaded.run( { { "primary", "scene-a" }, { "model", "model-y" } } );
        REQUIRE( findings( report, "SPF_MODEL_INCOMPATIBLE" )[0]->severity ==
                 PreflightSeverity::Block );
    }
    // Missing input band roles -> block.
    {
        Loaded loaded( entry );
        SlotFacts singleBand = opticalScene();
        singleBand.bands.erase( singleBand.bands.begin() );
        loaded.facts.set( "scene-a", singleBand );
        SlotFacts manifest = opticalScene();
        manifest.assetRef = "model-x";
        manifest.slot = "model";
        manifest.kind = "model";
        manifest.hasModelManifest = true;
        manifest.modelKind = "segmentation";
        loaded.facts.set( "model-x", manifest );
        const PreflightReport report =
            loaded.run( { { "primary", "scene-a" }, { "model", "model-x" } } );
        REQUIRE( hasCode( report, "SPF_MODEL_INCOMPATIBLE" ) );
    }
    // No manifest anywhere -> typed unknown.
    {
        Loaded loaded( entry );
        loaded.facts.set( "scene-a", opticalScene() );
        const PreflightReport report = loaded.run();
        REQUIRE( findings( report, "SPF_MODEL_UNKNOWN" )[0]->severity ==
                 PreflightSeverity::RequireAck );
        REQUIRE( trace( report, "preflight.model_compatibility" )->outcome ==
                 "insufficient_facts" );
    }
}

TEST_CASE( "virtual_raster inputs are judged like rasters, not silently skipped",
           "[preflight][rules]" )
{
    // VRT-style mosaics are ubiquitous; a missing role on a virtual raster
    // must block, not sail through as "no raster inputs".
    Loaded loaded;
    SlotFacts vrt = opticalScene();
    vrt.kind = "virtual_raster";
    vrt.bands.erase( vrt.bands.begin() ); // drop red
    loaded.facts.set( "scene-a", vrt );

    const PreflightReport report = loaded.run();
    REQUIRE( hasCode( report, "SPF_BAND_ROLE_MISSING" ) );
    REQUIRE( findings( report, "SPF_BAND_ROLE_MISSING" )[0]->severity == PreflightSeverity::Block );
}

TEST_CASE( "operator_known: undeclared operator and unavailable mirror are typed, not passes",
           "[preflight][rules]" )
{
    // Operator not declared in the mirror.
    {
        Loaded loaded{ Json::Value() }; // no capability entry at all
        loaded.facts.set( "scene-a", opticalScene() );
        const PreflightReport report = loaded.run();
        const auto unknown = findings( report, "SPF_OPERATOR_UNKNOWN" );
        REQUIRE( unknown.size() == 1 );
        REQUIRE( unknown[0]->severity == PreflightSeverity::RequireAck );
        REQUIRE( unknown[0]->basis == "unknown" );
        REQUIRE( report.verdict == "requires_ack" );
    }
    // Mirror is configured but could not be consulted -> fail-closed.
    {
        Loaded loaded;
        loaded.facts.set( "scene-a", opticalScene() );
        loaded.capability.markUnavailable( "mirror directory missing" );
        const PreflightReport report = loaded.run();
        const auto unavailable = findings( report, "SPF_CAPABILITY_MIRROR_UNAVAILABLE" );
        REQUIRE( unavailable.size() == 1 );
        REQUIRE( unavailable[0]->severity == PreflightSeverity::RequireAck );
        REQUIRE( report.verdict == "requires_ack" );
    }
}
