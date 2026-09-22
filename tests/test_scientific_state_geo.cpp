/***************************************************************************
  tests/test_scientific_state_geo.cpp
  RS14-01 Scientific Data Passport — Slice C: geometry / validity / temporal
  projection.

  Lane: sicnu_add_sdk_test (Catch2 + jsoncpp only). Pins the resolver's
  geometry (CRS / geotransform / pixel size / extent), validity (noData
  policy, cloud cover, QA vocabulary) and temporal-ref projection, including
  the evidence contract for each: derived pixel size and extent are
  *inferred* (never silently), absent CRS is a *typed unknown*, conflicting
  noData declarations are *conflicted*.

  Convention pinned here for the Slice F GDAL collector: a band's native
  NoData value is handed over as band metadata item "NO_DATA_VALUE"
  (mirrors GDAL GetNoDataValue); catalog structure mirrors use
  CatalogBandFacts::hasNoData.
 ***************************************************************************/

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scientific_state/asset_state_json.h"
#include "scientific_state/asset_state_resolver.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::state;

namespace
{

bool hasNote( const RemoteSensingAssetState &state, const std::string &code )
{
    for ( const ResolutionNote &note : state.notes )
    {
        if ( note.code == code )
            return true;
    }
    return false;
}

bool unknownsContain( const RemoteSensingAssetState &state, const std::string &path )
{
    return std::find( state.unknowns.begin(), state.unknowns.end(), path ) != state.unknowns.end();
}

/// One-band dataset shell (geometry/validity facts are added per test).
DatasetFacts makeOneBandDataset()
{
    DatasetFacts facts;
    facts.sourcePath = "/data/geo.tif";
    facts.driverName = "GTiff";
    facts.bandCount = 1;
    BandFacts band;
    band.index = 1;
    band.dataType = "Byte";
    facts.bands.push_back( band );
    return facts;
}

} // namespace

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

TEST_CASE( "dataset CRS facts project into the geometry section as a known claim",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.hasCrs = true;
    dataset.geometry.crsWkt = "PROJCS[\"WGS 84 / UTM zone 31N\"]";
    dataset.geometry.crsAuthid = "EPSG:32631";
    dataset.geometry.crsProjected = true;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.geometry.hasCrs );
    REQUIRE( state.geometry.crsWkt == "PROJCS[\"WGS 84 / UTM zone 31N\"]" );
    REQUIRE( state.geometry.crsAuthid == "EPSG:32631" );
    REQUIRE( state.geometry.crsProjected );
    REQUIRE( !state.geometry.crsGeographic );

    const ClaimRecord claim = claimFor( state, "geometry.crs" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "gdal:CRS" } );
}

TEST_CASE( "dataset without CRS records a typed unknown, never a guess",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.geometry.hasCrs );
    REQUIRE( claimFor( state, "geometry.crs" ).kind == ClaimKind::Unknown );
    REQUIRE( unknownsContain( state, "geometry.crs" ) );
}

TEST_CASE( "north-up geotransform derives pixel size and extent as inferred claims",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.hasGeoTransform = true;
    dataset.geometry.geoTransform = { 440720.0, 60.0, 0.0, 3751320.0, 0.0, -60.0 };
    dataset.geometry.width = 2;
    dataset.geometry.height = 2;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.geometry.hasGeoTransform );
    REQUIRE( state.geometry.hasPixelSize );
    REQUIRE( state.geometry.pixelSizeX == 60.0 );
    REQUIRE( state.geometry.pixelSizeY == 60.0 );
    REQUIRE( state.geometry.hasSize );
    REQUIRE( state.geometry.width == 2 );
    REQUIRE( state.geometry.height == 2 );
    REQUIRE( state.geometry.hasExtent );
    REQUIRE( state.geometry.minX == 440720.0 );
    REQUIRE( state.geometry.maxX == 440840.0 );
    REQUIRE( state.geometry.minY == 3751200.0 );
    REQUIRE( state.geometry.maxY == 3751320.0 );

    const ClaimRecord pixelClaim = claimFor( state, "geometry.pixel_size" );
    REQUIRE( pixelClaim.kind == ClaimKind::Inferred );
    const ClaimRecord extentClaim = claimFor( state, "geometry.extent" );
    REQUIRE( extentClaim.kind == ClaimKind::Inferred );
    REQUIRE( hasNote( state, "geometry.pixel_size_from_geotransform" ) );
    REQUIRE( hasNote( state, "geometry.extent_from_geotransform" ) );
}

