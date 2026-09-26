// test_preflight_runtime_adapter.cpp — Track 04: the thin runtime adapter.
//
// Oracles (no handwritten facts anywhere):
//   * preflightCheckJson drives ONE PreflightEngine evaluation from real
//     authorities: passports built by the canonical scientific_state reader
//     and capability entries injected as the runtime authority would hand
//     them over (merged entries; the adapter never re-merges).
//   * capability authority changes (variant/extends edits) flow into the
//     runtime verdict — the runtime path consumes the injected authority
//     verbatim instead of any mirror copy.
//   * the temporal provider resolves declared collection refs into counts /
//     dates; out-of-order, unparseable, missing and truncated series surface
//     as typed findings, never silent narrowing.
//   * grid facts come from the passport; mismatches block, unknowns degrade.
//   * the same input produces byte-identical results; acknowledgements can
//     never flip a block; the three projections agree on verdict/digest/
//     finding multiset.

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "preflight/asset_state_adapter.h"
#include "preflight/capability_mirror.h"
#include "preflight/engine.h"
#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/render.h"
#include "preflight/report.h"
#include "preflight/runtime_adapter.h"

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_types.h"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::preflight;

namespace {

/// In-test capability authority: same contract the harness
/// CapabilityKnowledge::entryForOperator exposes to the adapter — the MERGED
/// entry (base + first matching variant) or null. The merge lives here, on
/// the authority side of the seam, exactly as the real authority owns it;
/// tests mutate it to prove the runtime follows the authority.
class MapAuthority
{
  public:
    void set( const std::string &operatorId, Json::Value document )
    {
        documents_[operatorId] = std::move( document );
    }
    void remove( const std::string &operatorId ) { documents_.erase( operatorId ); }

    AuthorityCapabilityProvider provider() const
    {
        return AuthorityCapabilityProvider(
            [ this ]( const std::string &operatorId, const Json::Value &params ) {
                const auto it = documents_.find( operatorId );
                if ( it == documents_.end() )
                    return Json::Value();
                Json::Value merged( Json::objectValue );
                const Json::Value &document = it->second;
                for ( const auto &key : document.getMemberNames() )
                    if ( key != std::string( "variants" ) && key != std::string( "when" ) )
                        merged[key] = document[key];
                if ( params.isObject() )
                {
                    for ( const auto &variant : document["variants"] )
                    {
                        const Json::Value &when = variant["when"];
                        if ( !when.isObject() || !params.isMember( when["param"].asString() ) )
                            continue;
                        const std::string given = params[when["param"].asString()].asString();
                        bool matches = false;
                        for ( const auto &candidate : when["values"] )
                            if ( candidate.isString() && candidate.asString() == given )
                                matches = true;
                        if ( !matches )
                            continue;
                        for ( const auto &key : variant.getMemberNames() )
                            if ( key != std::string( "when" ) )
                                merged[key] = variant[key];
                        break;
                    }
                }
                return merged;
            } );
    }

  private:
    std::map<std::string, Json::Value> documents_;
};

/// Passport-backed facts provider over inline canonical documents — the same
/// sicnu.asset_state.v1 reader production resolvers feed.
class PassportFactsProvider : public IAssetFactsProvider
{
  public:
    void set( const std::string &ref, const std::string &passportJson )
    {
        sicnu::state::RemoteSensingAssetState state;
        sicnu::state::AssetStateError error;
        REQUIRE( sicnu::state::assetStateFromJson( passportJson, state, error ) );
        states_[ref] = std::move( state );
    }

    SlotFactsResult slotFacts( const std::string &assetRef ) const override
    {
        const StateAssetFactsProvider provider(
            [ this ]( const std::string &ref ) -> std::optional<sicnu::state::RemoteSensingAssetState> {
                const auto it = states_.find( ref );
                if ( it == states_.end() )
                    return std::nullopt;
                return it->second;
            } );
        return provider.slotFacts( assetRef );
    }

