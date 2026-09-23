// test_preflight_provider.cpp — RS14-02 slice B: the provider seam.
//
// Facts enter the engine ONLY through IAssetFactsProvider / ICapabilityProvider.
// This file proves the seam end to end:
//   * the in-lib Memory providers (the test fake),
//   * StateAssetFactsProvider projecting a REAL RemoteSensingAssetState built
//     by the scientific_state canonical reader (single truth source),
//   * CapabilityMirrorProjection consuming the REAL data/agent/capabilities
//     documents with the documented merge semantics,
//   * a full engine integration: real passport + real mirror -> report.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/capability_mirror.h"
#include "preflight/asset_state_adapter.h"
#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rules.h"

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_types.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::preflight;

namespace {

std::string filePath( const char *relative )
{
    return std::string( SICNU_TEST_SOURCE_DIR ) + "/" + relative;
}

/// A canonical RemoteSensingAssetState wire document (schema
/// "sicnu.asset_state.v1"), parsed by the authoritative scientific_state
/// reader — the same reader production resolvers feed.
sicnu::state::RemoteSensingAssetState passportFromJson( const std::string &json,
                                                        bool &ok )
{
    sicnu::state::RemoteSensingAssetState state;
    sicnu::state::AssetStateError error;
    ok = sicnu::state::assetStateFromJson( json, state, error );
    return state;
}

std::string twoBandPassportJson()
{
    return R"JSON({
      "schema": "sicnu.asset_state.v1",
      "identity": { "asset_id": "asset-demo", "revision": "r1",
                     "source_path": "/data/scene-a.tif", "display_name": "Scene A",
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
      "geometry": { "has_crs": true, "crs_wkt": "", "crs_authid": "EPSG:32650",
                     "crs_geographic": false, "crs_projected": true,
                     "pixel_size_x": 10.0, "pixel_size_y": 10.0,
                     "width": 10980, "height": 10980 },
      "validity": { "no_data_policy": "declared", "cloud_cover_percent": 12.5,
                     "quality_mask_info": "s2_scl" },
      "radiometric": { "unit": "surface_reflectance", "declared_raw": "", "domain": "" },
      "provenance": { "is_derived": false }
    })JSON";
}

struct CapturedProvider final : IAssetFactsProvider
{
    SlotFactsResult slotFacts( const std::string &ref ) const override
    {
        lastRef = ref;
        SlotFactsResult r;
        r.status = FactStatus::Available;
        SlotFacts f;
        f.slot = "primary";
        f.assetRef = ref;
        r.facts = f;
        return r;
    }
    mutable std::string lastRef;
};

} // namespace

TEST_CASE( "Memory providers are the bounded in-lib fake", "[preflight][provider]" )
{
    MemoryFactsProvider facts;
    SlotFacts f;
    f.assetRef = "a";
    f.kind = "raster";
    facts.set( "a", f );
    facts.setUnknown( "b", "no passport" );

    auto a = facts.slotFacts( "a" );
    REQUIRE( a.status == FactStatus::Available );
    REQUIRE( a.facts.kind == "raster" );
    auto b = facts.slotFacts( "b" );
    REQUIRE( b.status == FactStatus::Unknown );
    REQUIRE( b.detail == "no passport" );
    auto c = facts.slotFacts( "never-asked" );
    REQUIRE( c.status == FactStatus::Unknown ); // unregistered refs are unknown, not fabricated

    MemoryCapabilityProvider cap;
    cap.setEntry( "rs:x", Json::Value( Json::objectValue ) );
    REQUIRE( cap.entryForOperator( "rs:x", {} ).status == FactStatus::Available );
    REQUIRE( cap.entryForOperator( "rs:missing", {} ).status == FactStatus::Unknown );
    cap.markUnavailable( "mirror down" );
    REQUIRE( cap.entryForOperator( "rs:x", {} ).status == FactStatus::Unavailable );
}

TEST_CASE( "the provider seam is implementable outside the library", "[preflight][provider]" )
{
    CapturedProvider provider;
    const auto result = provider.slotFacts( "asset-42" );
    REQUIRE( provider.lastRef == "asset-42" );
    REQUIRE( result.status == FactStatus::Available );
}

