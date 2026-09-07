/***************************************************************************
  tests/test_io_stac.cpp — STAC interoperability suite (Phase 7).
 ***************************************************************************/

#include "geospatial/stac/stac_mapper.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

TEST_CASE( "STAC structural violations are structured errors", "[io][stac]" )
{
  CHECK_THROWS_AS( sicnu::geo::StacItem::parseText( "not json" ), sicnu::geo::GeoError );
  CHECK_THROWS_AS( sicnu::geo::StacItem::parseText( "{}" ), sicnu::geo::GeoError );

  const std::string noDatetime = R"({
    "type": "Feature", "id": "x",
    "properties": {},
    "assets": { "data": { "href": "a.tif" } }
  })";
  CHECK_THROWS_AS( sicnu::geo::StacItem::parseText( noDatetime ), sicnu::geo::GeoError );

  const std::string noAssets = R"({
    "type": "Feature", "id": "x",
    "properties": { "datetime": "2026-01-01T00:00:00Z" },
    "assets": {}
  })";
  CHECK_THROWS_AS( sicnu::geo::StacItem::parseText( noAssets ), sicnu::geo::GeoError );
}

TEST_CASE( "STAC projection, EO and SAR extensions map into canonical metadata", "[io][stac][extensions]" )
{
  const sicnu::geo::StacItem item = sicnu::geo::StacItem::parseText( R"({
    "type": "Feature", "stac_version": "1.0.0", "id": "S1A_IW_GRDH_1SDV_TEST",
    "bbox": [103.0, 29.5, 105.5, 31.0],
    "geometry": { "type": "Polygon", "coordinates": [] },
    "properties": {
      "datetime": "2026-07-14T22:03:41Z",
      "platform": "SENTINEL-1A",
      "sar:polarizations": ["VV", "VH"],
      "sar:instrument_mode": "IW",
      "sar:product_type": "GRD",
      "eo:cloud_cover": 0.0,
      "proj:epsg": 32648
    },
    "assets": { "vv": { "href": "s1.tiff", "type": "image/tiff; application=geotiff", "roles": ["data"] } }
  })" );

  CHECK( item.polarizations.size() == 2 );
  CHECK( item.epsg == "EPSG:32648" );

  const sicnu::geo::RasterMetadata canonical = sicnu::geo::stacItemToCanonical( item );
  CHECK( canonical.platform == "SENTINEL-1A" );
  CHECK( canonical.acquisitionTime == "2026-07-14T22:03:41Z" );
  CHECK( canonical.metadata.at( "sar:polarizations" ) == "VV,VH" );
  CHECK( canonical.crs.authid == "EPSG:32648" );
}

TEST_CASE( "canonical → STAC requires the fields STAC mandates", "[io][stac][generation]" )
{
  sicnu::geo::RasterMetadata bare; // no acquisition time
  bare.hasExtent = true;
  CHECK_THROWS_AS( sicnu::geo::canonicalToStacItem( bare, "a.tif", "x" ), sicnu::geo::GeoError );

  sicnu::geo::RasterMetadata full;
  full.acquisitionTime = "2026-09-01T00:00:00Z";
  full.hasExtent = true;
  full.minX = 0.0;
  full.minY = 0.0;
  full.maxX = 1.0;
  full.maxY = 1.0;
  sicnu::geo::BandInfo band;
  band.role = "NIR";
  band.hasWavelength = true;
  band.wavelengthNm = 842.0;
  full.bands.push_back( band );

  const Json::Value item = sicnu::geo::canonicalToStacItem( full, "https://example.test/x.tif", "item-1" );
  CHECK( item["id"].asString() == "item-1" );
  CHECK( item["properties"]["datetime"].asString() == "2026-09-01T00:00:00Z" );
  REQUIRE( item["bbox"].size() == 4 );
  CHECK( item["bbox"][0].asDouble() == 0.0 );
  CHECK( item["geometry"]["type"].asString() == "Polygon" );
  REQUIRE( item["properties"]["eo:bands"].size() == 1 );
  CHECK( item["properties"]["eo:bands"][0]["center_wavelength"].asDouble() == Approx( 0.842 ) );
  CHECK( item["assets"]["data"]["roles"][0].asString() == "data" );
}

TEST_CASE( "start/end datetime range satisfies the STAC time contract", "[io][stac][time]" )
{
  const sicnu::geo::StacItem item = sicnu::geo::StacItem::parseText( R"({
    "type": "Feature", "id": "range-item",
    "properties": { "start_datetime": "2026-01-01T00:00:00Z", "end_datetime": "2026-01-31T23:59:59Z" },
    "assets": { "data": { "href": "a.tif" } }
  })" );
  CHECK( item.startDatetime == "2026-01-01T00:00:00Z" );
  CHECK( item.datetime.empty() );

  const sicnu::geo::RasterMetadata canonical = sicnu::geo::stacItemToCanonical( item );
  CHECK( canonical.acquisitionTime == "2026-01-01T00:00:00Z" );
}
