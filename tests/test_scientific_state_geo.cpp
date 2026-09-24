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

// ---------------------------------------------------------------------------
// Acquisition time normalization (R2)
//
// The harness (workflow_facts parseInstant) and the suitability criteria
// (QDateTime) both parse acquisition timestamps; the resolver accepted any
// string verbatim and compared observations as raw text. The chain must
// agree: same instant in different spellings is ONE fact, and a string the
// rest of the chain cannot parse is a typed unknown, never a Known time.
// Canonical form: midnight-UTC instants collapse to the date-only spelling
// (the coarsest faithful representation — a date is never widened to a
// fabricated time-of-day); every other instant renders as
// "YYYY-MM-DDTHH:MM:SS[.frac]Z" with the offset applied (naive sources are
// UTC by the same convention workflow_facts documents).
// ---------------------------------------------------------------------------

TEST_CASE( "same instant in different spellings resolves once, never conflicts",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23 00:00:00",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23T00:00:00Z";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2026-09-23" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
    REQUIRE( !hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "zone offsets normalize before instants are compared",
           "[scientific_state][r2][acquisition_time]" )
{
    // 08:00+08:00 IS 00:00Z IS the date-only fact; master compared raw text
    // and reported a conflict for one physical acquisition.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23T08:00:00+08:00",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2026-09-23" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
    REQUIRE( !hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "already-canonical timestamps round-trip unchanged",
           "[scientific_state][r2][acquisition_time]" )
{
    // Existing passports pin "2024-05-01T10:12:31Z"; normalization must be
    // the identity on the canonical form.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2024-05-01T10:12:31Z",
                          "gdal:SICNU_ACQUISITION_DATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2024-05-01T10:12:31Z" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
}

TEST_CASE( "non-midnight naive sources carry the documented UTC convention",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23 05:06:07",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23T05:06:07Z";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2026-09-23T05:06:07Z" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
    REQUIRE( !hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "genuinely different instants still conflict, alternatives kept",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-24T00:00:00Z";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.acquisition.valid );
    const ClaimRecord claim = claimFor( state, "acquisition.time" );
    REQUIRE( claim.kind == ClaimKind::Conflicted );
    REQUIRE( claim.alternatives.size() == 2 );
    REQUIRE( hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "unparsable acquisition text is a typed unknown, never a Known time",
           "[scientific_state][r2][acquisition_time]" )
{
    // RED on master: "September 2026" became acquisition.time Known and
    // travelled the chain as a fact no consumer could parse.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "September 2026",
                          "gdal:SICNU_ACQUISITION_DATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.acquisition.valid );
    REQUIRE( unknownsContain( state, "acquisition.time" ) );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Unknown );
    REQUIRE( hasNote( state, "acquisition.time_unparseable" ) );
}

TEST_CASE( "one unparsable observation degrades the field, the usable one survives",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "September 2026",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23T05:06:07Z";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2026-09-23T05:06:07Z" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
    REQUIRE( hasNote( state, "acquisition.time_unparseable" ) );
}

TEST_CASE( "impossible calendar dates are unparsable, never silently normalized",
           "[scientific_state][r2][acquisition_time]" )
{
    for ( const char *text : { "2026-02-30", "2026-13-01", "2026-00-10" } )
    {
        DatasetFacts dataset = makeOneBandDataset();
        dataset.metadata.add( "SICNU_ACQUISITION_DATE", text,
                              "gdal:SICNU_ACQUISITION_DATE" );

        StateResolutionInput input;
        input.dataset = dataset;
        const RemoteSensingAssetState state = resolveAssetState( input ).state;

        INFO( "acquisition text: " << text );
        REQUIRE( !state.acquisition.valid );
        REQUIRE( unknownsContain( state, "acquisition.time" ) );
        REQUIRE( hasNote( state, "acquisition.time_unparseable" ) );
    }
}

TEST_CASE( "fractional spellings normalize to one canonical second value",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23T05:06:07.500Z",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23T05:06:07.5Z";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "2026-09-23T05:06:07.5Z" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
    REQUIRE( !hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "pre-epoch instants normalize through negative epochs correctly",
           "[scientific_state][r2][acquisition_time]" )
{
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "1961-08-21T00:00:00Z",
                          "gdal:SICNU_ACQUISITION_DATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "1961-08-21" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
}

TEST_CASE( "zone offsets beyond ±23:59 or with minute overflow are refused",
           "[scientific_state][r2][acquisition_time]" )
{
    // Review R2 P1: the composed-offset range check divided signed values
    // with truncating division, so "-99:00" (a 4-day silent shift) and
    // "+00:99" were accepted. Bounds are per digit field now.
    for ( const char *text : { "2026-09-23T00:00:00-99:00", "2026-09-23T00:00:00+00:99",
                               "2026-09-23T00:00:00-2400", "2026-09-23T00:00:00+24:00" } )
    {
        DatasetFacts dataset = makeOneBandDataset();
        dataset.metadata.add( "SICNU_ACQUISITION_DATE", text,
                              "gdal:SICNU_ACQUISITION_DATE" );

        StateResolutionInput input;
        input.dataset = dataset;
        const RemoteSensingAssetState state = resolveAssetState( input ).state;

        INFO( "acquisition text: " << text );
        REQUIRE( !state.acquisition.valid );
        REQUIRE( unknownsContain( state, "acquisition.time" ) );
        REQUIRE( hasNote( state, "acquisition.time_unparseable" ) );
    }
}

TEST_CASE( "a nonzero fraction at midnight is part of the instant, never collapsed",
           "[scientific_state][r2][acquisition_time]" )
{
    // Review R2 P2: 00:00:00.5Z collapsed to the date-only form, merging
    // distinct instants into one Known date fact.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23T00:00:00.5Z",
                          "gdal:SICNU_ACQUISITION_DATE" );
    CatalogFacts catalog;
    catalog.assetId = "x";
    catalog.acquisitionTimeIso = "2026-09-23";

    StateResolutionInput input;
    input.dataset = dataset;
    input.catalog = catalog;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    // The canonical spelling of the fraction survives; the instant is
    // genuinely different from the catalog's date-only fact, so the pair
    // is a real conflict, and valid stays false (a conflicted time is not
    // a usable time).
    const ClaimRecord claim = claimFor( state, "acquisition.time" );
    REQUIRE( claim.kind == ClaimKind::Conflicted );
    REQUIRE( claim.alternatives.size() == 2 );
    REQUIRE( std::find( claim.alternatives.begin(), claim.alternatives.end(),
                        "2026-09-23T00:00:00.5Z" ) != claim.alternatives.end() );
    REQUIRE( !state.acquisition.valid );
    REQUIRE( hasNote( state, "acquisition.conflict" ) );
}

TEST_CASE( "the canonical spelling survives the year-10000 zone rollover",
           "[scientific_state][r2][acquisition_time]" )
{
    // Review R2 P2: an 11-char date buffer truncated "10000-01-01…" into a
    // corrupt "10000-01-0…" the module itself could not re-read.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "9999-12-31T23:30:00-01:00",
                          "gdal:SICNU_ACQUISITION_DATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( state.acquisition.valid );
    REQUIRE( state.acquisition.timeIso == "10000-01-01T00:30:00Z" );
    REQUIRE( claimFor( state, "acquisition.time" ).kind == ClaimKind::Known );
}

TEST_CASE( "decimal minutes are outside the closed subset, never re-weighted",
           "[scientific_state][r2][acquisition_time]" )
{
    // Review R2 P3: "05:06.5" (ISO decimal minutes = 05:06:30) was parsed
    // as five-tenths of a SECOND.
    DatasetFacts dataset = makeOneBandDataset();
    dataset.metadata.add( "SICNU_ACQUISITION_DATE", "2026-09-23T05:06.5Z",
                          "gdal:SICNU_ACQUISITION_DATE" );

    StateResolutionInput input;
    input.dataset = dataset;
    const RemoteSensingAssetState state = resolveAssetState( input ).state;

    REQUIRE( !state.acquisition.valid );
    REQUIRE( unknownsContain( state, "acquisition.time" ) );
    REQUIRE( hasNote( state, "acquisition.time_unparseable" ) );
}
