/***************************************************************************
  tests/test_io_canonical_metadata.cpp
  Geospatial I/O Foundation 4.0 — canonical metadata model suite.
  Fixtures are generated at runtime (repo convention: no committed geodata).
 ***************************************************************************/

#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/crs/crs_policy.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <gdal_priv.h>

#include <filesystem>
#include <mutex>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

/// Writes a small 3-band Float32 GeoTIFF with rich metadata: CRS EPSG:4326,
/// geotransform, per-band nodata/scale/offset/unit/description/role/wavelength.
std::string writeRichGeoTiff( const std::string &dir )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / "rich_meta.tif" ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  const char *options[] = { "COMPRESS=LZW", nullptr };
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 8, 6, 3, GDT_Float32, const_cast<char **>( options ) );
  REQUIRE( dataset );

  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  REQUIRE( OSRImportFromEPSG( srs, 4326 ) == OGRERR_NONE );
  char *wkt = nullptr;
  REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
  const char *projectionWkt = wkt;
  OSRDestroySpatialReference( srs );

  double geotransform[6] = { 100.0, 0.5, 0.0, 40.0, 0.0, -0.5 };
  REQUIRE( GDALSetGeoTransform( dataset, geotransform ) == CE_None );
  REQUIRE( GDALSetProjection( dataset, projectionWkt ) == CE_None );
  CPLFree( wkt );

  struct BandSpec
  {
    const char *description;
    const char *role;
    double wavelengthNm;
    double nodata;
    double scale;
    double offset;
    const char *unit;
  };
  const BandSpec specs[3] = {
    { "Blue band", "Blue", 490.0, -9999.0, 0.0001, -0.1, "reflectance" },
    { "Green band", "Green", 560.0, -9999.0, 0.0001, -0.1, "reflectance" },
    { "NIR band", "NIR", 865.0, -9999.0, 0.0001, -0.1, "reflectance" },
  };
  for ( int i = 1; i <= 3; ++i )
  {
    GDALRasterBandH band = GDALGetRasterBand( dataset, i );
    REQUIRE( band );
    const BandSpec &spec = specs[i - 1];
    GDALSetDescription( band, spec.description );
    REQUIRE( GDALSetRasterNoDataValue( band, spec.nodata ) == CE_None );
    REQUIRE( GDALSetRasterScale( band, spec.scale ) == CE_None );
    REQUIRE( GDALSetRasterOffset( band, spec.offset ) == CE_None );
    GDALSetRasterUnitType( band, spec.unit );
    GDALSetMetadataItem( band, "SICNU_BAND_ROLE", spec.role, nullptr );
    GDALSetMetadataItem( band, "WAVELENGTH", std::to_string( spec.wavelengthNm ).c_str(), nullptr );
    float pixels[8 * 6];
    for ( int p = 0; p < 8 * 6; ++p )
      pixels[p] = static_cast<float>( i * 100 + p );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 8, 6, pixels, 8, 6, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALSetMetadataItem( dataset, "SICNU_SENSOR", "TEST_SENSOR_1", nullptr );
  GDALSetMetadataItem( dataset, "SICNU_PRODUCT_ID", "TST_2026_A", nullptr );
  GDALSetMetadataItem( dataset, "SICNU_ACQUISITION_DATE", "2026-09-07T02:00:00Z", nullptr );
  GDALSetMetadataItem( dataset, "SICNU_RADIOMETRIC_STATE", "toa_reflectance", nullptr );
  GDALSetMetadataItem( dataset, "SICNU_NUMERIC_SCALE", "10000", nullptr );
  GDALClose( dataset );
  return path;
}

std::string tempDir()
{
  static std::string dir;
  if ( dir.empty() )
  {
    const auto base = fs::temp_directory_path() / "sicnu_io_test_canonical";
    std::error_code ec;
    fs::remove_all( base, ec ); // idempotent suites
    fs::create_directories( base );
    dir = base.string();
  }
  return dir;
}

} // namespace