  private:
    std::map<std::string, sicnu::state::RemoteSensingAssetState> states_;
};

/// Collection store behind the temporal lookup: collection id → scene dates.
struct Collection
{
    std::vector<std::string> datesIso;  ///< Parseable times, collection order.
    int invalidTimeScenes = 0;          ///< Scenes whose time is missing/unparseable.
};

TemporalFactsLookup storeLookup( const std::map<std::string, Collection> &store )
{
    return [ &store ]( const std::string &collectionId ) -> TemporalCollectionFacts {
        const auto it = store.find( collectionId );
        if ( it == store.end() )
        {
            TemporalCollectionFacts miss;
            miss.status = FactStatus::Unknown;
            miss.detail = "collection not in the workspace store";
            return miss;
        }
        TemporalCollectionFacts facts;
        facts.status = FactStatus::Available;
        facts.sceneCount = static_cast<int>( it->second.datesIso.size() ) +
                           it->second.invalidTimeScenes;
        facts.datesIso = it->second.datesIso;
        facts.invalidTimeScenes = it->second.invalidTimeScenes;
        return facts;
    };
}

Json::Value opticalPassportJson( const std::string &assetId,
                                 const std::string &crs = "EPSG:32650" )
{
    Json::Value passport( Json::objectValue );
    passport["schema"] = "sicnu.asset_state.v1";
    Json::Value identity( Json::objectValue );
    identity["asset_id"] = assetId;
    identity["revision"] = "r1";
    identity["source_path"] = "/data/" + assetId + ".tif";
    identity["kind"] = "raster";
    identity["lifecycle"] = "ready";
    passport["identity"] = identity;
    Json::Value sensor( Json::objectValue );
    sensor["platform"] = "S2";
    sensor["instrument"] = "MSI";
    sensor["modality"] = "optical";
    passport["sensor"] = sensor;
    Json::Value acquisition( Json::objectValue );
    acquisition["time_iso"] = "2026-05-01T10:30:00Z";
    acquisition["valid"] = true;
    passport["acquisition"] = acquisition;
    Json::Value bands( Json::arrayValue );
    Json::Value red( Json::objectValue );
    red["index"] = 1;
    red["name"] = "B04";
    red["role"] = "red";
    red["data_type"] = "UInt16";
    red["wavelength_nm"] = 665.0;
    bands.append( red );
    Json::Value nir( Json::objectValue );
    nir["index"] = 2;
    nir["name"] = "B08";
    nir["role"] = "nir";
    nir["data_type"] = "UInt16";
    nir["wavelength_nm"] = 842.0;
    bands.append( nir );
    passport["bands"] = bands;
    Json::Value geometry( Json::objectValue );
    geometry["has_crs"] = true;
    geometry["crs_authid"] = crs;
    geometry["crs_projected"] = true;
    geometry["pixel_size_x"] = 10.0;
    geometry["pixel_size_y"] = 10.0;
    geometry["width"] = 10980;
    geometry["height"] = 10980;
    geometry["has_extent"] = true;
    geometry["min_x"] = 600000.0;
    geometry["min_y"] = 4900000.0;
    geometry["max_x"] = 709800.0;
    geometry["max_y"] = 5009800.0;
    passport["geometry"] = geometry;
    Json::Value validity( Json::objectValue );
    validity["no_data_policy"] = "declared";
    validity["cloud_cover_percent"] = 12.5;
    validity["quality_mask_info"] = "s2_scl";
    passport["validity"] = validity;
    Json::Value radiometric( Json::objectValue );
    radiometric["unit"] = "surface_reflectance";
    passport["radiometric"] = radiometric;
    Json::Value provenance( Json::objectValue );
    provenance["is_derived"] = false;
    passport["provenance"] = provenance;
    return passport;
}

/// Spectral-index capability entry with parameter-conditional variants — the
/// shape data/agent/capabilities ships (variants override band_roles).
Json::Value indexCapabilityJson()
{
    Json::Value entry( Json::objectValue );
    entry["id"] = "rs:index";
    Json::Value modality( Json::arrayValue );
    modality.append( "optical" );
    entry["modality"] = modality;
    Json::Value radiometric( Json::objectValue );
    Json::Value acceptable( Json::arrayValue );
    acceptable.append( "surface_reflectance" );
    entry["radiometric"]["acceptable"] = acceptable;
    Json::Value variants( Json::arrayValue );
    Json::Value ndvi( Json::objectValue );
    Json::Value whenNdvi( Json::objectValue );
    whenNdvi["param"] = "index";
    Json::Value ndviValues( Json::arrayValue );
    ndviValues.append( "NDVI" );
    whenNdvi["values"] = ndviValues;
    ndvi["when"] = whenNdvi;
    ndvi["band_roles"]["red"] = 1;
    ndvi["band_roles"]["nir"] = 1;
    variants.append( ndvi );
    Json::Value evi( Json::objectValue );
    Json::Value whenEvi( Json::objectValue );
    whenEvi["param"] = "index";
    Json::Value eviValues( Json::arrayValue );
    eviValues.append( "EVI" );
    whenEvi["values"] = eviValues;
    evi["when"] = whenEvi;
    evi["band_roles"]["red"] = 1;
    evi["band_roles"]["nir"] = 1;
    evi["band_roles"]["blue"] = 1;
    variants.append( evi );
    entry["variants"] = variants;
    return entry;
}

Json::Value checkArgs( const std::string &operatorId, Json::Value params,
                       const std::vector<std::pair<std::string, std::string>> &inputs,
                       const std::vector<std::string> &acks = {},
                       const std::string &humanOperatorId = "op" )
{
    Json::Value args( Json::objectValue );
    args["operator"] = operatorId;
    args["human_operator_id"] = humanOperatorId;
    if ( !params.isNull() )
        args["operator_params"] = std::move( params );
    Json::Value in( Json::arrayValue );
    for ( const auto &slot : inputs )
    {
        Json::Value item( Json::objectValue );
        item["slot"] = slot.first;
        item["ref"] = slot.second;
        in.append( item );
    }
    args["inputs"] = in;
    if ( !acks.empty() )
    {
        Json::Value ackArray( Json::arrayValue );
        for ( const auto &code : acks )
            ackArray.append( code );
        args["acknowledgements"] = ackArray;
    }
    return args;
}

std::vector<PreflightFinding> findingsOf( const Json::Value &check, const std::string &code )
{
    const Json::Value &findings = check["report"]["findings"];
    std::vector<PreflightFinding> out;
    for ( const auto &f : findings )
    {
        auto parsed = PreflightFinding::fromJson( f );
        REQUIRE( parsed.has_value() );
        if ( parsed->code == code )
            out.push_back( std::move( *parsed ) );
    }
    return out;
}

} // namespace