TEST_CASE( "non-finite geotransform yields typed unknowns and a self-readable document",
           "[scientific_state][review2][p1_nonfinite]" )
{
    // RED on master: a NaN axis scale flowed through as pixel_size = NaN,
    // which serializes to JSON null — a document this module's own readers
    // reject (InvalidField). The fix grounds nothing and records the gap.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.hasGeoTransform = true;
    dataset.geometry.geoTransform = { 440720.0, std::numeric_limits<double>::quiet_NaN(),
                                      0.0, 3751320.0, 0.0, -60.0 };
    dataset.geometry.width = 2;
    dataset.geometry.height = 2;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.geometry.hasGeoTransform );
    REQUIRE( !state.geometry.hasPixelSize );
    REQUIRE( claimFor( state, "geometry.pixel_size" ).kind == ClaimKind::Unknown );
    REQUIRE( unknownsContain( state, "geometry.pixel_size" ) );
    REQUIRE( claimFor( state, "geometry.extent" ).kind == ClaimKind::Unknown );
    REQUIRE( unknownsContain( state, "geometry.extent" ) );
    REQUIRE( hasNote( state, "geometry.geotransform_not_finite" ) );

    // The passport must survive its own round trip.
    RemoteSensingAssetState decoded;
    AssetStateError error;
    REQUIRE( assetStateFromJson( serializeState( state ), decoded, error ) );
    REQUIRE( error.code == StateErrorCode::None );
}

TEST_CASE( "rotated geotransform yields the four-corner bounding box, not a normalized extent",
           "[scientific_state][slice_c]" )
{
    // gt = (1000, 10, 5, 2000, 3, -10), 10 x 20 pixels.
    // Corners: (1000,2000) (1100,2030) (1100,1800) (1200,1830).
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.hasGeoTransform = true;
    dataset.geometry.geoTransform = { 1000.0, 10.0, 5.0, 2000.0, 3.0, -10.0 };
    dataset.geometry.width = 10;
    dataset.geometry.height = 20;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.geometry.hasExtent );
    REQUIRE( state.geometry.minX == 1000.0 );
    REQUIRE( state.geometry.maxX == 1200.0 );
    REQUIRE( state.geometry.minY == 1800.0 );
    REQUIRE( state.geometry.maxY == 2030.0 );
    // Pixel size stays the axis-aligned |gt1|,|gt5| projection (10, 10).
    REQUIRE( state.geometry.pixelSizeX == 10.0 );
    REQUIRE( state.geometry.pixelSizeY == 10.0 );
}

TEST_CASE( "missing geotransform leaves pixel size and extent unprojected",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.width = 4;
    dataset.geometry.height = 3;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.geometry.hasGeoTransform );
    REQUIRE( !state.geometry.hasPixelSize );
    REQUIRE( !state.geometry.hasExtent );
    REQUIRE( state.geometry.hasSize );
    REQUIRE( state.geometry.width == 4 );
    REQUIRE( state.geometry.height == 3 );
}

TEST_CASE( "size facts alone do not fabricate an extent", "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.geometry.hasGeoTransform = true;
    dataset.geometry.geoTransform = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    // width/height left at 0 → no size facts.

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.geometry.hasPixelSize );
    REQUIRE( !state.geometry.hasSize );
    REQUIRE( !state.geometry.hasExtent );
}

// ---------------------------------------------------------------------------
// Validity — noData policy
// ---------------------------------------------------------------------------

TEST_CASE( "catalog noData declarations set the band state and a known claim",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.structureBandCount = 1;
    CatalogBandFacts catalogBand;
    catalogBand.index = 1;
    catalogBand.hasNoData = true;
    catalogBand.noDataValue = -9999.0;
    catalog.bands.push_back( catalogBand );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands[0].hasNoData );
    REQUIRE( state.bands[0].noDataValue == -9999.0 );
    const ClaimRecord claim = claimFor( state, "bands[1].no_data" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "catalog:structure" } );
    REQUIRE( state.validity.noDataPolicy == "declared" );
}

