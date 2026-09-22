/***************************************************************************
  tests/test_scientific_state_core.cpp
  RS14-01 Scientific Data Passport — Slice A: core value types + schema
  round-trip + deterministic ordering.

  Lane: sicnu_add_sdk_test (Catch2 + jsoncpp only — no Qt, no QGIS, no GDAL).
  The scientific state layer is a read-only PROJECTION of recorded truth;
  these tests pin the versioned schema contract (sicnu.asset_state.v1):
  byte-deterministic serialization, round-trip fidelity, typed refusals.
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_types.h"

#include <json/json.h>

using namespace sicnu::state;

namespace
{

/// A small but non-trivial state: every section populated at least once so
/// round-trip tests exercise all serializers.
RemoteSensingAssetState makeSampleState()
{
    RemoteSensingAssetState state;
    state.assetId = "0b7fd6f2-1111-4f0e-9a31-52d4a1b9c777";
    state.revision = "3";
    state.sourcePath = "/data/landsat/LC08_L2SP.tif";
    state.displayName = "LC08 L2SP scene";
    state.kind = AssetKind::Raster;
    state.lifecycle = AssetLifecycle::Ready;
    state.persistence = "persistent";

    state.sensor.platform = "LANDSAT_8";
    state.sensor.instrument = "OLI_TIRS";
    state.sensor.modality = Modality::Optical;
    state.sensor.productFamily = "landsat_c2_l2";
    state.sensor.processingLevel = "L2SP";

    state.acquisition.timeIso = "2024-05-01T10:12:31Z";
    state.acquisition.timeSource = "metadata";
    state.acquisition.precision = "second";
    state.acquisition.valid = true;

    BandState band;
    band.index = 1;
    band.name = "B1";
    band.role = "coastal";
    band.hasWavelengthNm = true;
    band.wavelengthNm = 443.0;
    band.hasFwhmNm = true;
    band.fwhmNm = 16.0;
    band.dataType = "UInt16";
    band.hasNoData = true;
    band.noDataValue = 0.0;
    band.hasScale = true;
    band.scale = 10000.0;
    state.bands.push_back( band );

    state.geometry.hasCrs = true;
    state.geometry.crsAuthid = "EPSG:32650";
    state.geometry.crsWkt = "PROJCS[\"WGS 84 / UTM zone 50N\"]";
    state.geometry.crsProjected = true;
    state.geometry.hasGeoTransform = true;
    state.geometry.geoTransform = { 499980.0, 30.0, 0.0, 4800000.0, 0.0, -30.0 };
    state.geometry.hasPixelSize = true;
    state.geometry.pixelSizeX = 30.0;
    state.geometry.pixelSizeY = -30.0;
    state.geometry.hasSize = true;
    state.geometry.width = 7681;
    state.geometry.height = 7791;

    state.validity.noDataPolicy = "declared";

    state.radiometric.unit = "surface_reflectance";
    state.radiometric.declaredRaw = "surface_reflectance";
    state.radiometric.hasNumericScale = true;
    state.radiometric.numericScale = 10000.0;

    ProvenanceInputRef input;
    input.assetId = "aaaaaaaa-1111-4f0e-9a31-52d4a1b9c777";
    input.revision = "1";
    input.bandReferences = { "nir", "red" };
    input.valueDomain = "surface_reflectance";
    state.provenance.isDerived = true;
    state.provenance.algorithmId = "rs:spectral_index";
    state.provenance.algorithmVersion = "1.4";
    state.provenance.inputs.push_back( input );
    state.provenance.completedAtUtc = "2026-09-20T08:00:00Z";
    state.provenance.executionFingerprint = "fp-123";
    state.provenance.softwareVersion = "13.0.0";

    state.modelDerived.present = true;
    state.modelDerived.modelKind = "classification";
    state.modelDerived.labels = { "water", "vegetation" };
    state.modelDerived.hasAccuracy = true;
    state.modelDerived.accuracy = 0.91;
    state.modelDerived.sidecarPath = "/data/models/rf.meta.json";

    ClaimRecord claim;
    claim.path = "radiometric.unit";
    claim.kind = ClaimKind::Known;
    claim.sources = { "gdal:SICNU_RADIOMETRIC_STATE" };
    state.claims.push_back( claim );

    ClaimRecord inferred;
    inferred.path = "bands[1].role";
    inferred.kind = ClaimKind::Inferred;
    inferred.sources = { "sensor_profile:landsat_c2_l2" };
    inferred.note = "role from sensor profile band axis";
    state.claims.push_back( inferred );

    state.assumptions = { "radiometric.unit assumed by FSM default" };
    state.unknowns = { "validity.cloud_cover" };
    state.confidence = 0.75;

    return state;
}

} // namespace

TEST_CASE( "schema constants are pinned", "[scientific_state][slice_a]" )
{
    REQUIRE( std::string( kAssetStateSchemaId ) == "sicnu.asset_state.v1" );
    REQUIRE( std::string( kAssetStateDiffSchemaId ) == "sicnu.asset_state_diff.v1" );
    REQUIRE( kMaxPassportBands == 4096 );
}

TEST_CASE( "claim kind string round-trips for every kind", "[scientific_state][slice_a]" )
{
    for ( const ClaimKind kind : { ClaimKind::Known, ClaimKind::Inferred, ClaimKind::Assumed,
                                   ClaimKind::Unknown, ClaimKind::Conflicted } )
    {
        const std::string text = claimKindToString( kind );
        REQUIRE( !text.empty() );
        ClaimKind parsed = ClaimKind::Unknown;
        REQUIRE( claimKindFromString( text, parsed ) );
        REQUIRE( parsed == kind );
    }
    ClaimKind parsed = ClaimKind::Known;
    REQUIRE( claimKindFromString( "KNOWN", parsed ) ); // case-insensitive
    REQUIRE( parsed == ClaimKind::Known );
    REQUIRE( !claimKindFromString( "proven", parsed ) ); // foreign word refused
}

TEST_CASE( "serialization is deterministic and stable across repeated calls",
           "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState state = makeSampleState();
    const std::string first = serializeState( state );
    const std::string second = serializeState( state );
    REQUIRE( first == second );
    REQUIRE( !first.empty() );

    Json::Value doc;
    Json::CharReaderBuilder builder;
    builder[ "stackLimit" ] = 128;
    const std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    Json::Value parsed;
    REQUIRE( reader->parse( first.data(), first.data() + first.size(), &parsed, nullptr ) );
    REQUIRE( parsed[ "schema" ].asString() == "sicnu.asset_state.v1" );
    REQUIRE( parsed[ "identity" ][ "display_name" ].asString() == "LC08 L2SP scene" );
}

TEST_CASE( "round trip preserves values and claims", "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState state = makeSampleState();
    const Json::Value json = assetStateToJson( state );
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( json, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::None );
    // Serialization normalizes (sorts) collections; equality is against the
    // canonical form of the input. toJson must not mutate its argument.
    RemoteSensingAssetState canonical = state;
    normalizeState( canonical );
    REQUIRE( decoded == canonical );
}

TEST_CASE( "round trip re-serialization is byte-identical", "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState state = makeSampleState();
    const std::string bytes = serializeState( state );
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( bytes, decoded, error ) );
    REQUIRE( serializeState( decoded ) == bytes );
}

TEST_CASE( "claims array is serialized sorted by path regardless of insertion order",
           "[scientific_state][slice_a]" )
{
    RemoteSensingAssetState state = makeSampleState();
    state.claims.clear();
    ClaimRecord late;
    late.path = "sensor.platform";
    late.kind = ClaimKind::Known;
    late.sources = { "gdal:SICNU_PLATFORM" };
    ClaimRecord early;
    early.path = "acquisition.time";
    early.kind = ClaimKind::Known;
    early.sources = { "catalog:AssetSnapshot" };
    state.claims.push_back( late );  // deliberately out of order
    state.claims.push_back( early );

    const std::string bytes = serializeState( state );
    const std::size_t earlyPos = bytes.find( "\"acquisition.time\"" );
    const std::size_t latePos = bytes.find( "\"sensor.platform\"" );
    REQUIRE( earlyPos != std::string::npos );
    REQUIRE( latePos != std::string::npos );
    REQUIRE( earlyPos < latePos );

    // And the in-memory normalization is stable too.
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( bytes, decoded, error ) );
    REQUIRE( decoded.claims.size() == 2 );
    REQUIRE( decoded.claims.front().path == "acquisition.time" );
}

TEST_CASE( "claimFor synthesizes unknown only at query time",
           "[scientific_state][slice_a]" )
{
    RemoteSensingAssetState state = makeSampleState();
    const ClaimRecord synthesized = claimFor( state, "geometry.crs" );
    REQUIRE( synthesized.kind == ClaimKind::Unknown );
    REQUIRE( synthesized.path == "geometry.crs" );
    // Synthesis must not leak into the serialized document.
    REQUIRE( serializeState( state ).find( "\"geometry.crs\"" ) == std::string::npos );

    const ClaimRecord explicit_ = claimFor( state, "radiometric.unit" );
    REQUIRE( explicit_.kind == ClaimKind::Known );
}

TEST_CASE( "foreign or missing schema id is refused with a typed error",
           "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState state = makeSampleState();

    Json::Value foreign = assetStateToJson( state );
    foreign[ "schema" ] = "some.other.asset.v9";
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( !assetStateFromJson( foreign, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::SchemaMismatch );

    Json::Value missing = assetStateToJson( state );
    Json::Value removed;
    removed[ "identity" ] = missing[ "identity" ];
    REQUIRE( !assetStateFromJson( removed, decoded, error ) );
    REQUIRE( error.code == StateErrorCode::SchemaMismatch );
}

TEST_CASE( "malformed json text is refused with a typed error",
           "[scientific_state][slice_a]" )
{
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( !assetStateFromJson( std::string( "{ not json" ), decoded, error ) );
    REQUIRE( error.code == StateErrorCode::MalformedJson );
    REQUIRE( !error.message.empty() );
}

TEST_CASE( "confidence is clamped to [0,1] on parse", "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState state = makeSampleState();
    Json::Value json = assetStateToJson( state );
    json[ "confidence" ] = 5.0;
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( json, decoded, error ) );
    REQUIRE( decoded.confidence <= 1.0 );
    json[ "confidence" ] = -2.0;
    REQUIRE( assetStateFromJson( json, decoded, error ) );
    REQUIRE( decoded.confidence >= 0.0 );
}

TEST_CASE( "empty state serializes and round-trips", "[scientific_state][slice_a]" )
{
    const RemoteSensingAssetState empty;
    const std::string bytes = serializeState( empty );
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( bytes, decoded, error ) );
    RemoteSensingAssetState canonical = empty;
    normalizeState( canonical );
    REQUIRE( decoded == canonical );
    REQUIRE( decoded.kind == AssetKind::Unknown );
    REQUIRE( empty.kind == AssetKind::Unknown );
}

TEST_CASE( "asset kind and modality strings round-trip", "[scientific_state][slice_a]" )
{
    for ( const AssetKind kind : { AssetKind::Unknown, AssetKind::Raster, AssetKind::Vector,
                                   AssetKind::RemoteMap, AssetKind::VirtualRaster } )
    {
        AssetKind parsed = AssetKind::Unknown;
        REQUIRE( assetKindFromString( assetKindToString( kind ), parsed ) );
        REQUIRE( parsed == kind );
    }
    for ( const Modality modality : { Modality::Unknown, Modality::Optical, Modality::Sar,
                                      Modality::Thermal, Modality::Hyperspectral } )
    {
        Modality parsed = Modality::Unknown;
        REQUIRE( modalityFromString( modalityToString( modality ), parsed ) );
        REQUIRE( parsed == modality );
    }
}