TEST_CASE( "inspectRaster reads full canonical structure without scanning pixels", "[io][metadata]" )
{
  const std::string path = writeRichGeoTiff( tempDir() );
  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( path );

  CHECK( meta.driver == "GTiff" );
  CHECK( meta.width == 8 );
  CHECK( meta.height == 6 );
  CHECK( meta.bandCount == 3 );
  CHECK( meta.compression == "LZW" );
  REQUIRE( meta.bands.size() == 3 );

  CHECK( meta.crs.valid );
  CHECK( meta.crs.authid == "EPSG:4326" );
  CHECK( meta.crs.isGeographic );
  CHECK_FALSE( meta.crs.isProjected );

  CHECK( meta.hasGeotransform );
  CHECK( meta.hasExtent );
  CHECK( meta.minX == Approx( 100.0 ).margin( 1e-9 ) );
  CHECK( meta.maxX == Approx( 104.0 ).margin( 1e-9 ) );
  CHECK( meta.minY == Approx( 37.0 ).margin( 1e-9 ) );
  CHECK( meta.maxY == Approx( 40.0 ).margin( 1e-9 ) );
  CHECK( meta.resolutionX == Approx( 0.5 ).margin( 1e-9 ) );
  CHECK( meta.resolutionY == Approx( 0.5 ).margin( 1e-9 ) );

  const sicnu::geo::BandInfo &blue = meta.bands[0];
  CHECK( blue.dtype == "Float32" );
  CHECK( blue.description == "Blue band" );
  CHECK( blue.hasNoData );
  CHECK( blue.noDataValue == Approx( -9999.0 ) );
  CHECK_FALSE( blue.noDataIsNaN );
  CHECK( blue.hasScale );
  CHECK( blue.scale == Approx( 0.0001 ) );
  CHECK( blue.hasOffset );
  CHECK( blue.offset == Approx( -0.1 ) );
  CHECK( blue.unit == "reflectance" );
  CHECK( blue.role == "Blue" );
  CHECK( blue.hasWavelength );
  CHECK( blue.wavelengthNm == Approx( 490.0 ) );

  CHECK( meta.bands[2].role == "NIR" );
  CHECK( meta.bands[2].wavelengthNm == Approx( 865.0 ) );

  // Product semantics come from metadata stamps only — all declared here.
  CHECK( meta.sensor == "TEST_SENSOR_1" );
  CHECK( meta.productId == "TST_2026_A" );
  CHECK( meta.acquisitionTime == "2026-09-07T02:00:00Z" );
  CHECK( meta.radiometricState == "toa_reflectance" );
  CHECK( meta.numericScale == Approx( 10000.0 ) );
}

TEST_CASE( "canonical metadata JSON round-trips losslessly", "[io][metadata]" )
{
  const std::string path = writeRichGeoTiff( tempDir() );
  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( path );

  const Json::Value json = meta.toJson();
  REQUIRE( json["kind"].asString() == "raster" );
  REQUIRE( json["format_version"].asInt() == 1 );

  const Json::StreamWriterBuilder builder;
  const std::string text = Json::writeString( builder, json );
  Json::Value errors;
  const sicnu::geo::RasterMetadata restored = sicnu::geo::rasterMetadataFromJsonText( text, &errors );
  REQUIRE( errors.isNull() );

  CHECK( restored.driver == meta.driver );
  CHECK( restored.width == meta.width );
  CHECK( restored.height == meta.height );
  CHECK( restored.bandCount == meta.bandCount );
  CHECK( restored.crs.authid == meta.crs.authid );
  CHECK( restored.hasExtent );
  REQUIRE( restored.bands.size() == meta.bands.size() );
  for ( std::size_t i = 0; i < meta.bands.size(); ++i )
  {
    CHECK( restored.bands[i].dtype == meta.bands[i].dtype );
    CHECK( restored.bands[i].hasNoData == meta.bands[i].hasNoData );
    CHECK( restored.bands[i].noDataValue == Approx( meta.bands[i].noDataValue ) );
    CHECK( restored.bands[i].role == meta.bands[i].role );
    CHECK( restored.bands[i].hasScale == meta.bands[i].hasScale );
    CHECK( restored.bands[i].scale == Approx( meta.bands[i].scale ) );
  }
}