TEST_CASE( "preflight:check drives a real passport and authority into one report",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "scene-a", opticalPassportJson( "asset-demo" ).toStyledString() );
    MapAuthority authority;
    authority.set( "rs:index", indexCapabilityJson() );

    const Json::Value check = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } },
                   {}, "op-1" ),
        facts, authority.provider() );

    REQUIRE( check["kind"].asString() == "sicnu.preflight.check/1" );
    REQUIRE( check["report"]["verdict"].asString() == "ok" );
    REQUIRE( check["report"]["operator_id"].asString() == "op-1" );
    // The engine consumed the AUTHORITY's variant: NDVI needs red+nir, both
    // declared on the passport — nothing was handwritten into a rule.
    REQUIRE( findingsOf( check, "SPF_BAND_ROLE_MISSING" ).empty() );
    // Extent facts travel from the passport (declared, unused by rules — the
    // grid rules judge only provable mismatches).
    REQUIRE( check["report"]["findings"].isArray() );

    // Three projections of the ONE report agree.
    const Json::Value &report = check["report"];
    REQUIRE( check["teaching"]["verdict"].asString() == report["verdict"].asString() );
    REQUIRE( check["agent"]["verdict"].asString() == report["verdict"].asString() );
    REQUIRE( check["teaching"]["request_digest"].asString() ==
             report["request_digest"].asString() );
    REQUIRE( check["agent"]["request_digest"].asString() ==
             report["request_digest"].asString() );
    REQUIRE( check["teaching"]["items"].size() == report["findings"].size() );
    REQUIRE( check["agent"]["judgments"].size() == report["findings"].size() );
    REQUIRE( check["agent"]["can_proceed"].asBool() == ( report["verdict"].asString() == "ok" ) );
}

