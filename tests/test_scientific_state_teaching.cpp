/***************************************************************************
  tests/test_scientific_state_teaching.cpp
  RS14-01 Scientific Data Passport — Slice F1: teaching view-model.

  The teaching view is the student-facing rendering of a passport: it answers
  "what is this asset, what do we know, what was inferred, what was assumed,
  what is missing, what contradicts itself" — with EXACTLY the same evidence
  sets the machine-readable JSON carries (agent-mode/teaching-mode semantic
  consistency is asserted here).
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/teaching_view.h"

using namespace sicnu::state;

namespace
{

DatasetFacts makeLandsatLikeDataset()
{
    DatasetFacts facts;
    facts.sourcePath = "/data/landsat/LC08_L2SP_20240501.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 2;

    facts.metadata.add( "SICNU_SENSOR", "OLI_TIRS", "gdal:SICNU_SENSOR" );
    facts.metadata.add( "SICNU_PLATFORM", "LANDSAT_8", "gdal:SICNU_PLATFORM" );
    facts.metadata.add( "SICNU_PRODUCT_ID", "LC08_L2SP_20240501", "gdal:SICNU_PRODUCT_ID" );
    facts.metadata.add( "SICNU_PROCESSING_LEVEL", "L2SP", "gdal:SICNU_PROCESSING_LEVEL" );
    facts.metadata.add( "SICNU_RADIOMETRIC_STATE", "surface_reflectance",
                        "gdal:SICNU_RADIOMETRIC_STATE" );
    facts.metadata.add( "SICNU_NUMERIC_SCALE", "10000", "gdal:SICNU_NUMERIC_SCALE" );
    facts.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z",
                        "gdal:SICNU_ACQUISITION_DATE" );
    facts.metadata.add( "CLOUDCOVER", "12.0", "gdal:CLOUDCOVER" );

    BandFacts red;
    red.index = 1;
    red.name = "B4";
    red.dataType = "UInt16";
    red.metadata.add( "SICNU_BAND_ROLE", "red", "gdal:SICNU_BAND_ROLE" );
    red.metadata.add( "WAVELENGTH", "665", "gdal:WAVELENGTH" );
    facts.bands.push_back( red );

    BandFacts nir;
    nir.index = 2;
    nir.name = "B5";
    nir.dataType = "UInt16";
    nir.metadata.add( "SICNU_BAND_ROLE", "nir", "gdal:SICNU_BAND_ROLE" );
    nir.metadata.add( "WAVELENGTH", "865", "gdal:WAVELENGTH" );
    facts.bands.push_back( nir );

    facts.geometry.hasCrs = true;
    facts.geometry.crsAuthid = "EPSG:32650";
    facts.geometry.crsProjected = true;
    facts.geometry.hasGeoTransform = true;
    facts.geometry.geoTransform = { 499980.0, 30.0, 0.0, 4800000.0, 0.0, -30.0 };
    facts.geometry.width = 7681;
    facts.geometry.height = 7791;
    return facts;
}

} // namespace

TEST_CASE( "teaching summary splits claims into the five evidence buckets",
           "[scientific_state][slice_f]" )
{
    StateResolutionInput input;
    input.dataset = makeLandsatLikeDataset();
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const TeachingSummary summary = renderTeachingSummary( state );

    // Declared facts the student can trust.
    bool radiometricKnown = false;
    for ( const std::string &line : summary.known )
        radiometricKnown = radiometricKnown || line.find( "radiometric.unit" ) != std::string::npos;
    REQUIRE( radiometricKnown );

    // Nothing here is conflicted or assumed. The only legitimate inferences
    // are geometry derivations (pixel size / extent from the geotransform)
    // and the noData policy census of bands without declarations.
    REQUIRE( summary.conflicted.empty() );
    REQUIRE( summary.assumed.empty() );
    for ( const std::string &line : summary.inferred )
    {
        const bool geometryDerived = line.find( "geometry." ) == 0;
        const bool noDataCensus = line.find( "validity.no_data_policy" ) == 0;
        REQUIRE( ( geometryDerived || noDataCensus ) );
    }

    // Modality is not declared anywhere in this fixture: the student must see
    // it as missing, matching the machine-readable unknowns list.
    bool modalityMissing = false;
    for ( const std::string &line : summary.missing )
        modalityMissing = modalityMissing || line.find( "sensor.modality" ) != std::string::npos;
    REQUIRE( modalityMissing );
}

TEST_CASE( "teaching and agent modes carry identical evidence sets",
           "[scientific_state][slice_f]" )
{
    StateResolutionInput input;
    input.dataset = makeLandsatLikeDataset();
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const TeachingSummary summary = renderTeachingSummary( state );

    std::size_t knownPaths = 0;
    std::size_t missingPaths = 0;
    for ( const std::string &line : summary.known )
        knownPaths += line.find( "radiometric.unit" ) != std::string::npos ? 1 : 0;
    for ( const std::string &line : summary.missing )
        missingPaths += line.find( "sensor.modality" ) != std::string::npos ? 1 : 0;

    // The same facts must be present in the serialized passport.
    const Json::Value doc = assetStateToJson( state );
    REQUIRE( doc["claims"].isArray() );
    bool radiometricKnownInJson = false;
    bool modalityUnknownInJson = false;
    for ( const Json::Value &claim : doc["claims"] )
    {
        if ( claim["path"].asString() == "radiometric.unit" &&
             claim["kind"].asString() == "known" )
            radiometricKnownInJson = true;
        if ( claim["path"].asString() == "sensor.modality" &&
             claim["kind"].asString() == "unknown" )
            modalityUnknownInJson = true;
    }
    REQUIRE( radiometricKnownInJson == ( knownPaths == 1 ) );
    REQUIRE( modalityUnknownInJson == ( missingPaths == 1 ) );
}

TEST_CASE( "assumed and inferred facts appear in their own buckets",
           "[scientific_state][slice_f]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/raw/scene.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    dataset.bands.push_back( band );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const TeachingSummary summary = renderTeachingSummary( state );

    bool radiometricAssumed = false;
    for ( const std::string &line : summary.assumed )
        radiometricAssumed = radiometricAssumed || line.find( "radiometric.unit" ) != std::string::npos;
    REQUIRE( radiometricAssumed );

    bool roleMissing = false;
    for ( const std::string &line : summary.missing )
        roleMissing = roleMissing || line.find( "bands[1].role" ) != std::string::npos;
    REQUIRE( roleMissing );
}

TEST_CASE( "inferred claims are rendered as inferred, never as known",
           "[scientific_state][slice_f]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/s2/stacked.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    dataset.bands.push_back( band );

    SensorProfileFacts profile;
    profile.sensorKey = "sentinel2_msi";
    ProfileBand blue;
    blue.index = 1;
    blue.role = "blue";
    profile.bands = { blue };

    StateResolutionInput input;
    input.dataset = dataset;
    input.sensorProfile = profile;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const TeachingSummary summary = renderTeachingSummary( state );
    bool roleInferred = false;
    for ( const std::string &line : summary.inferred )
        roleInferred = roleInferred || line.find( "bands[1].role" ) != std::string::npos;
    REQUIRE( roleInferred );

    bool roleNotKnown = true;
    for ( const std::string &line : summary.known )
        roleNotKnown = roleNotKnown && line.find( "bands[1].role" ) == std::string::npos;
    REQUIRE( roleNotKnown );
}

TEST_CASE( "conflicted claims surface verbatim with alternatives",
           "[scientific_state][slice_f]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/sar_conflict.tif";
    dataset.bandCount = 1;
    BandFacts band;
    band.index = 1;
    dataset.bands.push_back( band );
    dataset.metadata.add( "SICNU_MODALITY", "sar", "gdal:SICNU_MODALITY" );
    dataset.metadata.add( "SICNU_SAR_CALIBRATION", "sigma0", "gdal:SICNU_SAR_CALIBRATION" );
    dataset.metadata.add( "SICNU_RADIOMETRIC_STATE", "gamma0", "gdal:SICNU_RADIOMETRIC_STATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const TeachingSummary summary = renderTeachingSummary( state );
    bool conflicted = false;
    for ( const std::string &line : summary.conflicted )
        conflicted = conflicted || line.find( "radiometric.unit" ) != std::string::npos;
    REQUIRE( conflicted );
}

TEST_CASE( "teaching lines carry the resolved value for mapped claim paths",
           "[scientific_state][slice_f]" )
{
    StateResolutionInput input;
    input.dataset = makeLandsatLikeDataset();
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const TeachingSummary summary = renderTeachingSummary( state );

    // Claim paths and document paths differ (band index base, logical field
    // names). The rendered line must still carry the VALUE, never a false
    // "(not resolved)" for a fact the passport knows.
    const auto lineFor = []( const std::vector<std::string> &lines, const char *path )
    {
        for ( const std::string &line : lines )
        {
            if ( line.find( path ) == 0 )
                return line;
        }
        return std::string();
    };

    REQUIRE( lineFor( summary.known, "radiometric.unit" ).find( "surface_reflectance" ) !=
             std::string::npos );
    REQUIRE( lineFor( summary.known, "acquisition.time" ).find( "2024-05-01T10:12:31Z" ) !=
             std::string::npos );
    REQUIRE( lineFor( summary.known, "bands[2].role" ).find( "nir" ) != std::string::npos );
    REQUIRE( lineFor( summary.known, "geometry.crs" ).find( "EPSG:32650" ) !=
             std::string::npos );
    REQUIRE( lineFor( summary.inferred, "geometry.pixel_size" ).find( "30.0 x 30.0" ) !=
             std::string::npos );
    REQUIRE( lineFor( summary.known, "validity.cloud_cover" ).find( "12" ) !=
             std::string::npos );
    // A genuinely unresolvable field stays honest.
    REQUIRE( lineFor( summary.missing, "sensor.instrument" ).find( "(not resolved)" ) !=
             std::string::npos );
}

TEST_CASE( "plain text rendering has stable section headers", "[scientific_state][slice_f]" )
{
    StateResolutionInput input;
    input.dataset = makeLandsatLikeDataset();
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const std::string text = teachingSummaryToPlainText( renderTeachingSummary( state ) );

    REQUIRE( text.find( "Asset:" ) != std::string::npos );
    REQUIRE( text.find( "Known (declared)" ) != std::string::npos );
    REQUIRE( text.find( "Missing / unknown" ) != std::string::npos );
    // Deterministic bytes for the same state.
    REQUIRE( text == teachingSummaryToPlainText( renderTeachingSummary( state ) ) );
}

TEST_CASE( "a passport resolved from nothing renders an honest all-missing summary",
           "[scientific_state][slice_f]" )
{
    StateResolutionInput input;  // no facts at all
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    const TeachingSummary summary = renderTeachingSummary( state );
    REQUIRE( summary.known.empty() );
    REQUIRE( summary.inferred.empty() );
    REQUIRE( summary.assumed.empty() );
    REQUIRE( summary.conflicted.empty() );
    // Modality is "always applicable": the student must see it as missing.
    bool modalityMissing = false;
    for ( const std::string &line : summary.missing )
        modalityMissing = modalityMissing || line.find( "sensor.modality" ) != std::string::npos;
    REQUIRE( modalityMissing );
}