TEST_CASE( "dataset band NO_DATA_VALUE declarations count toward the policy",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.bands[0].metadata.add( "NO_DATA_VALUE", "0", "gdal:NO_DATA_VALUE" );
    BandFacts second;
    second.index = 2;
    second.dataType = "Byte";
    dataset.bands.push_back( second );
    dataset.bandCount = 2;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.bands[0].hasNoData );
    REQUIRE( state.bands[0].noDataValue == 0.0 );
    REQUIRE( claimFor( state, "bands[1].no_data" ).kind == ClaimKind::Known );
    REQUIRE( state.validity.noDataPolicy == "partial" );
}

TEST_CASE( "bands without any noData declaration project an undeclared policy",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.bands[0].hasNoData );
    REQUIRE( state.validity.noDataPolicy == "undeclared" );
    REQUIRE( claimFor( state, "validity.no_data_policy" ).kind == ClaimKind::Inferred );
}

TEST_CASE( "conflicting noData declarations are conflicted, never resolved silently",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.bands[0].metadata.add( "NO_DATA_VALUE", "0", "gdal:NO_DATA_VALUE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.structureBandCount = 1;
    CatalogBandFacts catalogBand;
    catalogBand.index = 1;
    catalogBand.hasNoData = true;
    catalogBand.noDataValue = -9999.0;
    catalog.bands.push_back( catalogBand );

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    const ClaimRecord claim = claimFor( state, "bands[1].no_data" );
    REQUIRE( claim.kind == ClaimKind::Conflicted );
    REQUIRE( claim.alternatives.size() == 2 );
    REQUIRE( !state.bands[0].hasNoData );
}

TEST_CASE( "zero bands leave the noData policy unset", "[scientific_state][slice_c]" )
{
    DatasetFacts dataset;
    dataset.sourcePath = "/data/empty.tif";
    dataset.bandCount = 0;

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.validity.noDataPolicy.empty() );
    REQUIRE( claimFor( state, "validity.no_data_policy" ).kind == ClaimKind::Unknown );
}

TEST_CASE( "unparsable band NO_DATA_VALUE stays a typed unknown", "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.bands[0].metadata.add( "NO_DATA_VALUE", "none", "gdal:NO_DATA_VALUE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.bands[0].hasNoData );
    REQUIRE( claimFor( state, "bands[1].no_data" ).kind == ClaimKind::Unknown );
    REQUIRE( hasNote( state, "validity.nodata_invalid" ) );
}

// ---------------------------------------------------------------------------
// Validity — cloud cover
// ---------------------------------------------------------------------------

TEST_CASE( "numeric CLOUDCOVER metadata projects the cloud cover as a known claim",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "CLOUDCOVER", "12.5", "gdal:CLOUDCOVER" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.validity.hasCloudCover );
    REQUIRE( state.validity.cloudCoverPercent == 12.5 );
    const ClaimRecord claim = claimFor( state, "validity.cloud_cover" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "gdal:CLOUDCOVER" } );
}

TEST_CASE( "missing CLOUDCOVER key leaves cloud cover unprojected", "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.validity.hasCloudCover );
    REQUIRE( claimFor( state, "validity.cloud_cover" ).kind == ClaimKind::Unknown );
}

TEST_CASE( "non-numeric CLOUDCOVER is a typed unknown with an explicit note",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "CLOUDCOVER", "scattered", "gdal:CLOUDCOVER" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.validity.hasCloudCover );
    REQUIRE( claimFor( state, "validity.cloud_cover" ).kind == ClaimKind::Unknown );
    REQUIRE( hasNote( state, "validity.cloud_cover_invalid" ) );
}

// ---------------------------------------------------------------------------
// Validity — QA vocabulary
// ---------------------------------------------------------------------------

