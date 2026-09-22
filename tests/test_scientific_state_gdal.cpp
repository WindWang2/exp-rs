/***************************************************************************
  tests/test_scientific_state_gdal.cpp
  RS14-01 Scientific Data Passport — Slice F2: GDAL facts collector.

  Lane: sicnu_add_io_test (GDAL, still Qt-free). Fixtures are synthesized at
  runtime (repo convention: no committed geodata). The collector performs ONE
  read-only open and metadata queries only — never a pixel scan.
 ***************************************************************************/

#include "scientific_state/asset_state_resolver.h"
#include "scientific_state/gdal/gdal_state_facts.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>

#include <filesystem>
#include <mutex>
#include <string>

using namespace sicnu::state;

namespace fs = std::filesystem;

namespace
{

void ensureGdal()
{
    static std::once_flag once;
    std::call_once( once, [] { GDALAllRegister(); } );
}

/// Writes a small 2-band Byte GeoTIFF with SICNU_* dataset + band metadata,
/// UTM CRS and geotransform — a Landsat-like registered product.
std::string writeDeclaredGeoTiff( const std::string &dir )
{
    ensureGdal();
    const std::string path = ( fs::path( dir ) / "declared.tif" ).string();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 3, 2, GDT_Byte, nullptr );
    REQUIRE( dataset );

    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    REQUIRE( OSRImportFromEPSG( srs, 32650 ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
    REQUIRE( GDALSetProjection( dataset, wkt ) == CE_None );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );

    double geotransform[6] = { 499980.0, 30.0, 0.0, 4800000.0, 0.0, -30.0 };
    REQUIRE( GDALSetGeoTransform( dataset, geotransform ) == CE_None );

    GDALSetMetadataItem( dataset, "SICNU_SENSOR", "OLI_TIRS", nullptr );
    GDALSetMetadataItem( dataset, "SICNU_PLATFORM", "LANDSAT_8", nullptr );
    GDALSetMetadataItem( dataset, "SICNU_MODALITY", "optical", nullptr );
    GDALSetMetadataItem( dataset, "SICNU_RADIOMETRIC_STATE", "surface_reflectance", nullptr );
    GDALSetMetadataItem( dataset, "SICNU_NUMERIC_SCALE", "10000", nullptr );
    GDALSetMetadataItem( dataset, "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z", nullptr );
    GDALSetMetadataItem( dataset, "CLOUDCOVER", "12.0", nullptr );

    struct BandSpec
    {
        const char *name;
        const char *role;
        const char *wavelength;
    };
    const BandSpec specs[2] = { { "B4", "red", "665" }, { "B5", "nir", "865" } };
    for ( int i = 1; i <= 2; ++i )
    {
        GDALRasterBandH band = GDALGetRasterBand( dataset, i );
        REQUIRE( band );
        GDALSetDescription( band, specs[i - 1].name );
        GDALSetMetadataItem( band, "SICNU_BAND_ROLE", specs[i - 1].role, nullptr );
        GDALSetMetadataItem( band, "WAVELENGTH", specs[i - 1].wavelength, nullptr );
        GDALSetMetadataItem( band, "WAVELENGTH_UNITS", "nm", nullptr );
        REQUIRE( GDALSetRasterNoDataValue( band, 0.0 ) == CE_None );
    }

    GDALClose( dataset );
    return path;
}

/// Writes a bare GeoTIFF: no SICNU_* keys, no CRS, no geotransform.
std::string writeBareGeoTiff( const std::string &dir )
{
    ensureGdal();
    const std::string path = ( fs::path( dir ) / "bare.tif" ).string();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 2, 2, 1, GDT_Float32, nullptr );
    REQUIRE( dataset );
    GDALClose( dataset );
    return path;
}

} // namespace

