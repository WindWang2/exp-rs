/***************************************************************************
  tests/test_scientific_state_review.cpp
  RS14-01 Scientific Data Passport — Review Gate 1 fix oracles.

  Every test here pins a defect found by the adversarial review (or its
  fix): uncaught jsoncpp exceptions on untrusted input, unvalidated enum
  field types, silent field drops on "present: false", the evidence
  contract's symmetry (radiometric.unit carries a claim even without a
  dataset), and metadata-cap truncation surfacing as a note.
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"

#include <json/json.h>

#include <string>

using namespace sicnu::state;

namespace
{

/// A document nested far deeper than stackLimit=128.
std::string makeDeepDocument( int depth )
{
    std::string text;
    for ( int i = 0; i < depth; ++i )
        text += "{\"a\":";
    text += "1";
    for ( int i = 0; i < depth; ++i )
        text += "}";
    return text;
}

} // namespace

TEST_CASE( "over-deep untrusted JSON is a typed MalformedJson, never an abort",
           "[scientific_state][review1][p1_1]" )
{
    RemoteSensingAssetState state;
    AssetStateError error;
    // 512 nesting levels >> stackLimit 128; jsoncpp THROWS here rather than
    // returning false — the text overload must catch it.
    REQUIRE( !assetStateFromJson( makeDeepDocument( 512 ), state, error ) );
    REQUIRE( error.code == StateErrorCode::MalformedJson );
    REQUIRE( !error.message.empty() );
}

TEST_CASE( "non-string enum fields are typed InvalidField, never an exception",
           "[scientific_state][review1][p1_2]" )
{
    const RemoteSensingAssetState state;
    const Json::Value doc = assetStateToJson( state );

    struct Case
    {
        const char *field;
        const char *replacement;
    };
    const Case cases[] = {
        { "kind", "identity" },      // identity.kind = {...}
        { "lifecycle", "identity" }, // identity.lifecycle = {...}
    };
    for ( const Case &item : cases )
    {
        Json::Value mutated = doc;
        mutated[ "identity" ][ item.field ] = Json::Value( Json::objectValue );
        RemoteSensingAssetState decoded;
        AssetStateError error;
        REQUIRE( !assetStateFromJson( mutated, decoded, error ) );
        REQUIRE( error.code == StateErrorCode::InvalidField );
    }

    Json::Value mutatedModality = doc;
    mutatedModality[ "sensor" ][ "modality" ] = Json::Value( Json::arrayValue );
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( !assetStateFromJson( mutatedModality, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );

    Json::Value mutatedClaim = doc;
    Json::Value claim( Json::objectValue );
    claim[ "path" ] = "x";
    claim[ "kind" ] = 42;
    mutatedClaim[ "claims" ].append( claim );
    REQUIRE( !assetStateFromJson( mutatedClaim, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );
}

TEST_CASE( "is_derived:false and present:false keep parsing the rest of the document",
           "[scientific_state][review1][p2_1]" )
{
    // A canonical document never carries these flags as false, but the
    // public parser must not silently drop everything after them either.
    const std::string text = R"( {
        "schema": "sicnu.asset_state.v1",
        "identity": { "display_name": "kept" },
        "provenance": { "is_derived": false },
        "confidence": 0.5,
        "unknowns": [ "sensor.modality" ],
        "claims": [ { "path": "sensor.modality", "kind": "unknown" } ]
    } )";
    RemoteSensingAssetState state;
    AssetStateError error;
    REQUIRE( assetStateFromJson( text, state, error ) );
    REQUIRE( state.displayName == "kept" );
    REQUIRE( !state.provenance.isDerived );
    REQUIRE( state.confidence == 0.5 );
    REQUIRE( state.claims.size() == 1 );
    REQUIRE( state.unknowns.size() == 1 );
}

TEST_CASE( "radiometric.unit carries a typed claim even without a dataset",
           "[scientific_state][review1][p2_5]" )
{
    CatalogFacts catalog;
    catalog.assetId = "x";
    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const ClaimRecord claim = claimFor( state, "radiometric.unit" );
    REQUIRE( claim.kind == ClaimKind::Unknown );
    bool listed = false;
    for ( const std::string &path : state.unknowns )
        listed = listed || path == "radiometric.unit";
    REQUIRE( listed );
}

TEST_CASE( "metadata caps surface as an explicit note, never silently",
           "[scientific_state][review1][p2_4]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/noisy.tif";
    dataset.metadata.setCap( 8 );
    for ( int i = 0; i < 20; ++i )
    {
        dataset.metadata.add( "KEY_" + std::to_string( i ), "v", "gdal:KEY" );
    }
    dataset.droppedMetadataItems = dataset.metadata.dropped();
    REQUIRE( dataset.droppedMetadataItems == 12 );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    bool hasNote = false;
    for ( const ResolutionNote &note : state.notes )
        hasNote = hasNote || note.code == "facts.metadata_truncated";
    REQUIRE( hasNote );
}

TEST_CASE( "non-finite doubles are typed InvalidField, never a self-unreadable document",
           "[scientific_state][review2][p1_nonfinite]" )
{
    // RED on master: jsoncpp parses 1e+9999 into +inf, isNumeric() accepted
    // it, and the state re-serialized it to a non-strict token that strict
    // parsers (and our own NaN->null re-read path) reject. Fail closed at
    // the reader instead.
    const RemoteSensingAssetState state;
    const Json::Value doc = assetStateToJson( state );

    Json::Value mutated = doc;
    mutated[ "geometry" ][ "pixel_size_x" ] = 1e+9999;
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( !assetStateFromJson( mutated, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );
    REQUIRE( error.message.find( "finite" ) != std::string::npos );

    Json::Value mutatedNoData = doc;
    mutatedNoData[ "bands" ].append( Json::Value( Json::objectValue ) );
    mutatedNoData[ "bands" ][ 0 ][ "no_data_value" ] = -1e+9999;
    RemoteSensingAssetState decodedBand;
    AssetStateError bandError;
    REQUIRE( !assetStateFromJson( mutatedNoData, decodedBand, bandError ) );
    REQUIRE( bandError.code == StateErrorCode::InvalidField );
}

// ---------------------------------------------------------------------------
// R2 review gate: positional catalog band pairing
//
// resolveBands pairs dataset band i with catalog structure band i. The
// pairing is only founded when both sides describe the SAME structure; a
// stale mirror (asset re-registered after the file gained a band) must not
// feed its roles/noData into the file's bands.
// ---------------------------------------------------------------------------

namespace
{

bool stateHasNote( const RemoteSensingAssetState &state, const char *code )
{
    for ( const ResolutionNote &note : state.notes )
    {
        if ( note.code == code )
            return true;
    }
    return false;
}

DatasetFacts makePlainBands( int count )
{
    DatasetFacts facts;
    facts.sourcePath = "/data/pairing.tif";
    facts.driverName = "GTiff";
    facts.bandCount = count;
    for ( int i = 1; i <= count; ++i )
    {
        BandFacts band;
        band.index = i;
        band.dataType = "Byte";
        facts.bands.push_back( band );
    }
    return facts;
}

CatalogFacts makeCatalogRoles( const std::vector<const char *> &roles )
{
    CatalogFacts catalog;
    catalog.assetId = "pairing";
    catalog.structureBandCount = static_cast<int>( roles.size() );
    for ( std::size_t i = 0; i < roles.size(); ++i )
    {
        CatalogBandFacts band;
        band.index = static_cast<int>( i ) + 1;
        band.role = roles[i];
        catalog.bands.push_back( band );
    }
    return catalog;
}

} // namespace

TEST_CASE( "a stale catalog mirror cannot feed roles into the file's bands",
           "[scientific_state][r2][catalog_pairing]" )
{
    // RED on master: bands paired by position with no structure check, so
    // the 3-band mirror silently labelled a 2-band file's band 1 "red".
    DatasetFacts dataset = makePlainBands( 2 );
    CatalogFacts catalog = makeCatalogRoles( { "red", "green", "nir" } );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( stateHasNote( state, "bands.catalog_structure_mismatch" ) );
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Unknown );
    REQUIRE( claimFor( state, "bands[2].role" ).kind == ClaimKind::Unknown );
}

TEST_CASE( "matching structures still pair dataset bands with the mirror",
           "[scientific_state][r2][catalog_pairing]" )
{
    DatasetFacts dataset = makePlainBands( 2 );
    CatalogFacts catalog = makeCatalogRoles( { "red", "nir" } );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !stateHasNote( state, "bands.catalog_structure_mismatch" ) );
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Known );
    REQUIRE( state.bands[0].role == "red" );
    REQUIRE( state.bands[1].role == "nir" );
}

TEST_CASE( "catalog-only passports keep the structure mirror as their authority",
           "[scientific_state][r2][catalog_pairing]" )
{
    CatalogFacts catalog = makeCatalogRoles( { "red", "green", "nir" } );

    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !stateHasNote( state, "bands.catalog_structure_mismatch" ) );
    REQUIRE( state.bands.size() == 3 );
    REQUIRE( claimFor( state, "bands[3].role" ).kind == ClaimKind::Known );
}