TEST_CASE( "capability authority variant and extends edits flow into the runtime verdict",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "scene-a", opticalPassportJson( "asset-demo" ).toStyledString() );
    MapAuthority authority;

    // The passport declares red+nir only. NDVI passes, EVI blocks on blue.
    authority.set( "rs:index", indexCapabilityJson() );
    const Json::Value ndviRun = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } } ),
        facts, authority.provider() );
    REQUIRE( ndviRun["report"]["verdict"].asString() == "ok" );
    const Json::Value eviRun = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "EVI" ), { { "primary", "scene-a" } } ),
        facts, authority.provider() );
    REQUIRE( eviRun["report"]["verdict"].asString() == "blocked" );
    REQUIRE_FALSE( findingsOf( eviRun, "SPF_BAND_ROLE_MISSING" ).empty() );

    // An authority-side variant edit (blue removed from EVI) un-blocks the
    // runtime WITHOUT any adapter change — the merged entry is consumed
    // verbatim, never mirrored.
    Json::Value edited = indexCapabilityJson();
    edited["variants"][1]["band_roles"].removeMember( "blue" );
    authority.set( "rs:index", std::move( edited ) );
    const Json::Value eviEdited = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "EVI" ), { { "primary", "scene-a" } } ),
        facts, authority.provider() );
    REQUIRE( eviEdited["report"]["verdict"].asString() == "ok" );

    // An undeclared operator is the typed Unknown path: require_ack with the
    // stable code, never a crash and never a fabricated pass.
    MapAuthority empty;
    const Json::Value unknownRun = preflightCheckJson(
        checkArgs( "rs:missing", Json::Value(), { { "primary", "scene-a" } } ), facts,
        empty.provider() );
    REQUIRE( unknownRun["report"]["verdict"].asString() == "requires_ack" );
    REQUIRE_FALSE( findingsOf( unknownRun, "SPF_OPERATOR_UNKNOWN" ).empty() );
}

