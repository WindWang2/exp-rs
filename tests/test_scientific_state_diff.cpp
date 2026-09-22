/***************************************************************************
  tests/test_scientific_state_diff.cpp
  RS14-01 Scientific Data Passport — Slice E: confidence lattice + state
  diff API.

  Lane: sicnu_add_sdk_test (Catch2 + jsoncpp only).

  Confidence formula (deterministic, computed by the resolver over 8 key
  paths; each path scores known=1.0, inferred=0.75, assumed=0.25,
  conflicted/unknown=0; confidence = sum / applicable-path count, rounded to
  3 decimals — full contract in scientific_state/asset_state_schema.h):

    identity.asset_id        only when a catalog is present
    sensor.modality          always
    bands[*].role            when bands exist; scores the worst band (full
                             score only when every band role resolves)
    radiometric.unit         always
    acquisition.time         always
    geometry.crs             only when a dataset is present
    provenance.algorithm     only when a derivation record is present
    validity.noDataPolicy    only when bands exist

  The diff API flattens two canonical state documents into path → scalar
  text maps (values compared as canonical JSON text; a claim record lives at
  the special path "claims[<claim path>]") and reports sorted FieldDiffs:
  "changed" | "added" | "removed" for values, "claim_changed" when the same
  claim path carries a different ClaimRecord (before/after carry the claim
  kind texts). Serialization is byte-deterministic.
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_diff.h"
#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"

#include <json/json.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace sicnu::state;

namespace
{

bool hasDiff( const StateDiff &diff, const std::string &path, const std::string &kind )
{
    for ( const FieldDiff &field : diff.diffs )
    {
        if ( field.path == path && field.kind == kind )
            return true;
    }
    return false;
}

const FieldDiff *findDiff( const StateDiff &diff, const std::string &path )
{
    for ( const FieldDiff &field : diff.diffs )
    {
        if ( field.path == path )
            return &field;
    }
    return nullptr;
}

/// Sentinel-2-like dataset: declared sensor, roles, wavelengths, L2A
/// radiometric state, CRS and geotransform — every key path known.
DatasetFacts makeKnownSentinel2()
{
    DatasetFacts facts;
    facts.sourcePath = "/data/s2/S2A_MSIL2A.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 3;
    facts.metadata.add( "SICNU_SENSOR", "MSI", "gdal:SICNU_SENSOR" );
    facts.metadata.add( "SICNU_PLATFORM", "SENTINEL_2A", "gdal:SICNU_PLATFORM" );
    facts.metadata.add( "SICNU_MODALITY", "optical", "gdal:SICNU_MODALITY" );
    facts.metadata.add( "SICNU_RADIOMETRIC_STATE", "surface_reflectance",
                        "gdal:SICNU_RADIOMETRIC_STATE" );
    facts.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z",
                        "gdal:SICNU_ACQUISITION_DATE" );
    facts.geometry.hasCrs = true;
    facts.geometry.crsAuthid = "EPSG:32631";
    facts.geometry.crsProjected = true;
    facts.geometry.hasGeoTransform = true;
    facts.geometry.geoTransform = { 499980.0, 10.0, 0.0, 3900000.0, 0.0, -10.0 };
    facts.geometry.width = 3;
    facts.geometry.height = 3;

    const char *roles[3] = { "blue", "green", "red" };
    for ( int i = 0; i < 3; ++i )
    {
        BandFacts band;
        band.index = i + 1;
        band.name = "B" + std::to_string( i + 1 );
        band.dataType = "UInt16";
        band.metadata.add( "SICNU_BAND_ROLE", roles[i], "gdal:SICNU_BAND_ROLE" );
        facts.bands.push_back( band );
    }
    return facts;
}

/// Catalog that mirrors the known S2 dataset (identity + noData declarations).
CatalogFacts makeKnownCatalog()
{
    CatalogFacts catalog;
    catalog.assetId = "0b7fd6f2-1111-4f0e-9a31-52d4a1b9c777";
    const char *roles[3] = { "blue", "green", "red" };
    for ( int i = 0; i < 3; ++i )
    {
        CatalogBandFacts band;
        band.index = i + 1;
        band.role = roles[i];
        band.hasNoData = true;
        band.noDataValue = 0.0;
        catalog.bands.push_back( band );
    }
    return catalog;
}

/// Raw single-band dataset: nothing declared anywhere.
DatasetFacts makeRawOneBand()
{
    DatasetFacts facts;
    facts.sourcePath = "/data/raw.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 1;
    BandFacts band;
    band.index = 1;
    band.dataType = "UInt16";
    facts.bands.push_back( band );
    return facts;
}

/// One-band dataset with a declared band role and wavelength; the radiometric
/// state is per-test (DN before calibration, TOA after).
DatasetFacts makeCalibrationSide( const char *radiometricState )
{
    DatasetFacts facts;
    facts.sourcePath = "/data/scene.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 1;
    facts.geometry.hasCrs = true;
    facts.geometry.crsAuthid = "EPSG:32631";
    BandFacts band;
    band.index = 1;
    band.name = "B1";
    band.dataType = "UInt16";
    band.metadata.add( "SICNU_BAND_ROLE", "nir", "gdal:SICNU_BAND_ROLE" );
    band.metadata.add( "WAVELENGTH", "842", "gdal:WAVELENGTH" );
    facts.bands.push_back( band );
    if ( radiometricState )
        facts.metadata.add( "SICNU_RADIOMETRIC_STATE", radiometricState,
                            "gdal:SICNU_RADIOMETRIC_STATE" );
    return facts;
}

} // namespace

// ---------------------------------------------------------------------------
// Confidence
// ---------------------------------------------------------------------------

TEST_CASE( "fully known sentinel-2 resolution reaches confidence 1.0",
           "[scientific_state][slice_e]" )
{
    StateResolutionInput input;
    input.dataset = makeKnownSentinel2();
    input.catalog = makeKnownCatalog();

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.confidence == 1.0 );
}

TEST_CASE( "FSM default raw dataset scores a deterministic low confidence",
           "[scientific_state][slice_e]" )
{
    // Applicable paths (no catalog, no derivation): sensor.modality unknown 0,
    // bands[1].role unknown 0, radiometric.unit assumed 0.25, acquisition.time
    // unknown 0, geometry.crs unknown 0, validity.noDataPolicy (undeclared)
    // inferred 0.75 → 1.0 / 6 = 0.167.
    StateResolutionInput input;
    input.dataset = makeRawOneBand();

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.confidence == 0.167 );
}

TEST_CASE( "with a catalog the identity path joins the denominator",
           "[scientific_state][slice_e]" )
{
    // Same raw dataset plus a catalog assetId: identity.asset_id known 1.0
    // adds a path → 2.0 / 7 = 0.286.
    StateResolutionInput input;
    input.dataset = makeRawOneBand();
    CatalogFacts catalog;
    catalog.assetId = "asset-x";
    input.catalog = catalog;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.confidence == 0.286 );
}

TEST_CASE( "conflicted band roles score zero in the confidence lattice",
           "[scientific_state][slice_e]" )
{
    DatasetFacts dataset = makeRawOneBand();
    dataset.bands[0].metadata.add( "SICNU_BAND_ROLE", "red", "gdal:SICNU_BAND_ROLE" );
    dataset.metadata.add( "SICNU_MODALITY", "optical", "gdal:SICNU_MODALITY" );

    CatalogFacts catalog;
    catalog.assetId = "asset-x";
    CatalogBandFacts catalogBand;
    catalogBand.index = 1;
    catalogBand.role = "nir";
    catalog.bands.push_back( catalogBand );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Conflicted );
    // 1 (identity) + 1 (modality) + 0 (conflicted role) + 0.25 (assumed unit)
    // + 0 (acquisition) + 0 (geometry.crs) + 0.75 (undeclared policy) = 3 / 7.
    REQUIRE( state.confidence == 0.429 );
}

// ---------------------------------------------------------------------------
// Diff
// ---------------------------------------------------------------------------

TEST_CASE( "calibration DN to toa_reflectance changes only the radiometric paths",
           "[scientific_state][slice_e]" )
{
    StateResolutionInput beforeInput;
    beforeInput.dataset = makeCalibrationSide( nullptr );  // raw DN (FSM default)
    StateResolutionInput afterInput;
    afterInput.dataset = makeCalibrationSide( "toa_reflectance" );

    const RemoteSensingAssetState before = resolveAssetState( beforeInput ).state;
    const RemoteSensingAssetState after = resolveAssetState( afterInput ).state;
    const StateDiff diff = diffStates( before, after );

    REQUIRE( !diff.empty() );

    // The radiometric unit value changed...
    const FieldDiff *unit = findDiff( diff, "radiometric.unit" );
    REQUIRE( unit != nullptr );
    REQUIRE( unit->kind == "changed" );
    REQUIRE( unit->before == "digital_number" );
    REQUIRE( unit->after == "toa_reflectance" );

    // ...and its evidence claim flipped assumed → known.
    const FieldDiff *claim = findDiff( diff, "claims[radiometric.unit]" );
    REQUIRE( claim != nullptr );
    REQUIRE( claim->kind == "claim_changed" );
    REQUIRE( claim->before == "assumed" );
    REQUIRE( claim->after == "known" );

    // Bands (role, wavelength) and geometry are untouched; there is a
    // measurable unchanged set.
    REQUIRE( !hasDiff( diff, "bands[1].role", "changed" ) );
    REQUIRE( !hasDiff( diff, "claims[bands[1].role]", "claim_changed" ) );
    REQUIRE( findDiff( diff, "geometry.crs" ) == nullptr );
    REQUIRE( diff.unchangedFields > 0 );
}

TEST_CASE( "a freshly derived asset adds its provenance fields", "[scientific_state][slice_e]" )
{
    StateResolutionInput beforeInput;
    beforeInput.dataset = makeKnownSentinel2();
    StateResolutionInput afterInput = beforeInput;
    DerivationFacts derivation;
    derivation.algorithmId = "sicnu.ndvi.change";
    afterInput.derivation = derivation;

    const RemoteSensingAssetState before = resolveAssetState( beforeInput ).state;
    const RemoteSensingAssetState after = resolveAssetState( afterInput ).state;
    const StateDiff diff = diffStates( before, after );

    REQUIRE( hasDiff( diff, "provenance.is_derived", "added" ) );
    REQUIRE( hasDiff( diff, "provenance.algorithm_id", "added" ) );
    REQUIRE( hasDiff( diff, "claims[provenance.algorithm]", "added" ) );
}

TEST_CASE( "losing the numeric scale is a removed field", "[scientific_state][slice_e]" )
{
    DatasetFacts withScale = makeKnownSentinel2();
    withScale.metadata.add( "SICNU_NUMERIC_SCALE", "10000", "gdal:SICNU_NUMERIC_SCALE" );

    StateResolutionInput beforeInput;
    beforeInput.dataset = withScale;
    StateResolutionInput afterInput;
    afterInput.dataset = makeKnownSentinel2();

    const RemoteSensingAssetState before = resolveAssetState( beforeInput ).state;
    const RemoteSensingAssetState after = resolveAssetState( afterInput ).state;
    const StateDiff diff = diffStates( before, after );

    const FieldDiff *scale = findDiff( diff, "radiometric.numeric_scale" );
    REQUIRE( scale != nullptr );
    REQUIRE( scale->kind == "removed" );
    REQUIRE( scale->before == "10000.0" );
    REQUIRE( scale->after.empty() );
}

TEST_CASE( "two empty states diff to an empty diff", "[scientific_state][slice_e]" )
{
    const StateDiff diff = diffStates( RemoteSensingAssetState{}, RemoteSensingAssetState{} );
    REQUIRE( diff.empty() );
    REQUIRE( diff.diffs.empty() );
    REQUIRE( diff.unchangedFields > 0 );
}

TEST_CASE( "same input resolves to an empty diff", "[scientific_state][slice_e]" )
{
    StateResolutionInput input;
    input.dataset = makeKnownSentinel2();
    input.catalog = makeKnownCatalog();

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const StateDiff diff = diffStates( state, state );
    REQUIRE( diff.empty() );
}

TEST_CASE( "diff output is byte-deterministic", "[scientific_state][slice_e]" )
{
    StateResolutionInput beforeInput;
    beforeInput.dataset = makeCalibrationSide( nullptr );
    StateResolutionInput afterInput;
    afterInput.dataset = makeCalibrationSide( "toa_reflectance" );

    const RemoteSensingAssetState before = resolveAssetState( beforeInput ).state;
    const RemoteSensingAssetState after = resolveAssetState( afterInput ).state;

    const StateDiff first = diffStates( before, after );
    const StateDiff second = diffStates( before, after );
    REQUIRE( first == second );
    REQUIRE( serializeStateDiff( first ) == serializeStateDiff( second ) );
    REQUIRE( !serializeStateDiff( first ).empty() );
}

TEST_CASE( "diff documents round-trip through JSON", "[scientific_state][slice_e]" )
{
    StateResolutionInput beforeInput;
    beforeInput.dataset = makeCalibrationSide( nullptr );
    StateResolutionInput afterInput;
    afterInput.dataset = makeCalibrationSide( "toa_reflectance" );

    const StateDiff diff =
        diffStates( resolveAssetState( beforeInput ).state, resolveAssetState( afterInput ).state );

    const Json::Value json = stateDiffToJson( diff );
    StateDiff parsed;
    AssetStateError error;
    REQUIRE( stateDiffFromJson( json, parsed, error ) );
    REQUIRE( error.ok() );
    REQUIRE( parsed == diff );
    REQUIRE( parsed.schemaId == kAssetStateDiffSchemaId );
    REQUIRE( serializeStateDiff( parsed ) == serializeStateDiff( diff ) );
}

TEST_CASE( "diff parsing rejects a foreign schema and unknown kinds with typed errors",
           "[scientific_state][slice_e]" )
{
    StateDiff diff;
    AssetStateError error;

    Json::Value foreign( Json::objectValue );
    foreign[ "schema" ] = "sicnu.other.v1";
    REQUIRE( !stateDiffFromJson( foreign, diff, error ) );
    REQUIRE( error.code == StateErrorCode::SchemaMismatch );

    Json::Value badKind( Json::objectValue );
    badKind[ "schema" ] = kAssetStateDiffSchemaId;
    Json::Value diffs( Json::arrayValue );
    Json::Value entry( Json::objectValue );
    entry[ "path" ] = "radiometric.unit";
    entry[ "kind" ] = "exploded";
    diffs.append( entry );
    badKind[ "diffs" ] = diffs;
    REQUIRE( !stateDiffFromJson( badKind, diff, error ) );
    REQUIRE( error.code == StateErrorCode::InvalidField );
}