TEST_CASE( "StateAssetFactsProvider projects the real passport reader output",
           "[preflight][provider]" )
{
    bool ok = false;
    const auto state = passportFromJson( twoBandPassportJson(), ok );
    REQUIRE( ok );

    SlotFacts facts = projectAssetState( state );
    REQUIRE( facts.assetId == "asset-demo" );
    REQUIRE( facts.kind == "raster" );
    REQUIRE( facts.modality == "optical" );
    REQUIRE( facts.hasCrs == true );
    REQUIRE( facts.crsAuthid == "EPSG:32650" );
    REQUIRE( facts.crsProjected == true );
    REQUIRE( facts.hasPixelSize == true );
    REQUIRE( facts.pixelSizeX == 10.0 );
    REQUIRE( facts.radiometricUnit == "surface_reflectance" );
    REQUIRE( facts.hasCloudCover == true );
    REQUIRE( facts.cloudCoverPercent == 12.5 );
    REQUIRE( facts.qualityMaskInfo == "s2_scl" );
    REQUIRE( facts.hasAcquisitionTime == true );
    REQUIRE( facts.acquisitionTimeIso == "2026-05-01T10:30:00Z" );
    REQUIRE( facts.bands.size() == 2 );
    REQUIRE( facts.bands[0].role == "red" );
    REQUIRE( facts.bands[0].hasWavelengthNm == true );
    REQUIRE( facts.bands[0].wavelengthNm == 665.0 );
    REQUIRE( facts.bands[1].role == "nir" );

    // Through the provider seam:
    StateAssetFactsProvider provider(
        [&]( const std::string & ) { return std::optional{ state }; } );
    auto resolved = provider.slotFacts( "whatever" );
    REQUIRE( resolved.status == FactStatus::Available );
    REQUIRE( resolved.facts.crsAuthid == "EPSG:32650" );

    // Missing passport -> typed unknown (never fabricated).
    StateAssetFactsProvider hole(
        []( const std::string & ) { return std::optional<sicnu::state::RemoteSensingAssetState>{}; } );
    REQUIRE( hole.slotFacts( "ghost" ).status == FactStatus::Unknown );
}

TEST_CASE( "CapabilityMirrorProjection consumes the real mirror documents",
           "[preflight][provider]" )
{
    CapabilityMirrorProjection mirror;
    const int loaded = mirror.loadDirectory( filePath( "data/agent/capabilities" ) );
    REQUIRE( loaded >= 15 );
    REQUIRE( mirror.healthy() );
    const auto ids = mirror.entryIds();
    REQUIRE( ids.size() >= 15 );
    REQUIRE( std::is_sorted( ids.begin(), ids.end() ) );

    // Family defaults are merge sources, not operators (authority parity).
    REQUIRE( mirror.entryForOperator( "family:spectral_index", {} ).status ==
             FactStatus::Unknown );

    // The operator entry merges its extends chain (child-wins; radiometric
    // inherited from the terminal family default) and resolves the first
    // matching variant against the given params.
    const auto ndvi = mirror.entryForOperator( "rs:spectral_index", makeVariantParams( "index", "NDVI" ) );
    REQUIRE( ndvi.status == FactStatus::Available );
    REQUIRE( ndvi.entry["band_roles"]["red"].asInt() == 1 );
    REQUIRE( ndvi.entry["band_roles"]["nir"].asInt() == 1 );
    REQUIRE( ndvi.entry["radiometric"]["acceptable"].isArray() ); // inherited from the family
    REQUIRE( ndvi.entry["kind"].asString() != "family_default" ); // id/kind never inherited

    // Variant matching is case-insensitive (authority parity): a lowercase
    // selector must reach the same NDVI requirements, or a run could be
    // cleared with the requirements silently unconsulted.
    const auto lower = mirror.entryForOperator( "rs:spectral_index", makeVariantParams( "index", "ndvi" ) );
    REQUIRE( lower.status == FactStatus::Available );
    REQUIRE( lower.entry["band_roles"]["red"].asInt() == 1 );
    REQUIRE( lower.entry["band_roles"]["nir"].asInt() == 1 );

    // Variant mismatch falls back to the un-varianted merge.
    const auto noVariant = mirror.entryForOperator( "rs:spectral_index", {} );
    REQUIRE( noVariant.status == FactStatus::Available );
    REQUIRE( ( noVariant.entry["band_roles"].isNull() || noVariant.entry["band_roles"].empty() ) );

    // Undeclared operator -> typed unknown.
    REQUIRE( mirror.entryForOperator( "rs:not_in_mirror", {} ).status == FactStatus::Unknown );
}