TEST_CASE( "collector projects declared GDAL metadata into facts and state",
           "[scientific_state][slice_f2]" )
{
    ensureGdal();
    const std::string path =
        writeDeclaredGeoTiff( CMAKE_SOURCE_DIR + std::string( "/build-rs14-passport" ) );

    std::string error;
    std::optional<DatasetFacts> facts = collectDatasetFacts( path, error );
    REQUIRE( facts.has_value() );
    REQUIRE( error.empty() );
    REQUIRE( facts->sourcePath == path );
    REQUIRE( facts->driverName == "GTiff" );
    REQUIRE( facts->bandCount == 2 );
    REQUIRE( facts->bands.size() == 2 );
    REQUIRE( facts->bands[0].name == "B4" );
    REQUIRE( facts->bands[0].dataType == "Byte" );
    REQUIRE( facts->bands[0].metadata.find( "SICNU_BAND_ROLE" ).size() == 1 );
    REQUIRE( facts->bands[0].metadata.find( "NO_DATA_VALUE" ).size() == 1 );
    REQUIRE( facts->geometry.hasCrs );
    REQUIRE( facts->geometry.crsAuthid == "EPSG:32650" );
    REQUIRE( facts->geometry.crsProjected );
    REQUIRE( facts->geometry.hasGeoTransform );
    REQUIRE( facts->geometry.width == 4 );
    REQUIRE( facts->geometry.height == 3 );

    StateResolutionInput input;
    input.dataset = *facts;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands.size() == 2 );
    REQUIRE( state.bands[0].role == "red" );
    REQUIRE( state.bands[1].role == "nir" );
    REQUIRE( state.bands[1].hasWavelengthNm );
    REQUIRE( state.bands[1].wavelengthNm == 865.0 );
    REQUIRE( state.radiometric.unit == "surface_reflectance" );
    REQUIRE( state.radiometric.numericScale == 10000.0 );
    REQUIRE( state.sensor.modality == Modality::Optical );
    REQUIRE( state.sensor.platform == "LANDSAT_8" );
    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2024-05-01T10:12:31Z" );
    REQUIRE( state.geometry.hasCrs );
    REQUIRE( state.geometry.crsAuthid == "EPSG:32650" );
    REQUIRE( state.geometry.hasPixelSize );
    REQUIRE( state.geometry.hasExtent );
    REQUIRE( state.validity.hasCloudCover );
    REQUIRE( state.validity.cloudCoverPercent == 12.0 );
    // GDAL-declared per-band nodata feeds the policy census.
    REQUIRE( state.validity.noDataPolicy == "declared" );
    REQUIRE( claimFor( state, "radiometric.unit" ).kind == ClaimKind::Known );
}

TEST_CASE( "bare dataset projects honest unknowns and the FSM default",
           "[scientific_state][slice_f2]" )
{
    ensureGdal();
    const std::string path =
        writeBareGeoTiff( CMAKE_SOURCE_DIR + std::string( "/build-rs14-passport" ) );

    std::string error;
    std::optional<DatasetFacts> facts = collectDatasetFacts( path, error );
    REQUIRE( facts.has_value() );
    REQUIRE( facts->bandCount == 1 );
    REQUIRE( !facts->geometry.hasCrs );
    REQUIRE( !facts->geometry.hasGeoTransform );

    StateResolutionInput input;
    input.dataset = *facts;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.geometry.crsAuthid.empty() );
    REQUIRE( !state.geometry.hasPixelSize );
    REQUIRE( state.radiometric.unit == "digital_number" );
    REQUIRE( claimFor( state, "radiometric.unit" ).kind == ClaimKind::Assumed );
    REQUIRE( claimFor( state, "sensor.modality" ).kind == ClaimKind::Unknown );
}

TEST_CASE( "collector refuses missing files with a typed error",
           "[scientific_state][slice_f2]" )
{
    ensureGdal();
    std::string error;
    const std::optional<DatasetFacts> facts =
        collectDatasetFacts( "/definitely/not/here.tif", error );
    REQUIRE( !facts.has_value() );
    REQUIRE( !error.empty() );
}

TEST_CASE( "metadata cap drops are counted, never silent",
           "[scientific_state][review2][p2_cap]" )
{
    // RED on master: the collector loop bounded SCANNED entries at the cap,
    // so items beyond it never reached MetadataItems and dropped() stayed 0
    // — exactly the >cap files where "truncation is never silent" matters.
    ensureGdal();
    const std::string dir = CMAKE_SOURCE_DIR + std::string( "/build-rs14-passport" );
    std::filesystem::create_directories( dir );
    const std::string path = ( fs::path( dir ) / "capped.tif" ).string();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 2, 2, 1, GDT_Byte, nullptr );
    REQUIRE( dataset );
    for ( int i = 0; i < 600; ++i )
        GDALSetMetadataItem( dataset, ( "FILLER_" + std::to_string( i ) ).c_str(),
                             std::to_string( i ).c_str(), nullptr );
    GDALClose( dataset );

    std::string error;
    const std::optional<DatasetFacts> facts = collectDatasetFacts( path, error );
    REQUIRE( facts.has_value() );
    REQUIRE( facts->metadata.all().size() == kMaxCollectedMetadataItems );
    REQUIRE( facts->droppedMetadataItems == 600 - kMaxCollectedMetadataItems );

    StateResolutionInput input;
    input.dataset = *facts;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;
    bool truncated = false;
    for ( const ResolutionNote &note : state.notes )
        truncated = truncated || note.code == "facts.metadata_truncated";
    REQUIRE( truncated );
}
