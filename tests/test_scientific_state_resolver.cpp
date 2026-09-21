/***************************************************************************
  tests/test_scientific_state_resolver.cpp
  RS14-01 Scientific Data Passport — Slice B: facts model + optical
  projection (identity, band roles/wavelengths/units, radiometric state).

  Lane: sicnu_add_sdk_test (Catch2 + jsoncpp only). The resolver is a pure
  function from source-tagged facts to a claim-annotated asset state; every
  test here pins one evidence-contract behaviour (known / inferred / assumed
  / unknown / conflicted).
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"

using namespace sicnu::state;

namespace
{

/// Convenience: resolve with only a dataset.
ResolveOutcome resolveDataset( const DatasetFacts &dataset )
{
    StateResolutionInput input;
    input.dataset = dataset;
    return resolveAssetState( input );
}

/// Sentinel-2-like optical dataset: declared roles, wavelengths, L2A scale.
DatasetFacts makeSentinel2Dataset()
{
    DatasetFacts facts;
    facts.sourcePath = "/data/s2/S2A_MSIL2A.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 3;

    facts.metadata.add( "SICNU_SENSOR", "MSI", "gdal:SICNU_SENSOR" );
    facts.metadata.add( "SICNU_PLATFORM", "SENTINEL_2A", "gdal:SICNU_PLATFORM" );
    facts.metadata.add( "SICNU_MODALITY", "optical", "gdal:SICNU_MODALITY" );
    facts.metadata.add( "SICNU_PRODUCT_ID", "S2A_MSIL2A_20240501", "gdal:SICNU_PRODUCT_ID" );
    facts.metadata.add( "SICNU_PROCESSING_LEVEL", "L2A", "gdal:SICNU_PROCESSING_LEVEL" );
    facts.metadata.add( "SICNU_RADIOMETRIC_STATE", "surface_reflectance",
                        "gdal:SICNU_RADIOMETRIC_STATE" );
    facts.metadata.add( "SICNU_NUMERIC_SCALE", "10000", "gdal:SICNU_NUMERIC_SCALE" );
    facts.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z",
                        "gdal:SICNU_ACQUISITION_DATE" );

    const char *roles[3] = { "blue", "green", "red" };
    const double wavelengths[3] = { 492.0, 560.0, 665.0 };
    for ( int i = 0; i < 3; ++i )
    {
        BandFacts band;
        band.index = i + 1;
        band.name = i == 0 ? "B1" : ( i == 1 ? "B2" : "B3" );
        band.dataType = "UInt16";
        band.metadata.add( "SICNU_BAND_ROLE", roles[i], "gdal:SICNU_BAND_ROLE" );
        band.metadata.add( "WAVELENGTH", std::to_string( wavelengths[i] ),
                           "gdal:WAVELENGTH" );
        facts.bands.push_back( band );
    }
    return facts;
}

} // namespace

TEST_CASE( "dataset key/value items round-trip observations per key",
           "[scientific_state][slice_b]" )
{
    MetadataItems items;
    items.add( "SICNU_SENSOR", "MSI", "gdal:SICNU_SENSOR" );
    items.add( "SICNU_SENSOR", "OLI", "catalog:structure" );
    REQUIRE( items.find( "SICNU_SENSOR" ).size() == 2 );
    REQUIRE( items.find( "SICNU_MISSING" ).empty() );
}

TEST_CASE( "identity is projected from catalog facts as known claims",
           "[scientific_state][slice_b]" )
{
    CatalogFacts catalog;
    catalog.assetId = "0b7fd6f2-1111-4f0e-9a31-52d4a1b9c777";
    catalog.revision = "3";
    catalog.displayName = "LC08 scene";
    catalog.kind = "raster";
    catalog.lifecycle = "ready";
    catalog.persistence = "persistent";
    catalog.sourcePath = "/data/landsat/LC08.tif";

    StateResolutionInput input;
    input.catalog = catalog;

    const ResolveOutcome outcome = resolveAssetState( input );
    REQUIRE( outcome.state.assetId == "0b7fd6f2-1111-4f0e-9a31-52d4a1b9c777" );
    REQUIRE( outcome.state.revision == "3" );
    REQUIRE( outcome.state.displayName == "LC08 scene" );
    REQUIRE( outcome.state.kind == AssetKind::Raster );
    REQUIRE( outcome.state.lifecycle == AssetLifecycle::Ready );
    REQUIRE( outcome.state.sourcePath == "/data/landsat/LC08.tif" );

    const ClaimRecord claim = claimFor( outcome.state, "identity.asset_id" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "catalog:AssetSnapshot" } );
}

TEST_CASE( "sourcePath falls back through dataset then input", "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/from_gdal.tif";
    StateResolutionInput input;
    input.dataset = dataset;
    input.sourcePath = "/data/from_input.tif";
    REQUIRE( resolveAssetState( input ).state.sourcePath == "/data/from_gdal.tif" );

    StateResolutionInput inputOnly;
    inputOnly.sourcePath = "/data/from_input.tif";
    REQUIRE( resolveAssetState( inputOnly ).state.sourcePath == "/data/from_input.tif" );
}

TEST_CASE( "sentinel-2 optical facts project declared roles, wavelengths and radiometric state",
           "[scientific_state][slice_b]" )
{
    StateResolutionInput input;
    input.dataset = makeSentinel2Dataset();

    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands.size() == 3 );
    REQUIRE( state.bands[0].role == "blue" );
    REQUIRE( state.bands[2].role == "red" );
    REQUIRE( state.bands[1].hasWavelengthNm );
    REQUIRE( state.bands[1].wavelengthNm == 560.0 );

    // Declared band metadata ⇒ known claims.
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Known );
    REQUIRE( claimFor( state, "radiometric.unit" ).kind == ClaimKind::Known );
    REQUIRE( state.radiometric.unit == "surface_reflectance" );
    REQUIRE( state.radiometric.declaredRaw == "surface_reflectance" );
    REQUIRE( state.radiometric.hasNumericScale );
    REQUIRE( state.radiometric.numericScale == 10000.0 );

    // Sensor identity is declared by dataset metadata.
    REQUIRE( state.sensor.platform == "SENTINEL_2A" );
    REQUIRE( state.sensor.modality == Modality::Optical );
    REQUIRE( claimFor( state, "sensor.modality" ).kind == ClaimKind::Known );

    // Acquisition declared by dataset metadata.
    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2024-05-01T10:12:31Z" );
    REQUIRE( state.acquisition.timeSource == "metadata" );

    // Everything resolvable, nothing assumed here.
    REQUIRE( state.assumptions.empty() );
}

TEST_CASE( "band role falls back to sensor profile as an inferred claim",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/s2/stacked.tif";
    dataset.bandCount = 2;
    for ( int i = 0; i < 2; ++i )
    {
        BandFacts band;
        band.index = i + 1;
        band.dataType = "UInt16";
        dataset.bands.push_back( band );
    }

    SensorProfileFacts profile;
    profile.sensorKey = "sentinel2_msi";
    profile.modality = Modality::Optical;
    ProfileBand blue;
    blue.index = 1;
    blue.role = "blue";
    blue.hasWavelengthNm = true;
    blue.wavelengthNm = 492.0;
    ProfileBand green;
    green.index = 2;
    green.role = "green";
    profile.bands = { blue, green };

    StateResolutionInput input;
    input.dataset = dataset;
    input.sensorProfile = profile;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.bands[0].role == "blue" );
    REQUIRE( state.bands[0].hasWavelengthNm );
    REQUIRE( state.bands[0].wavelengthNm == 492.0 );

    const ClaimRecord roleClaim = claimFor( state, "bands[1].role" );
    REQUIRE( roleClaim.kind == ClaimKind::Inferred );
    REQUIRE( roleClaim.sources == std::vector<std::string>{ "sensor_profile:sentinel2_msi" } );

    bool hasNote = false;
    for ( const ResolutionNote &note : state.notes )
        hasNote = hasNote || note.code == "band_role.from_sensor_profile";
    REQUIRE( hasNote );
}

TEST_CASE( "band without any role source stays unknown and is reported",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/generic.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    band.dataType = "Float32";
    dataset.bands.push_back( band );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands[0].role.empty() );
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Unknown );
    bool listed = false;
    for ( const std::string &path : state.unknowns )
        listed = listed || path == "bands[1].role";
    REQUIRE( listed );
}

TEST_CASE( "conflicting declared band roles produce a conflicted claim with alternatives",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/conflict.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    band.dataType = "Byte";
    band.metadata.add( "SICNU_BAND_ROLE", "red", "gdal:SICNU_BAND_ROLE" );
    dataset.bands.push_back( band );

    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.structureBandCount = 1;
    CatalogBandFacts catalogBand;
    catalogBand.index = 1;
    catalogBand.role = "nir";
    catalog.bands.push_back( catalogBand );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const ClaimRecord claim = claimFor( state, "bands[1].role" );
    REQUIRE( claim.kind == ClaimKind::Conflicted );
    REQUIRE( claim.alternatives == std::vector<std::string>{ "nir", "red" } );
    REQUIRE( state.bands[0].role.empty() );
}

TEST_CASE( "wavelength unit normalization handles µm and refuses unknown units",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/um.tif";
    dataset.bandCount = 2;

    BandFacts umBand;
    umBand.index = 1;
    umBand.metadata.add( "WAVELENGTH", "0.492", "gdal:WAVELENGTH" );
    umBand.metadata.add( "WAVELENGTH_UNITS", "um", "gdal:WAVELENGTH_UNITS" );
    dataset.bands.push_back( umBand );

    BandFacts weirdBand;
    weirdBand.index = 2;
    weirdBand.metadata.add( "WAVELENGTH", "42", "gdal:WAVELENGTH" );
    weirdBand.metadata.add( "WAVELENGTH_UNITS", "furlongs", "gdal:WAVELENGTH_UNITS" );
    dataset.bands.push_back( weirdBand );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands[0].hasWavelengthNm );
    REQUIRE( state.bands[0].wavelengthNm == 492.0 );
    REQUIRE( !state.bands[1].hasWavelengthNm );
    REQUIRE( claimFor( state, "bands[2].wavelength_nm" ).kind == ClaimKind::Unknown );
    bool hasNote = false;
    for ( const ResolutionNote &note : state.notes )
        hasNote = hasNote || note.code == "wavelength.unknown_units";
    REQUIRE( hasNote );
}

TEST_CASE( "non-finite wavelength is refused as unknown, never stored",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/nan.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    band.metadata.add( "WAVELENGTH", "not-a-number", "gdal:WAVELENGTH" );
    dataset.bands.push_back( band );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( !state.bands[0].hasWavelengthNm );
    REQUIRE( claimFor( state, "bands[1].wavelength_nm" ).kind == ClaimKind::Unknown );
}

TEST_CASE( "missing radiometric marker projects the documented FSM default as assumed",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/raw.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    dataset.bands.push_back( band );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.radiometric.unit == "digital_number" );
    const ClaimRecord claim = claimFor( state, "radiometric.unit" );
    REQUIRE( claim.kind == ClaimKind::Assumed );
    bool hasNote = false;
    for ( const ResolutionNote &note : state.notes )
        hasNote = hasNote || note.code == "radiometric.fsm_default";
    REQUIRE( hasNote );
    REQUIRE( !state.assumptions.empty() );
}

TEST_CASE( "uppercase FSM vocabulary and lowercase product vocabulary normalize identically",
           "[scientific_state][slice_b]" )
{
    const char *values[] = { "TOA_REFLECTANCE", "toa_reflectance", "Toa_Reflectance" };
    for ( const char *value : values )
    {
        DatasetFacts dataset;
        dataset.sourcePath = "/data/x.tif";
        dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", value, "gdal:SICNU_RADIOMETRIC_STATE" );
        const RemoteSensingAssetState state = resolveDataset( dataset ).state;
        REQUIRE( state.radiometric.unit == "toa_reflectance" );
        REQUIRE( state.radiometric.declaredRaw == value );
    }
}

TEST_CASE( "sar calibration tokens project verbatim with domain", "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar.tif";
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_CALIBRATION", "gamma0", "gdal:SICNU_SAR_CALIBRATION" );
    dataset.metadata.add( "SICNU_SAR_DOMAIN", "db", "gdal:SICNU_SAR_DOMAIN" );

    const RemoteSensingAssetState state = resolveDataset( dataset ).state;
    REQUIRE( state.radiometric.unit == "gamma0" );
    REQUIRE( state.radiometric.domain == "db" );
    REQUIRE( state.sensor.modality == Modality::Sar );
}

TEST_CASE( "sar dual-key disagreement projects a conflicted radiometric state",
           "[scientific_state][slice_b]" )
{
    // Mirrors readDeclaredSarState semantics: SICNU_SAR_CALIBRATION and
    // SICNU_RADIOMETRIC_STATE both declared but disagreeing ⇒ conflict.
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar_conflict.tif";
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_CALIBRATION", "sigma0", "gdal:SICNU_SAR_CALIBRATION" );
    dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", "gamma0",
                          "gdal:SICNU_RADIOMETRIC_STATE" );

    const RemoteSensingAssetState state = resolveDataset( dataset ).state;
    REQUIRE( state.radiometric.unit.empty() );
    const ClaimRecord claim = claimFor( state, "radiometric.unit" );
    REQUIRE( claim.kind == ClaimKind::Conflicted );
    REQUIRE( claim.alternatives == std::vector<std::string>{ "gamma0", "sigma0" } );
    REQUIRE( !state.radiometric.declaredRaw.empty() );
}

TEST_CASE( "sar legacy-undeclared assumption is projected as an assumed claim",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar_legacy.tif";
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_STATE_ASSUMED", "sigma0_legacy_undeclared",
                          "gdal:SICNU_SAR_STATE_ASSUMED" );

    const RemoteSensingAssetState state = resolveDataset( dataset ).state;
    REQUIRE( state.radiometric.unit == "sigma0" );
    const ClaimRecord claim = claimFor( state, "radiometric.unit" );
    REQUIRE( claim.kind == ClaimKind::Assumed );
    REQUIRE( claim.note.find( "sigma0_legacy_undeclared" ) != std::string::npos );
}

TEST_CASE( "modality is inferred from sar keys when undeclared", "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar_no_modality.tif";
    dataset.metadata.add( "SICNU_SAR_CALIBRATION", "sigma0", "gdal:SICNU_SAR_CALIBRATION" );

    const RemoteSensingAssetState state = resolveDataset( dataset ).state;
    REQUIRE( state.sensor.modality == Modality::Sar );
    const ClaimRecord claim = claimFor( state, "sensor.modality" );
    REQUIRE( claim.kind == ClaimKind::Inferred );
}

TEST_CASE( "declarative sources agreeing merge their source lists", "[scientific_state][slice_b]" )
{
    DatasetFacts dataset = makeSentinel2Dataset();
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.structureBandCount = 3;
    for ( int i = 0; i < 3; ++i )
    {
        CatalogBandFacts band;
        band.index = i + 1;
        band.role = i == 0 ? "blue" : ( i == 1 ? "green" : "red" );
        catalog.bands.push_back( band );
    }

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const ClaimRecord claim = claimFor( state, "bands[1].role" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources.size() == 2 );
    REQUIRE( claim.sources[0] == "catalog:structure" );
    REQUIRE( claim.sources[1] == "gdal:SICNU_BAND_ROLE" );
}

TEST_CASE( "resolution is deterministic: same input, same serialized bytes",
           "[scientific_state][slice_b]" )
{
    StateResolutionInput input;
    input.dataset = makeSentinel2Dataset();
    CatalogFacts catalog;
    catalog.assetId = "x";
    input.catalog = catalog;

    const std::string first = serializeState( resolveAssetState( input ).state );
    const std::string second = serializeState( resolveAssetState( input ).state );
    REQUIRE( first == second );
}

TEST_CASE( "band projection is capped at kMaxPassportBands with an explicit note",
           "[scientific_state][slice_b]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/huge.tif";
    dataset.bandCount = kMaxPassportBands + 5;
    for ( int i = 0; i < dataset.bandCount; ++i )
    {
        BandFacts band;
        band.index = i + 1;
        dataset.bands.push_back( band );
    }

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.bands.size() == kMaxPassportBands );
    bool hasNote = false;
    for ( const ResolutionNote &note : state.notes )
        hasNote = hasNote || note.code == "bands.truncated";
    REQUIRE( hasNote );
}