TEST_CASE( "temporal provider: order, invalid times, missing collections and "
           "truncation are typed, never silent",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    Json::Value passport = opticalPassportJson( "asset-series" );
    passport["temporal"] = Json::Value( Json::objectValue );
    passport["temporal"]["present"] = true;
    Json::Value refs( Json::arrayValue );
    Json::Value ref( Json::objectValue );
    ref["collection_id"] = "col-1";
    ref["role"] = "series";
    refs.append( ref );
    passport["temporal"]["refs"] = refs;
    facts.set( "scene-a", passport.toStyledString() );

    MapAuthority authority;
    Json::Value entry = indexCapabilityJson();
    Json::Value temporal( Json::objectValue );
    temporal["min_scenes"] = 3;
    temporal["requires_acquisition_time"] = true;
    temporal["max_gap_days"] = 40;
    entry["temporal"] = temporal;
    authority.set( "rs:index", entry );

    const Json::Value args =
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } } );

    // Ordered series within the gap budget: clean.
    {
        std::map<std::string, Collection> store;
        store["col-1"] = { { "2026-01-01", "2026-01-15", "2026-02-01" }, 0 };
        const TemporalFactsLookup lookup = storeLookup( store );
        const Json::Value check = preflightCheckJson( args, facts, authority.provider(), &lookup );
        REQUIRE( check["report"]["verdict"].asString() == "ok" );
    }

    // Out-of-order collection order blocks: the provider never sorts — the
    // rule sees the series as declared.
    {
        std::map<std::string, Collection> store;
        store["col-1"] = { { "2026-01-15", "2026-01-01", "2026-02-01" }, 0 };
        const TemporalFactsLookup lookup = storeLookup( store );
        const Json::Value check = preflightCheckJson( args, facts, authority.provider(), &lookup );
        REQUIRE( check["report"]["verdict"].asString() == "blocked" );
        REQUIRE_FALSE( findingsOf( check, "SPF_TEMPORAL_ORDER_INVALID" ).empty() );
    }

    // One scene without a parseable time: counted, order/gap judgment stays
    // partial (SPF_TEMPORAL_TIME_INCOMPLETE), never silently narrowed.
    {
        std::map<std::string, Collection> store;
        store["col-1"] = { { "2026-01-01", "2026-01-15", "2026-02-01" }, 1 };
        const TemporalFactsLookup lookup = storeLookup( store );
        const Json::Value check = preflightCheckJson( args, facts, authority.provider(), &lookup );
        const auto hit = findingsOf( check, "SPF_TEMPORAL_TIME_INCOMPLETE" );
        REQUIRE( hit.size() == 1 );
        REQUIRE( hit[0].evidence["invalid_time_scenes"].asInt() == 1 );
        REQUIRE( hit[0].evidence["declared_scenes"].asInt() == 4 );
    }

    // The declared collection is not in the store: typed temporal unknown.
    {
        std::map<std::string, Collection> empty;
        const TemporalFactsLookup lookup = storeLookup( empty );
        const Json::Value check = preflightCheckJson( args, facts, authority.provider(), &lookup );
        REQUIRE( check["report"]["verdict"].asString() == "requires_ack" );
        REQUIRE_FALSE( findingsOf( check, "SPF_TEMPORAL_UNKNOWN" ).empty() );
    }

    // Oversized declared series: the provider caps and SAYS so — and a
    // simultaneous invalid-time count is NOT swallowed by the truncation
    // finding (partialities never hide each other).
    {
        std::map<std::string, Collection> store;
        Collection big;
        for ( int i = 0; i < 600; ++i )
        {
            char buffer[16];
            std::snprintf( buffer, sizeof( buffer ), "2025-%02d-%02d", 1 + i / 28 % 12,
                           1 + i % 28 );
            big.datesIso.emplace_back( buffer );
        }
        big.invalidTimeScenes = 2;
        store["col-1"] = std::move( big );
        const TemporalFactsLookup lookup = storeLookup( store );
        const Json::Value check = preflightCheckJson( args, facts, authority.provider(), &lookup );
        const auto truncated = findingsOf( check, "SPF_TEMPORAL_DATES_TRUNCATED" );
        REQUIRE( truncated.size() == 1 );
        REQUIRE( truncated[0].evidence["invalid_time_scenes"].asInt() == 2 );
        REQUIRE( findingsOf( check, "SPF_TEMPORAL_TIME_INCOMPLETE" ).empty() );
    }
}

