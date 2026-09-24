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
    // Malformed variant payloads fail closed too: band_roles with a bool
    // count (or a 0 count) would silently narrow the check downstream.
    {
        CapabilityMirrorProjection mirror;
        Json::Value doc( Json::arrayValue );
        Json::Value entry( Json::objectValue );
        entry["id"] = "rs:bad_variant";
        Json::Value variants( Json::arrayValue );
        Json::Value variant( Json::objectValue );
        Json::Value when( Json::objectValue );
        when["param"] = "index";
        Json::Value values( Json::arrayValue );
        values.append( "NDVI" );
        when["values"] = values;
        variant["when"] = when;
        Json::Value roles( Json::objectValue );
        roles["red"] = true; // bool, not an int count
        variant["band_roles"] = roles;
        variants.append( variant );
        entry["variants"] = variants;
        doc.append( entry );
        mirror.addDocument( doc, "bad_variant.json" );
        REQUIRE_FALSE( mirror.healthy() );
        REQUIRE( mirror.problems().size() == 1 );
        REQUIRE( mirror.problems()[0].find( "band_roles" ) != std::string::npos );
        REQUIRE( mirror.entryForOperator( "rs:bad_variant", makeVariantParams( "index", "NDVI" ) )
                     .status == FactStatus::Unavailable );
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

TEST_CASE( "variant-parameterized policies that match nothing are flagged, not silently dropped",
           "[preflight][provider]" )
{
    // The real mirror keeps rs:spectral_index's band_roles ONLY inside its
    // variants. A request whose params match no variant (missing, typo'd,
    // foreign-cased) must not hand rules a bare entry that reads as "no
    // policy declared": the policies exist but none is consultable.
    CapabilityMirrorProjection mirror;
    Json::Value doc( Json::arrayValue );
    Json::Value entry( Json::objectValue );
    entry["id"] = "rs:variant_only";
    Json::Value variants( Json::arrayValue );
    Json::Value variant( Json::objectValue );
    Json::Value when( Json::objectValue );
    when["param"] = "index";
    Json::Value values( Json::arrayValue );
    values.append( "NDVI" );
    when["values"] = values;
    variant["when"] = when;
    Json::Value roles( Json::objectValue );
    roles["red"] = 1;
    roles["nir"] = 1;
    variant["band_roles"] = roles;
    variants.append( variant );
    entry["variants"] = variants;
    doc.append( entry );
    mirror.addDocument( doc, "variant_only.json" );

    // Matching params apply the policy: nothing is dropped.
    const auto matched =
        mirror.entryForOperator( "rs:variant_only", makeVariantParams( "index", "NDVI" ) );
    REQUIRE( matched.status == FactStatus::Available );
    REQUIRE_FALSE( matched.variantPoliciesDropped );
    REQUIRE( matched.entry["band_roles"]["red"].asInt() == 1 );

    // No params at all: declared but not consultable.
    const auto unmatched = mirror.entryForOperator( "rs:variant_only", {} );
    REQUIRE( unmatched.status == FactStatus::Available );
    REQUIRE( unmatched.variantPoliciesDropped );

    // Foreign params: same flag, still Available.
    const auto foreign =
        mirror.entryForOperator( "rs:variant_only", makeVariantParams( "index", "NDWI" ) );
    REQUIRE( foreign.status == FactStatus::Available );
    REQUIRE( foreign.variantPoliciesDropped );

    // An entry without variants never sets the flag.
    Json::Value plainDoc( Json::arrayValue );
    Json::Value plain( Json::objectValue );
    plain["id"] = "rs:plain";
    plainDoc.append( plain );
    CapabilityMirrorProjection plainMirror;
    plainMirror.addDocument( plainDoc, "plain.json" );
    const auto plainEntry = plainMirror.entryForOperator( "rs:plain", {} );
    REQUIRE( plainEntry.status == FactStatus::Available );
    REQUIRE_FALSE( plainEntry.variantPoliciesDropped );
}

TEST_CASE( "passport lifecycle gates the projection and truncation stays honest",
           "[preflight][provider][integration]" )
{
    // Same science facts, but the passport says the asset is stale: the
    // metadata may no longer describe the usable data, so the adapter must
    // not hand rules "observed" values — every strategy check degrades to a
    // typed unknown and the verdict cannot be a clean ok.
    std::string json = twoBandPassportJson();
    const std::string ready = "\"lifecycle\": \"ready\"";
    const std::string stale = "\"lifecycle\": \"stale\"";
    const auto pos = json.find( ready );
    REQUIRE( pos != std::string::npos );
    json.replace( pos, ready.size(), stale );
    bool ok = false;
    const auto state = passportFromJson( json, ok );
    REQUIRE( ok );

    StateAssetFactsProvider provider(
        [&]( const std::string & ) { return std::optional{ state }; } );
    const auto resolved = provider.slotFacts( "scene-a" );
    REQUIRE( resolved.status == FactStatus::Unknown );
    REQUIRE( resolved.detail.find( "stale" ) != std::string::npos );

    StateAssetFactsProvider facts( [&]( const std::string &ref ) {
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
    req.operatorParams = makeVariantParams( "index", "NDVI" );
    const PreflightReport report = engine.evaluate( req, facts, mirror );
    REQUIRE( report.verdict == "requires_ack" );

    // The passport's temporal truncation flag projects honestly instead of
    // being rewritten to false.
    std::string truncatedJson = twoBandPassportJson();
    const std::string provenance = "\"provenance\": { \"is_derived\": false }";
    const auto provPos = truncatedJson.find( provenance );
    REQUIRE( provPos != std::string::npos );
    truncatedJson.replace( provPos, provenance.size(),
                            "\"provenance\": { \"is_derived\": false }, "
                            "\"temporal\": { \"present\": true, \"truncated\": true, "
                            "\"refs\": [ { \"collection_id\": \"S2\" } ] }" );
    bool truncatedOk = false;
    const auto truncatedState = passportFromJson( truncatedJson, truncatedOk );
    REQUIRE( truncatedOk );
    StateAssetFactsProvider truncatedProvider(
        [&]( const std::string & ) { return std::optional{ truncatedState }; } );
    const auto truncatedFacts = truncatedProvider.slotFacts( "scene-a" );
    REQUIRE( truncatedFacts.status == FactStatus::Available );
    REQUIRE( truncatedFacts.facts.temporalTruncated );
}

TEST_CASE( "mirror fails closed on extends chains deeper than the merge bound",
           "[preflight][provider]" )
{
    // Six chained ancestors carry the policy at the deepest end. The merge
    // walks at most kMaxMergeDepth=4 hops, so the policy could never be
    // consulted — the header promises this is never silently skipped.
    CapabilityMirrorProjection mirror;
    Json::Value doc( Json::arrayValue );
    const char *ids[] = { "deep:a", "deep:b", "deep:c", "deep:d", "deep:e", "deep:f" };
    for ( int i = 0; i < 6; ++i )
    {
        Json::Value entry( Json::objectValue );
        entry["id"] = ids[i];
        if ( i > 0 )
        {
            entry["extends"] = ids[i - 1];
        }
        else
        {
            Json::Value roles( Json::objectValue );
            roles["red"] = 1;
            entry["band_roles"] = roles;
        }
        doc.append( entry );
    }
    mirror.addDocument( doc, "deep.json" );
    REQUIRE_FALSE( mirror.healthy() );
    REQUIRE( mirror.problems().size() == 1 );
    REQUIRE( mirror.problems().front().find( "extends" ) != std::string::npos );
    // The flagged entry is the one whose merge gets cut: the query root.
    REQUIRE( mirror.problems().front().find( "deep:f" ) != std::string::npos );
    REQUIRE( mirror.entryForOperator( "deep:f", {} ).status == FactStatus::Unavailable );
}

TEST_CASE( "mirror keeps a forward-referencing or dangling extends chain loadable",
           "[preflight][provider]" )
{
    // A child loaded before its parent (document order is load order) and a
    // dangling parent reference are both the merge's own bounded stops at
    // query time — neither is a depth truncation, so neither may poison the
    // projection into a global Unavailable.
    {
        CapabilityMirrorProjection mirror;
        Json::Value child( Json::arrayValue );
        Json::Value childEntry( Json::objectValue );
        childEntry["id"] = "fwd:child";
        childEntry["extends"] = "fwd:parent";
        child.append( childEntry );
        mirror.addDocument( child, "a_child.json" );
        Json::Value parent( Json::arrayValue );
        Json::Value parentEntry( Json::objectValue );
        parentEntry["id"] = "fwd:parent";
        Json::Value roles( Json::objectValue );
        roles["red"] = 1;
        parentEntry["band_roles"] = roles;
        parent.append( parentEntry );
        mirror.addDocument( parent, "b_parent.json" );
        REQUIRE( mirror.healthy() );
        const auto merged = mirror.entryForOperator( "fwd:child", {} );
        REQUIRE( merged.status == FactStatus::Available );
        REQUIRE( merged.entry["band_roles"]["red"].asInt() == 1 );
    }
    // Dangling parent: bounded stop, projection stays healthy and the entry
    // itself still answers.
    {
        CapabilityMirrorProjection mirror;
        Json::Value doc( Json::arrayValue );
        Json::Value entry( Json::objectValue );
        entry["id"] = "dg:entry";
        entry["extends"] = "dg:nowhere";
        doc.append( entry );
        mirror.addDocument( doc, "dangling.json" );
        REQUIRE( mirror.healthy() );
        REQUIRE( mirror.entryForOperator( "dg:entry", {} ).status == FactStatus::Available );
    }
}

TEST_CASE( "mirror accepts a chain exactly at the merge bound", "[preflight][provider]" )
{
    // Five entries -> four hops: exactly kMaxMergeDepth. The full chain is
    // consultable, so the projection must stay healthy (boundary pin).
    CapabilityMirrorProjection mirror;
    Json::Value doc( Json::arrayValue );
    const char *ids[] = { "bound:a", "bound:b", "bound:c", "bound:d", "bound:e" };
    for ( int i = 0; i < 5; ++i )
    {
        Json::Value entry( Json::objectValue );
        entry["id"] = ids[i];
        if ( i > 0 )
            entry["extends"] = ids[i - 1];
        else
        {
            Json::Value roles( Json::objectValue );
            roles["red"] = 1;
            entry["band_roles"] = roles;
        }
        doc.append( entry );
    }
    mirror.addDocument( doc, "bound.json" );
    REQUIRE( mirror.healthy() );
    const auto merged = mirror.entryForOperator( "bound:e", {} );
    REQUIRE( merged.status == FactStatus::Available );
    REQUIRE( merged.entry["band_roles"]["red"].asInt() == 1 );
}

TEST_CASE( "integration: a variant-only capability gates the verdict instead of passing silently",
           "[preflight][provider][integration]" )
{
    bool ok = false;
    const auto state = passportFromJson( twoBandPassportJson(), ok );
    REQUIRE( ok );
    StateAssetFactsProvider facts( [&]( const std::string &ref ) {
        if ( ref == "scene-a" )
            return std::optional{ state };
        return std::optional<sicnu::state::RemoteSensingAssetState>{};
    } );
    CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( filePath( "data/agent/capabilities" ) ) > 0 );

    PreflightEngine engine;
    for ( auto &rule : sicnu::preflight::builtinRules() )
        REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );

    // rs:spectral_index keeps its band_roles only in variants; a request
    // without the index param must surface a typed unknown per policy
    // family, never a clean ok on an unconsultable strategy.
    PreflightRequest req;
    req.operatorId = "rs:spectral_index";
    req.mode = "teaching";
    req.humanOperatorId = "integration";
    req.inputs = { { "primary", "scene-a" } };
    req.operatorParams = Json::Value();
    const PreflightReport report = engine.evaluate( req, facts, mirror );

    std::size_t unknowns = 0;
    for ( const auto &f : report.findings )
        if ( f.code == "SPF_BAND_ROLE_UNKNOWN" )
            ++unknowns;
    REQUIRE( unknowns >= 1 );
    REQUIRE( report.verdict == "requires_ack" );
    // Trace vocabulary: a check that could not decide says
    // insufficient_facts, never "finding".
    bool sawGateTrace = false;
    for ( const auto &e : report.evaluated )
        if ( e.ruleId == "preflight.band_role" )
        {
            REQUIRE( e.outcome == "insufficient_facts" );
            sawGateTrace = true;
        }
    REQUIRE( sawGateTrace );
}

TEST_CASE( "a passport without a resolvable asset kind is a typed unknown, not an available slot",
           "[preflight][provider][integration]" )
{
    // The wire format legally omits identity.kind; the resolver then reports
    // AssetKind::Unknown. Projecting that as an Available slot would make
    // every raster rule silently skip it ("non-raster slot skipped") and the
    // engine returns ok on a passport nobody can classify.
    std::string json = twoBandPassportJson();
    const auto pos = json.find( "\"kind\": \"raster\", " );
    REQUIRE( pos != std::string::npos );
    json.erase( pos, std::string( "\"kind\": \"raster\", " ).size() );
    bool ok = false;
    const auto state = passportFromJson( json, ok );
    REQUIRE( ok );
    REQUIRE( state.kind == sicnu::state::AssetKind::Unknown );

    StateAssetFactsProvider provider(
        [&]( const std::string & ) { return std::optional{ state }; } );
    const auto resolved = provider.slotFacts( "scene-a" );
    REQUIRE( resolved.status == FactStatus::Unknown );
    REQUIRE_FALSE( resolved.detail.empty() );

    // Engine-level: the unclassifiable passport gates the verdict.
    StateAssetFactsProvider facts( [&]( const std::string &ref ) {
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
    req.operatorParams = makeVariantParams( "index", "NDVI" );
    const PreflightReport report = engine.evaluate( req, facts, mirror );
    bool sawUnknown = false;
    for ( const auto &f : report.findings )
        if ( f.basis == "unknown" )
            sawUnknown = true;
    REQUIRE( sawUnknown );
    REQUIRE( report.verdict == "requires_ack" );
}