TEST_CASE( "metadata absence is explicit, never fabricated", "[io][metadata]" )
{
  ensureGdal();
  // A bare PNG-style raster: no CRS, no geotransform, no nodata.
  const std::string path = ( fs::path( tempDir() ) / "bare.tif" ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALClose( dataset );

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( path );
  CHECK_FALSE( meta.crs.valid );
  CHECK_FALSE( meta.hasGeotransform );
  CHECK_FALSE( meta.hasExtent );
  REQUIRE( meta.bands.size() == 1 );
  CHECK_FALSE( meta.bands[0].hasNoData );
  CHECK_FALSE( meta.bands[0].hasScale );
  CHECK_FALSE( meta.bands[0].hasOffset );
  CHECK( meta.bands[0].role.empty() );
  CHECK( meta.sensor.empty() );
  CHECK( meta.radiometricState.empty() );
}

TEST_CASE( "inspectVector describes GeoJSON layers cheaply", "[io][metadata]" )
{
  ensureGdal();
  const std::string path = ( fs::path( tempDir() ) / "points.geojson" ).string();
  {
    std::ofstream out( path );
    REQUIRE( out.is_open() );
    out << R"json({
      "type": "FeatureCollection",
      "name": "points",
      "crs": { "type": "name", "properties": { "name": "urn:ogc:def:crs:OGC:1.3:CRS84" } },
      "features": [
        { "type": "Feature", "properties": { "name": "a", "value": 1 },
          "geometry": { "type": "Point", "coordinates": [117.2, 30.5] } },
        { "type": "Feature", "properties": { "name": "b", "value": 2 },
          "geometry": { "type": "Point", "coordinates": [117.3, 30.6] } },
        { "type": "Feature", "properties": { "name": "c", "value": 3 },
          "geometry": { "type": "Point", "coordinates": [117.4, 30.7] } }
      ]
    })json";
  }

  const sicnu::geo::VectorMetadata meta = sicnu::geo::inspectVector( path );
  CHECK( meta.driver == "GeoJSON" );
  REQUIRE( meta.layers.size() == 1 );
  const sicnu::geo::VectorLayerInfo &layer = meta.layers[0];
  CHECK( layer.featureCount == 3 );
  CHECK( layer.featureCountExact );
  CHECK( layer.geometryTypeName.find( "Point" ) != std::string::npos );
  REQUIRE( layer.fields.size() == 2 );
  CHECK( layer.fields[0].name == "name" );
  CHECK( layer.hasExtent );
  CHECK( layer.minX == Approx( 117.2 ).margin( 1e-6 ) );
}

TEST_CASE( "inspection failures are structured, not silent", "[io][metadata]" )
{
  CHECK_THROWS_AS( sicnu::geo::inspectRaster( "" ), sicnu::geo::GeoError );

  try
  {
    sicnu::geo::inspectRaster( ( fs::path( tempDir() ) / "no_such_file.tif" ).string() );
    FAIL( "expected OpenFailed" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::OpenFailed );
    CHECK( std::string( error.what() ).find( "no_such_file" ) != std::string::npos );
  }

  // A text file masquerading as .tif must fail open, not produce junk metadata.
  const std::string junkPath = ( fs::path( tempDir() ) / "junk.tif" ).string();
  { std::ofstream junk( junkPath ); junk << "this is not a tiff"; }
  try
  {
    sicnu::geo::inspectRaster( junkPath );
    FAIL( "expected OpenFailed for malformed dataset" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::OpenFailed );
  }
}