TEST_CASE( "grid facts from the passport: provable mismatch blocks, missing facts "
           "degrade typed", "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "before", opticalPassportJson( "asset-b", "EPSG:32650" ).toStyledString() );
    facts.set( "after", opticalPassportJson( "asset-a", "EPSG:4326" ).toStyledString() );
    MapAuthority authority;
    authority.set( "rs:index", indexCapabilityJson() );

    const Json::Value mismatch = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ),
                   { { "before", "before" }, { "after", "after" } } ),
        facts, authority.provider() );
    REQUIRE( mismatch["report"]["verdict"].asString() == "blocked" );
    const auto hit = findingsOf( mismatch, "SPF_CRS_MISMATCH" );
    REQUIRE( hit.size() == 1 );
    REQUIRE( hit[0].basis == "observed" );
    REQUIRE( hit[0].evidence["crs_a"].asString() == "EPSG:32650" );
    REQUIRE( hit[0].evidence["crs_b"].asString() == "EPSG:4326" );

    // A pair where one side declares no CRS: typed unknown, not a fabricated
    // mismatch and not a pass.
    Json::Value noCrs = opticalPassportJson( "asset-c" );
    noCrs["geometry"]["has_crs"] = false;
    noCrs["geometry"].removeMember( "crs_authid" );
    facts.set( "after", noCrs.toStyledString() );
    const Json::Value unknownCrs = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ),
                   { { "before", "before" }, { "after", "after" } } ),
        facts, authority.provider() );
    REQUIRE_FALSE( findingsOf( unknownCrs, "SPF_CRS_UNKNOWN" ).empty() );
}

TEST_CASE( "repeat checks are byte-stable; acknowledgements never flip a block",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "scene-a", opticalPassportJson( "asset-demo" ).toStyledString() );
    MapAuthority authority;
    authority.set( "rs:index", indexCapabilityJson() );

    const Json::Value args =
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } } );
    const Json::Value first = preflightCheckJson( args, facts, authority.provider() );
    const Json::Value second = preflightCheckJson( args, facts, authority.provider() );
    REQUIRE( canonicalProjectionJson( first ) == canonicalProjectionJson( second ) );

    // A real block: EVI on a red+nir passport (no blue band). Acknowledging
    // the block code cannot flip the verdict, whatever the request claims.
    const Json::Value blocked = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "EVI" ),
                   { { "primary", "scene-a" } }, { "SPF_BAND_ROLE_MISSING" } ),
        facts, authority.provider() );
    REQUIRE( blocked["report"]["verdict"].asString() == "blocked" );
    for ( const auto &f : blocked["report"]["findings"] )
        if ( f["code"].asString() == "SPF_BAND_ROLE_MISSING" )
            REQUIRE( f["acknowledged"].asBool() == false );

    // The ack path is real for require_ack: acknowledging every unacknowledged
    // require_ack code of a ghost-reference run clears the gate.
    const Json::Value ackable = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ),
                   { { "primary", "ghost-ref" } } ),
        facts, authority.provider() );
    REQUIRE( ackable["report"]["verdict"].asString() == "requires_ack" );
    Json::Value ackArgs = checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ),
                                     { { "primary", "ghost-ref" } } );
    Json::Value ackCodes( Json::arrayValue );
    for ( const auto &f : ackable["report"]["findings"] )
        if ( f["severity"].asString() == "require_ack" && !f["acknowledged"].asBool() )
            ackCodes.append( f["code"].asString() );
    REQUIRE( ackCodes.size() > 0 );
    ackArgs["acknowledgements"] = ackCodes;
    const Json::Value acked = preflightCheckJson( ackArgs, facts, authority.provider() );
    REQUIRE( acked["report"]["verdict"].asString() == "ok" );
}