TEST_CASE( "CapabilityMirrorProjection fails closed on corrupt input and missing dirs",
           "[preflight][provider]" )
{
    // Corrupt JSON document: counted as a problem, healthy() goes false.
    {
        CapabilityMirrorProjection mirror;
        Json::CharReaderBuilder builder;
        Json::Value parsed;
        std::string errors;
        std::stringstream stream( "{ not json" );
        Json::parseFromStream( builder, stream, &parsed, &errors );
        mirror.addDocument( parsed, "corrupt.json" );
        REQUIRE_FALSE( mirror.healthy() );
        REQUIRE( mirror.problems().size() == 1 );
        REQUIRE( mirror.problems()[0].find( "corrupt.json" ) != std::string::npos );
    }
    // Non-array top-level document: fail-closed, counted.
    {
        CapabilityMirrorProjection mirror;
        Json::Value obj( Json::objectValue );
        mirror.addDocument( obj, "object.json" );
        REQUIRE_FALSE( mirror.healthy() );
    }
    // Missing directory: queries report Unavailable (fail-closed gate), not Unknown.
    {
        CapabilityMirrorProjection mirror;
        mirror.loadDirectory( filePath( "data/agent/capabilities_does_not_exist" ) );
        REQUIRE_FALSE( mirror.healthy() );
        REQUIRE( mirror.entryForOperator( "rs:spectral_index", {} ).status ==
                 FactStatus::Unavailable );
    }
    // Unconfigured projection: explicit "not configured" unknown.
    {
        CapabilityMirrorProjection mirror;
        REQUIRE_FALSE( mirror.configured() );
        REQUIRE( mirror.entryForOperator( "rs:spectral_index", {} ).status ==
                 FactStatus::Unknown );
    }
}

TEST_CASE( "integration: real passport + real mirror drive the full engine",
           "[preflight][provider][integration]" )
{
    bool ok = false;
    const auto state = passportFromJson( twoBandPassportJson(), ok );
    REQUIRE( ok );

    StateAssetFactsProvider facts(
        [&]( const std::string &ref ) {
            if ( ref == "scene-a" )
                return std::optional{ state };
            return std::optional<sicnu::state::RemoteSensingAssetState>{};
        } );
    CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( filePath( "data/agent/capabilities" ) ) > 0 );

    PreflightEngine engine;
    for ( auto &rule : sicnu::preflight::builtinRules() )
        REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );

    PreflightRequest req;
    req.operatorId = "rs:spectral_index";
    req.mode = "teaching";
    req.humanOperatorId = "integration";
    req.inputs = { { "primary", "scene-a" } };

    // NDVI variant requires red+nir; the passport has both, reflectance unit,
    // projected CRS -> clean run.
    req.operatorParams = makeVariantParams( "index", "NDVI" );
    const PreflightReport clean = engine.evaluate( req, facts, mirror );
    REQUIRE( clean.verdict == "ok" );

    // The same operator without a variant declares no band roles; a missing
    // passport for the primary slot must surface as typed unknowns and flip
    // the verdict to requires_ack — a placeholder success is forbidden.
    PreflightRequest ghost = req;
    ghost.operatorParams = Json::Value();
    ghost.inputs = { { "primary", "no-such-asset" } };
    const PreflightReport unknown = engine.evaluate( ghost, facts, mirror );
    REQUIRE( unknown.verdict == "requires_ack" );
    bool sawUnknown = false;
    for ( const auto &f : unknown.findings )
        if ( f.basis == "unknown" )
            sawUnknown = true;
    REQUIRE( sawUnknown );
}