TEST_CASE( "declared SICNU_QA_VOCABULARY wins as a known quality mask info",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_QA_VOCABULARY", "s2_scl", "gdal:SICNU_QA_VOCABULARY" );

    SensorProfileFacts profile;
    profile.sensorKey = "sentinel2_msi";
    profile.qaVocabulary = "landsat_qa_pixel";

    StateResolutionInput input;
    input.dataset = dataset;
    input.sensorProfile = profile;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.validity.qualityMaskInfo == "s2_scl" );
    const ClaimRecord claim = claimFor( state, "validity.quality_mask_info" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "gdal:SICNU_QA_VOCABULARY" } );
    REQUIRE( !hasNote( state, "validity.qa_from_sensor_profile" ) );
}

TEST_CASE( "QA vocabulary falls back to the sensor profile as an inferred claim",
           "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();

    SensorProfileFacts profile;
    profile.sensorKey = "landsat_oli";
    profile.qaVocabulary = "landsat_qa_pixel";

    StateResolutionInput input;
    input.dataset = dataset;
    input.sensorProfile = profile;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.validity.qualityMaskInfo == "landsat_qa_pixel" );
    const ClaimRecord claim = claimFor( state, "validity.quality_mask_info" );
    REQUIRE( claim.kind == ClaimKind::Inferred );
    REQUIRE( claim.sources == std::vector<std::string>{ "sensor_profile:landsat_oli" } );
    REQUIRE( hasNote( state, "validity.qa_from_sensor_profile" ) );
}

TEST_CASE( "no QA source leaves quality mask info empty", "[scientific_state][slice_c]" )
{
    DatasetFacts dataset = makeOneBandDataset();

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.validity.qualityMaskInfo.empty() );
}

// ---------------------------------------------------------------------------
// Temporal
// ---------------------------------------------------------------------------

TEST_CASE( "catalog temporal refs project sorted with a known claim",
           "[scientific_state][slice_c]" )
{
    CatalogFacts catalog;
    catalog.assetId = "x";
    TemporalStateRef previous;
    previous.collectionId = "t-season-2023";
    previous.role = "previous";
    TemporalStateRef baseline;
    baseline.collectionId = "t-baseline";
    baseline.role = "baseline";
    catalog.temporalRefs = { previous, baseline };

    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.hasTemporalRefs );
    REQUIRE( state.temporalRefs.size() == 2 );
    REQUIRE( state.temporalRefs[0].collectionId == "t-baseline" );
    REQUIRE( state.temporalRefs[1].collectionId == "t-season-2023" );
    REQUIRE( !state.temporalRefsTruncated );

    const ClaimRecord claim = claimFor( state, "temporal.refs" );
    REQUIRE( claim.kind == ClaimKind::Known );
    REQUIRE( claim.sources == std::vector<std::string>{ "catalog:collection" } );
}

TEST_CASE( "temporal refs beyond kMaxPassportTemporalRefs are truncated with a note",
           "[scientific_state][slice_c]" )
{
    CatalogFacts catalog;
    catalog.assetId = "x";
    for ( std::size_t i = 0; i < kMaxPassportTemporalRefs + 1; ++i )
    {
        TemporalStateRef ref;
        ref.collectionId = "t-" + std::to_string( i );
        ref.role = "scene";
        catalog.temporalRefs.push_back( ref );
    }

    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.temporalRefs.size() == kMaxPassportTemporalRefs );
    REQUIRE( state.temporalRefsTruncated );
    REQUIRE( hasNote( state, "temporal.truncated" ) );
}

TEST_CASE( "exactly kMaxPassportTemporalRefs refs are not truncated",
           "[scientific_state][slice_c]" )
{
    CatalogFacts catalog;
    catalog.assetId = "x";
    for ( std::size_t i = 0; i < kMaxPassportTemporalRefs; ++i )
    {
        TemporalStateRef ref;
        ref.collectionId = "t-" + std::to_string( i );
        catalog.temporalRefs.push_back( ref );
    }

    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.temporalRefs.size() == kMaxPassportTemporalRefs );
    REQUIRE( !state.temporalRefsTruncated );
    REQUIRE( !hasNote( state, "temporal.truncated" ) );
}

TEST_CASE( "empty catalog projects no temporal refs", "[scientific_state][slice_c]" )
{
    CatalogFacts catalog;
    catalog.assetId = "x";

    StateResolutionInput input;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.hasTemporalRefs );
    REQUIRE( state.temporalRefs.empty() );
    REQUIRE( !state.temporalRefsTruncated );
}
