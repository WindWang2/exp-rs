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
