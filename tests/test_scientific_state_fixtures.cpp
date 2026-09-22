/***************************************************************************
  tests/test_scientific_state_fixtures.cpp
  RS14-01 Scientific Data Passport — Slice G: cross-asset fixture contracts.

  Five asset families as pure fact fixtures (no I/O): Landsat C2 L2,
  Sentinel-2 L2A, MODIS, SAR (declared + legacy-assumed variants) and a
  model-derived classification. Each family pins its full projected
  passport: radiometric vocabulary, band roles, claim kinds, notes and
  confidence ordering. Also: teaching-mode vs agent-mode consistency,
  deterministic replay for every family, and the DN→TOA calibration diff.
  (The GTiff end-to-end case lives in test_scientific_state_gdal.cpp.)
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "scientific_state/asset_state_diff.h"
#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/model_sidecar.h"
#include "scientific_state/teaching_view.h"

using namespace sicnu::state;

namespace
{

/// Landsat Collection-2 Level-2 surface reflectance: declared platform,
/// roles, wavelengths, scale (physical = stored / 1/REFLECTANCE_MULT).
StateResolutionInput landsatC2L2()
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/landsat/LC08_L2SP_043034_20240501_20240509_02_T2.tif";
    dataset.driverName = "GTiff";
    dataset.bandCount = 2;
    dataset.metadata.add( "SICNU_PLATFORM", "LANDSAT_8", "gdal:SICNU_PLATFORM" );
    dataset.metadata.add( "SICNU_SENSOR", "OLI_TIRS", "gdal:SICNU_SENSOR" );
    dataset.metadata.add( "SICNU_MODALITY", "optical", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_PRODUCT_ID", "LC08_L2SP_043034_20240501",
                          "gdal:SICNU_PRODUCT_ID" );
    dataset.metadata.add( "SICNU_PROCESSING_LEVEL", "L2SP", "gdal:SICNU_PROCESSING_LEVEL" );
    dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", "surface_reflectance",
                          "gdal:SICNU_RADIOMETRIC_STATE" );
    dataset.metadata.add( "SICNU_NUMERIC_SCALE", "36363.636363636363",
                          "gdal:SICNU_NUMERIC_SCALE" );
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T18:12:31Z",
                          "gdal:SICNU_ACQUISITION_DATE" );
    dataset.metadata.add( "CLOUDCOVER", "3.0", "gdal:CLOUDCOVER" );
    dataset.metadata.add( "SICNU_QA_VOCABULARY", "landsat_qa_pixel", "gdal:SICNU_QA_VOCABULARY" );

    BandFacts red;
    red.index = 1;
    red.name = "B4";
    red.dataType = "UInt16";
    red.metadata.add( "SICNU_BAND_ROLE", "red", "gdal:SICNU_BAND_ROLE" );
    red.metadata.add( "WAVELENGTH", "665", "gdal:WAVELENGTH" );
    red.metadata.add( "NO_DATA_VALUE", "0", "gdal:NO_DATA_VALUE" );
    BandFacts nir;
    nir.index = 2;
    nir.name = "B5";
    nir.dataType = "UInt16";
    nir.metadata.add( "SICNU_BAND_ROLE", "nir", "gdal:SICNU_BAND_ROLE" );
    nir.metadata.add( "WAVELENGTH", "865", "gdal:WAVELENGTH" );
    nir.metadata.add( "NO_DATA_VALUE", "0", "gdal:NO_DATA_VALUE" );
    dataset.bands = { red, nir };
    dataset.geometry.hasCrs = true;
    dataset.geometry.crsAuthid = "EPSG:32611";
    dataset.geometry.crsProjected = true;
    dataset.geometry.hasGeoTransform = true;
    dataset.geometry.geoTransform = { 249285.0, 30.0, 0.0, 4139835.0, 0.0, -30.0 };
    dataset.geometry.width = 7821;
    dataset.geometry.height = 7951;

    StateResolutionInput input;
    input.dataset = dataset;
    return input;
}

/// Sentinel-2 L2A: quantified BOA (scale 10000), band roles NOT declared in
/// the file — they come from the sensor profile as inferred claims.
StateResolutionInput sentinel2L2A()
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/s2/S2A_MSIL2A_20240501.tif";
    dataset.bandCount = 3;
    dataset.metadata.add( "SICNU_PLATFORM", "SENTINEL_2A", "gdal:SICNU_PLATFORM" );
    dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", "boa_reflectance",
                          "gdal:SICNU_RADIOMETRIC_STATE" );
    dataset.metadata.add( "SICNU_NUMERIC_SCALE", "10000", "gdal:SICNU_NUMERIC_SCALE" );
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z",
                          "gdal:SICNU_ACQUISITION_DATE" );
    for ( int i = 1; i <= 3; ++i )
    {
        BandFacts band;
        band.index = i;
        band.name = "B" + std::to_string( i + 1 );
        band.dataType = "UInt16";
        dataset.bands.push_back( band );
    }

    SensorProfileFacts profile;
    profile.sensorKey = "sentinel2_msi";
    profile.platform = "SENTINEL_2A";
    profile.instrument = "MSI";
    profile.modality = Modality::Optical;
    profile.productFamily = "sentinel2_l2a";
    profile.qaVocabulary = "s2_scl";
    const char *roles[3] = { "blue", "green", "red" };
    const double wavelengths[3] = { 492.0, 560.0, 665.0 };
    for ( int i = 0; i < 3; ++i )
    {
        ProfileBand band;
        band.index = i + 1;
        band.role = roles[i];
        band.hasWavelengthNm = true;
        band.wavelengthNm = wavelengths[i];
        profile.bands.push_back( band );
    }

    StateResolutionInput input;
    input.dataset = dataset;
    input.sensorProfile = profile;
    return input;
}

/// MODIS sinusoidal tile: QA band declared, thermal-style radiometric state.
StateResolutionInput modisTile()
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/modis/MOD11A2.A2024.h25v05.tif";
    dataset.bandCount = 2;
    dataset.metadata.add( "SICNU_PLATFORM", "TERRA", "gdal:SICNU_PLATFORM" );
    dataset.metadata.add( "SICNU_MODALITY", "thermal", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", "brightness_temperature",
                          "gdal:SICNU_RADIOMETRIC_STATE" );
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01", "gdal:SICNU_ACQUISITION_DATE" );
    BandFacts lst;
    lst.index = 1;
    lst.name = "LST_Day_1km";
    lst.dataType = "Int16";
    lst.metadata.add( "SICNU_BAND_ROLE", "thermal", "gdal:SICNU_BAND_ROLE" );
    BandFacts qa;
    qa.index = 2;
    qa.name = "QC_Day";
    qa.dataType = "UInt32";
    qa.metadata.add( "SICNU_BAND_ROLE", "qa", "gdal:SICNU_BAND_ROLE" );
    qa.metadata.add( "NO_DATA_VALUE", "0", "gdal:NO_DATA_VALUE" );
    dataset.bands = { lst, qa };

    StateResolutionInput input;
    input.dataset = dataset;
    return input;
}

/// SAR gamma0, linear power, fully declared.
StateResolutionInput sarDeclared()
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar/S1A_gamma0.tif";
    dataset.bandCount = 1;
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_CALIBRATION", "gamma0", "gdal:SICNU_SAR_CALIBRATION" );
    dataset.metadata.add( "SICNU_SAR_DOMAIN", "linear_power", "gdal:SICNU_SAR_DOMAIN" );
    BandFacts band;
    band.index = 1;
    band.name = "VV";
    band.dataType = "Float32";
    dataset.bands.push_back( band );

    StateResolutionInput input;
    input.dataset = dataset;
    return input;
}

/// SAR with only the legacy-undeclared assumption marker (issue-adjacent
/// state projected verbatim — never interpreted here).
StateResolutionInput sarLegacyAssumed()
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar/S1A_legacy.tif";
    dataset.bandCount = 1;
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_STATE_ASSUMED", "sigma0_legacy_undeclared",
                          "gdal:SICNU_SAR_STATE_ASSUMED" );
    BandFacts band;
    band.index = 1;
    band.name = "VV";
    band.dataType = "Float32";
    dataset.bands.push_back( band );
    StateResolutionInput input;
    input.dataset = dataset;
    return input;
}

/// Model-derived classification output: catalog identity + derivation record
/// + classifier sidecar facts.
StateResolutionInput modelDerived()
{
    CatalogFacts catalog;
    catalog.assetId = "1c2f8f36-2222-4f0e-9a31-52d4a1b9c777";
    catalog.revision = "1";
    catalog.displayName = "Postfire classification";
    catalog.kind = "raster";
    catalog.lifecycle = "ready";
    catalog.structureBandCount = 1;
    CatalogBandFacts band;
    band.index = 1;
    band.role = "scene_classification";
    band.dataType = "Byte";
    catalog.bands.push_back( band );

    DerivationFacts derivation;
    derivation.algorithmId = "rs:rf_classification";
    derivation.algorithmVersion = "1.3";
    DerivationFacts::Input trainingInput;
    trainingInput.assetId = "aaaaaaaa-1111-4f0e-9a31-52d4a1b9c777";
    trainingInput.revision = "1";
    trainingInput.valueDomain = "surface_reflectance";
    derivation.inputs.push_back( trainingInput );
    derivation.executionFingerprint = "fp-99";
    derivation.softwareVersion = "13.0.0";

    StateResolutionInput input;
    input.catalog = catalog;
    input.derivation = derivation;
    return input;
}

/// v2-shaped classifier sidecar text for the model-derived family.
std::string classifierSidecarText()
{
    return R"( {
        "version": 2,
        "method": "random_forest",
        "classes": [ { "id": 2, "color": "#00ff00" }, { "id": 1, "color": "#0000ff" } ],
        "bandIndices": [ 1, 2 ],
        "featureSchema": { "features": [ { "name": "ndvi" }, { "name": "brightness" } ] },
        "validation": { "overallAccuracy": 0.93 }
    } )";
}

std::size_t countKind( const std::vector<ClaimRecord> &claims, ClaimKind kind )
{
    std::size_t count = 0;
    for ( const ClaimRecord &claim : claims )
    {
        if ( claim.kind == kind )
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE( "Landsat C2 L2 passport pins the declared-surface-reflectance contract",
           "[scientific_state][slice_g]" )
{
    const RemoteSensingAssetState state = resolveAssetState( landsatC2L2() ).state;
    REQUIRE( state.radiometric.unit == "surface_reflectance" );
    REQUIRE( claimFor( state, "radiometric.unit" ).kind == ClaimKind::Known );
    REQUIRE( state.radiometric.hasNumericScale );
    REQUIRE( state.bands.size() == 2 );
    REQUIRE( state.bands[0].role == "red" );
    REQUIRE( state.bands[1].role == "nir" );
    REQUIRE( state.sensor.modality == Modality::Optical );
    REQUIRE( state.validity.qualityMaskInfo == "landsat_qa_pixel" );
    REQUIRE( state.acquisition.valid );
    // Fully declared: no assumption, no conflict.
    REQUIRE( state.assumptions.empty() );
    REQUIRE( countKind( state.claims, ClaimKind::Conflicted ) == 0 );
    REQUIRE( state.confidence == 1.0 );
}

TEST_CASE( "Sentinel-2 L2A passport pins the inferred-roles contract",
           "[scientific_state][slice_g]" )
{
    const RemoteSensingAssetState state = resolveAssetState( sentinel2L2A() ).state;
    // boa_reflectance and surface_reflectance are the same physical state.
    REQUIRE( state.radiometric.unit == "surface_reflectance" );
    REQUIRE( state.radiometric.numericScale == 10000.0 );
    REQUIRE( state.bands.size() == 3 );
    REQUIRE( state.bands[0].role == "blue" );
    REQUIRE( claimFor( state, "bands[1].role" ).kind == ClaimKind::Inferred );
    REQUIRE( state.bands[2].hasWavelengthNm );
    REQUIRE( state.validity.qualityMaskInfo == "s2_scl" );
    bool profileNote = false;
    for ( const ResolutionNote &note : state.notes )
        profileNote = profileNote || note.code == "band_role.from_sensor_profile";
    REQUIRE( profileNote );
}

TEST_CASE( "MODIS passport pins the QA-band and thermal contract",
           "[scientific_state][slice_g]" )
{
    const RemoteSensingAssetState state = resolveAssetState( modisTile() ).state;
    REQUIRE( state.radiometric.unit == "brightness_temperature" );
    REQUIRE( state.sensor.modality == Modality::Thermal );
    REQUIRE( state.bands[0].role == "thermal" );
    REQUIRE( state.bands[1].role == "qa" );
    // Day acquisition date (no time of day) still counts as valid acquisition.
    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2024-05-01" );
}

TEST_CASE( "SAR passports pin declared and legacy-assumed contracts verbatim",
           "[scientific_state][slice_g]" )
{
    const RemoteSensingAssetState declared = resolveAssetState( sarDeclared() ).state;
    REQUIRE( declared.radiometric.unit == "gamma0" );
    REQUIRE( declared.radiometric.domain == "linear_power" );
    REQUIRE( claimFor( declared, "radiometric.unit" ).kind == ClaimKind::Known );
    REQUIRE( declared.assumptions.empty() );

    const RemoteSensingAssetState legacy = resolveAssetState( sarLegacyAssumed() ).state;
    REQUIRE( legacy.radiometric.unit == "sigma0" );
    REQUIRE( claimFor( legacy, "radiometric.unit" ).kind == ClaimKind::Assumed );
    REQUIRE( countKind( legacy.claims, ClaimKind::Assumed ) >= 1 );
    // Confident ordering: a declared SAR state outranks a legacy assumption.
    REQUIRE( declared.confidence > legacy.confidence );
}

TEST_CASE( "model-derived passport pins the provenance + sidecar contract",
           "[scientific_state][slice_g]" )
{
    StateResolutionInput input = modelDerived();
    ModelSidecarFacts sidecar;
    AssetStateError error;
    REQUIRE( parseClassifierSidecarJson( classifierSidecarText(), "/models/rf.meta.json",
                                         sidecar, error ) );
    input.modelSidecar = sidecar;

    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    REQUIRE( state.provenance.isDerived );
    REQUIRE( state.provenance.algorithmId == "rs:rf_classification" );
    REQUIRE( state.modelDerived.present );
    REQUIRE( state.modelDerived.modelKind == "random_forest" );
    REQUIRE( state.modelDerived.labels == std::vector<std::string>{ "1", "2" } );
    REQUIRE( state.modelDerived.accuracy == 0.93 );
    REQUIRE( state.bands.size() == 1 );
    REQUIRE( state.bands[0].role == "scene_classification" );
    const ClaimRecord labels = claimFor( state, "model_derived.labels" );
    REQUIRE( labels.kind == ClaimKind::Known );
    REQUIRE( labels.sources.front() == "sidecar:classifier-meta" );
}

TEST_CASE( "teaching and agent modes carry identical evidence for every family",
           "[scientific_state][slice_g]" )
{
    const std::vector<StateResolutionInput> families = {
        landsatC2L2(), sentinel2L2A(), modisTile(), sarDeclared(), sarLegacyAssumed(),
        modelDerived()
    };
    for ( const StateResolutionInput &input : families )
    {
        const RemoteSensingAssetState state = resolveAssetState( input ).state;
        const TeachingSummary summary = renderTeachingSummary( state );
        // Every bucket of the teaching view is exactly the set of claims of
        // that kind — the two surfaces can never drift apart.
        REQUIRE( summary.known.size() == countKind( state.claims, ClaimKind::Known ) );
        REQUIRE( summary.inferred.size() == countKind( state.claims, ClaimKind::Inferred ) );
        REQUIRE( summary.assumed.size() == countKind( state.claims, ClaimKind::Assumed ) );
        REQUIRE( summary.conflicted.size() == countKind( state.claims, ClaimKind::Conflicted ) );
        REQUIRE( summary.missing.size() == countKind( state.claims, ClaimKind::Unknown ) );
    }
}

TEST_CASE( "every family replays deterministically byte-for-byte",
           "[scientific_state][slice_g]" )
{
    const std::vector<StateResolutionInput> families = {
        landsatC2L2(), sentinel2L2A(), modisTile(), sarDeclared(), sarLegacyAssumed(),
        modelDerived()
    };
    for ( const StateResolutionInput &input : families )
    {
        const std::string first = serializeState( resolveAssetState( input ).state );
        const std::string second = serializeState( resolveAssetState( input ).state );
        REQUIRE( first == second );
        // Round-trip through the schema keeps the bytes stable.
        RemoteSensingAssetState decoded;
        AssetStateError error;
        REQUIRE( assetStateFromJson( first, decoded, error ) );
        REQUIRE( serializeState( decoded ) == first );
    }
}

TEST_CASE( "DN to surface-reflectance calibration diff changes only radiometric fields",
           "[scientific_state][slice_g]" )
{
    // A pre-calibration variant of the SAME scene: identical in every
    // respect except the radiometric declaration (DN instead of SR).
    DatasetFacts dn = landsatC2L2().dataset.value();
    MetadataItems stripped;
    for ( const auto &item : dn.metadata.all() )
    {
        if ( item.first != "SICNU_RADIOMETRIC_STATE" && item.first != "SICNU_NUMERIC_SCALE" )
            stripped.add( item.first, item.second.value, item.second.source );
    }
    stripped.add( "SICNU_RADIOMETRIC_STATE", "digital_number", "gdal:SICNU_RADIOMETRIC_STATE" );
    dn.metadata = stripped;
    StateResolutionInput raw;
    raw.dataset = dn;
    const RemoteSensingAssetState before = resolveAssetState( raw ).state;
    const RemoteSensingAssetState after = resolveAssetState( landsatC2L2() ).state;

    const StateDiff diff = diffStates( before, after );
    REQUIRE( !diff.empty() );
    bool radiometricChanged = false;
    bool geometryTouched = false;
    for ( const FieldDiff &field : diff.diffs )
    {
        radiometricChanged =
            radiometricChanged || field.path == "radiometric.unit" && field.kind == "changed";
        geometryTouched = geometryTouched || field.path.rfind( "geometry.", 0 ) == 0;
    }
    REQUIRE( radiometricChanged );
    REQUIRE( geometryTouched == false );
    REQUIRE( diff.diffs.front().path <= diff.diffs.back().path );

    // Diff document round-trips and is deterministic.
    const std::string bytes = serializeStateDiff( diff );
    Json::Value diffDoc;
    Json::CharReaderBuilder builder;
    builder[ "stackLimit" ] = 128;
    Json::CharReader *reader = builder.newCharReader();
    const bool parsed = reader->parse( bytes.data(), bytes.data() + bytes.size(), &diffDoc,
                                       nullptr );
    delete reader;
    REQUIRE( parsed );
    StateDiff decoded;
    AssetStateError error;
    REQUIRE( stateDiffFromJson( diffDoc, decoded, error ) );
    REQUIRE( serializeStateDiff( decoded ) == bytes );
}

TEST_CASE( "a fully declared family outranks one needing the FSM default",
           "[scientific_state][slice_g]" )
{
    const RemoteSensingAssetState declared = resolveAssetState( landsatC2L2() ).state;
    StateResolutionInput raw = landsatC2L2();
    DatasetFacts bare = raw.dataset.value();
    MetadataItems stripped;
    for ( const auto &item : bare.metadata.all() )
    {
        if ( item.first != "SICNU_RADIOMETRIC_STATE" )
            stripped.add( item.first, item.second.value, item.second.source );
    }
    bare.metadata = stripped;
    raw.dataset = bare;
    const RemoteSensingAssetState assumed = resolveAssetState( raw ).state;

    REQUIRE( assumed.radiometric.unit == "digital_number" );
    REQUIRE( assumed.confidence < declared.confidence );
}