TEST_CASE( "malformed check arguments fail closed with typed error documents",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "scene-a", opticalPassportJson( "asset-demo" ).toStyledString() );
    MapAuthority authority;
    authority.set( "rs:index", indexCapabilityJson() );

    Json::Value noOperator( Json::objectValue );
    noOperator["inputs"] = Json::Value( Json::arrayValue );
    Json::Value err1 = preflightCheckJson( noOperator, facts, authority.provider() );
    REQUIRE( err1["kind"].asString() == "sicnu.preflight.check_error/1" );

    Json::Value emptyInputs( Json::objectValue );
    emptyInputs["operator"] = "rs:index";
    emptyInputs["inputs"] = Json::Value( Json::arrayValue );
    Json::Value err2 = preflightCheckJson( emptyInputs, facts, authority.provider() );
    REQUIRE( err2["kind"].asString() == "sicnu.preflight.check_error/1" );

    Json::Value badInput( Json::objectValue );
    badInput["operator"] = "rs:index";
    Json::Value inputs( Json::arrayValue );
    inputs.append( "scene-a" );  // string, not {slot, ref}
    badInput["inputs"] = inputs;
    Json::Value err3 = preflightCheckJson( badInput, facts, authority.provider() );
    REQUIRE( err3["kind"].asString() == "sicnu.preflight.check_error/1" );

    // A malformed variant selector must never degrade to judging the base
    // entry (that would silently drop variant-scoped blockers): it is a
    // typed error, not a "pass".
    Json::Value badParams( Json::objectValue );
    badParams["operator"] = "rs:index";
    badParams["operator_params"] = "index=EVI";
    Json::Value inputsOk( Json::arrayValue );
    Json::Value inputOk( Json::objectValue );
    inputOk["slot"] = "primary";
    inputOk["ref"] = "scene-a";
    inputsOk.append( inputOk );
    badParams["inputs"] = inputsOk;
    Json::Value err4 = preflightCheckJson( badParams, facts, authority.provider() );
    REQUIRE( err4["kind"].asString() == "sicnu.preflight.check_error/1" );

    // Non-string acknowledgements entries are the same fail-closed class.
    Json::Value badAcks( Json::objectValue );
    badAcks["operator"] = "rs:index";
    Json::Value acks( Json::arrayValue );
    acks.append( 42 );
    badAcks["acknowledgements"] = acks;
    badAcks["inputs"] = inputsOk;
    Json::Value err5 = preflightCheckJson( badAcks, facts, authority.provider() );
    REQUIRE( err5["kind"].asString() == "sicnu.preflight.check_error/1" );
}

TEST_CASE( "authority load problems flip a missing entry to typed Unavailable",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    facts.set( "scene-a", opticalPassportJson( "asset-demo" ).toStyledString() );

    // A lookup that cannot serve anything plus recorded load problems: the
    // adapter must say Unavailable (authority broken), not Unknown (not
    // declared) — the diagnostic code is the reader's only signal.
    AuthorityCapabilityProvider broken(
        []( const std::string &, const Json::Value & ) { return Json::Value(); },
        { "capabilities directory not found: /install/missing" } );
    const Json::Value check = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } } ),
        facts, broken );
    const auto hit = findingsOf( check, "SPF_CAPABILITY_MIRROR_UNAVAILABLE" );
    REQUIRE( hit.size() == 1 );
    REQUIRE( hit[0].basis == "unknown" );
    REQUIRE( hit[0].evidence["detail"].asString().find( "not found" ) != std::string::npos );

    // Without load problems the same null lookup stays the typed Unknown.
    MapAuthority empty;
    const Json::Value unknown = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ), { { "primary", "scene-a" } } ),
        facts, empty.provider() );
    REQUIRE_FALSE( findingsOf( unknown, "SPF_OPERATOR_UNKNOWN" ).empty() );
    REQUIRE( findingsOf( unknown, "SPF_CAPABILITY_MIRROR_UNAVAILABLE" ).empty() );
}

TEST_CASE( "unresolved asset references are typed unknowns through the adapter",
           "[preflight][runtime]" )
{
    PassportFactsProvider facts;
    MapAuthority authority;
    authority.set( "rs:index", indexCapabilityJson() );
    const Json::Value check = preflightCheckJson(
        checkArgs( "rs:index", makeVariantParams( "index", "NDVI" ),
                   { { "primary", "ghost-ref" } } ),
        facts, authority.provider() );
    REQUIRE( check["report"]["verdict"].asString() == "requires_ack" );
    bool sawUnknownBasis = false;
    for ( const auto &f : check["report"]["findings"] )
        if ( f["basis"].asString() == "unknown" )
            sawUnknownBasis = true;
    REQUIRE( sawUnknownBasis );
}
